#include <QDir>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QtTest>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "arguments.h"
#include "document.h"
#include "editing.h"
#include "highlighter.h"
#include "theme.h"
#include "translator.h"

class PlainTests : public QObject {
    Q_OBJECT

private slots:
    // Decoding.

    void decodesLfAndCrlf()
    {
        auto lf = Document::decode("a\nb\n");
        QVERIFY(lf.ok);
        QVERIFY(!lf.crlf);
        QCOMPARE(lf.text, QString("a\nb\n"));

        auto crlf = Document::decode("a\r\nb\r\n");
        QVERIFY(crlf.ok);
        QVERIFY(crlf.crlf);
        QVERIFY(!crlf.mixed);
        QCOMPARE(crlf.text, QString("a\nb\n"));

        auto mixed = Document::decode("a\r\nb\r\nc\n");
        QVERIFY(mixed.ok);
        QVERIFY(mixed.crlf);
        QVERIFY(mixed.mixed);
    }

    void refusesWhatCantRoundTrip()
    {
        QVERIFY(!Document::decode(QByteArray("a\0b", 3)).ok);
        QVERIFY(!Document::decode("caf\xE9\n").ok);           // Latin-1, not UTF-8
        QVERIFY(!Document::decode("a\rb\n").ok);              // lone CR
        QVERIFY(!Document::decode("a\xE2\x80\xA9" "b").ok);   // U+2029
        QVERIFY(Document::decode("").ok);
    }

    void keepsTheBom()
    {
        auto d = Document::decode("\xEF\xBB\xBFkey = 1\n");
        QVERIFY(d.ok);
        QVERIFY(d.bom);
        QCOMPARE(d.text, QString("key = 1\n"));
    }

    void roundTripsByteForByte_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("lf") << QByteArray("a = 1\nb = 2\n");
        QTest::newRow("no final newline") << QByteArray("a = 1\nb = 2");
        QTest::newRow("blank final lines") << QByteArray("a\n\n\n");
        QTest::newRow("crlf") << QByteArray("[s]\r\nk=v\r\n");
        QTest::newRow("bom") << QByteArray("\xEF\xBB\xBF" "x\n");
        QTest::newRow("double bom") << QByteArray("\xEF\xBB\xBF\xEF\xBB\xBF" "x\n");
        QTest::newRow("nbsp and tabs") << QByteArray("a\xC2\xA0" "b\n\tc\n");
        QTest::newRow("line separator") << QByteArray("a\xE2\x80\xA8" "b\n");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("only newline") << QByteArray("\n");
        QTest::newRow("unicode") << QByteArray("名前 = \"ø\" # 🎉\n");
    }

    void roundTripsByteForByte()
    {
        QFETCH(QByteArray, bytes);
        const auto decoded = Document::decode(bytes);
        QVERIFY(decoded.ok);
        QTextDocument document;
        document.setPlainText(decoded.text);
        QCOMPARE(Document::encode(&document, decoded.bom, decoded.crlf), bytes);
    }

    void detectsIndentation()
    {
        QCOMPARE(Document::detectIndent("a {\n    b {\n        c\n    }\n}\n"), QString("    "));
        QCOMPARE(Document::detectIndent("a:\n  b:\n    c: 1\n  d: 2\n"), QString("  "));
        QCOMPARE(Document::detectIndent("a {\n\tb\n\tc\n}\n"), QString("\t"));
        QCOMPARE(Document::detectIndent("flat\nfile\n"), QString());
    }

    // Opening.

    void opensAndRefuses()
    {
        QTemporaryDir dir;
        QTextDocument text;
        Document doc;
        doc.attachDocument(&text);

        QVERIFY(!doc.open(dir.filePath("missing/new.conf")));
        QCOMPARE(doc.problem(), QString("NotFound"));

        QVERIFY(!doc.open(dir.path()));
        QCOMPARE(doc.problem(), QString("NotRegular"));

        QVERIFY(::mkfifo(QFile::encodeName(dir.filePath("fifo")).constData(), 0600) == 0);
        QVERIFY(!doc.open(dir.filePath("fifo")));
        QCOMPARE(doc.problem(), QString("NotRegular"));

        write(dir.filePath("binary"), QByteArray("\x7F" "ELF\0\0", 6));
        QVERIFY(!doc.open(dir.filePath("binary")));
        QCOMPARE(doc.problem(), QString("NotText"));
        QVERIFY(!doc.editable());

        write(dir.filePath("secret"), "x\n");
        QFile::setPermissions(dir.filePath("secret"), QFile::WriteOwner);
        if (::geteuid() != 0) {
            QVERIFY(!doc.open(dir.filePath("secret")));
            QCOMPARE(doc.problem(), QString("NotReadable"));
        }

        write(dir.filePath("ok.toml"), "a = 1\r\nb = 2\n");
        QVERIFY(doc.open(dir.filePath("ok.toml")));
        QCOMPARE(doc.problem(), QString("MixedEndings"));
        QCOMPARE(doc.language(), QString("toml"));
        QVERIFY(!doc.modified());
    }

    // Saving.

    void savesAtomicallyKeepingTheMode()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("a.conf");
        write(path, "a = 1\n");
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
        const ino_t before = inode(path);

        QTextDocument text;
        Document doc;
        doc.attachDocument(&text);
        QVERIFY(doc.open(path));
        append(&text, "b = 2\n");
        QVERIFY(doc.modified());
        QVERIFY(doc.save());
        QVERIFY(!doc.modified());
        QCOMPARE(read(path), QByteArray("a = 1\nb = 2\n"));
        QCOMPARE(mode(path), 0640);
        QVERIFY(inode(path) != before); // replaced, not rewritten
    }

    void savesThroughSymlinks()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("store");
        const QString target = dir.filePath("store/real.lua"), link = dir.filePath("link.lua");
        write(target, "x = 1\n");
        QVERIFY(QFile::link(target, link));

        QTextDocument text;
        Document doc;
        doc.attachDocument(&text);
        QVERIFY(doc.open(link));
        QCOMPARE(doc.path(), link);
        append(&text, "y = 2\n");
        QVERIFY(doc.save());
        QVERIFY(QFileInfo(link).isSymLink());
        QCOMPARE(read(target), QByteArray("x = 1\ny = 2\n"));
    }

    void asksWhenALinkIsRetargeted()
    {
        QTemporaryDir dir;
        const QString one = dir.filePath("one"), two = dir.filePath("two"), link = dir.filePath("link");
        write(one, "1\n");
        write(two, "2\n");
        QVERIFY(QFile::link(one, link));

        QTextDocument text;
        Document doc;
        doc.attachDocument(&text);
        QVERIFY(doc.open(link));
        QFile::remove(link);
        QVERIFY(QFile::link(two, link));
        append(&text, "more\n");
        QVERIFY(!doc.save());
        QCOMPARE(doc.problem(), QString("Retargeted"));
        QCOMPARE(read(two), QByteArray("2\n"));
        QCOMPARE(read(one), QByteArray("1\n"));
    }

    void neverSavesOverAnUnseenVersion()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("a.conf");
        write(path, "a = 1\n");

        QTextDocument text;
        Document doc;
        doc.attachDocument(&text);
        QVERIFY(doc.open(path));
        // Same size, different content: mtime and size wouldn't catch it.
        write(path, "a = 2\n");
        append(&text, "mine\n");
        QVERIFY(!doc.save());
        QCOMPARE(doc.problem(), QString("Conflict"));
        QCOMPARE(read(path), QByteArray("a = 2\n"));

        QVERIFY(doc.overwrite());
        QCOMPARE(read(path), QByteArray("a = 1\nmine\n"));
    }

    void asksBeforeRecreatingADeletedFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("gone.conf");
        write(path, "a\n");
        QTextDocument text;
        Document doc;
        doc.attachDocument(&text);
        QVERIFY(doc.open(path));
        QFile::remove(path);
        append(&text, "b\n");
        QVERIFY(!doc.save());
        QCOMPARE(doc.problem(), QString("DeletedOnDisk"));
        QVERIFY(!QFile::exists(path));
        QVERIFY(doc.overwrite());
        QCOMPARE(read(path), QByteArray("a\nb\n"));
    }

    void asksBeforeWritingInPlace()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("linked.conf"), other = dir.filePath("other-name.conf");
        write(path, "a\n");
        QVERIFY(::link(QFile::encodeName(path).constData(), QFile::encodeName(other).constData()) == 0);

        QTextDocument text;
        Document doc;
        doc.attachDocument(&text);
        QVERIFY(doc.open(path));
        append(&text, "b\n");
        QVERIFY(!doc.save());
        QCOMPARE(doc.problem(), QString("NeedsInPlace"));
        QCOMPARE(read(path), QByteArray("a\n"));

        QVERIFY(doc.overwriteInPlace());
        QCOMPARE(read(other), QByteArray("a\nb\n")); // the hard link survived
        QVERIFY(!doc.modified());
    }

    void asksBeforeDroppingSpecialBits()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("tool.sh");
        write(path, "echo hi\n");
        QVERIFY(::chmod(QFile::encodeName(path).constData(), 02755) == 0);
        QTextDocument text;
        Document doc;
        doc.attachDocument(&text);
        QVERIFY(doc.open(path));
        append(&text, "echo bye\n");
        QVERIFY(!doc.save());
        QCOMPARE(doc.problem(), QString("NeedsInPlace"));
        QVERIFY(doc.overwriteInPlace());
        QCOMPARE(mode(path), 02755);
    }

    void reportsFilesItCantWrite()
    {
        if (::geteuid() == 0)
            QSKIP("root can write anything");
        QTemporaryDir dir;
        const QString path = dir.filePath("ro.conf");
        write(path, "a\n");
        QFile::setPermissions(path, QFile::ReadOwner);
        QTextDocument text;
        Document doc;
        doc.attachDocument(&text);
        QVERIFY(doc.open(path));
        append(&text, "b\n");
        QVERIFY(!doc.save());
        QCOMPARE(doc.problem(), QString("NotWritable"));
    }

    void createsNewFiles()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("new.yaml");
        QTextDocument text;
        Document doc;
        doc.attachDocument(&text);
        QVERIFY(doc.open(path));
        QCOMPARE(doc.problem(), QString("None"));
        QCOMPARE(doc.indentUnit(), QString("  "));
        append(&text, "a: 1");
        QVERIFY(doc.save());
        QCOMPARE(read(path), QByteArray("a: 1"));
    }

    void followsTheDisk()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("live.conf");
        write(path, "a = 1\n");
        QTextDocument text;
        Document doc;
        doc.attachDocument(&text);
        QVERIFY(doc.open(path));

        // No unsaved changes: reloads quietly, and the reload can be undone.
        replace(path, "a = 2\n");
        QTRY_COMPARE(text.toPlainText(), QString("a = 2\n"));
        QVERIFY(!doc.modified());
        QCOMPARE(doc.problem(), QString("None"));

        // Unsaved changes: asks.
        append(&text, "mine\n");
        replace(path, "a = 3\n");
        QTRY_COMPARE(doc.problem(), QString("ChangedOnDisk"));
        QVERIFY(text.toPlainText().contains("mine"));
        doc.keepMine();
        QVERIFY(doc.save());
        QCOMPARE(read(path), QByteArray("a = 2\nmine\n"));

        // Plain's own save doesn't count as news.
        QTest::qWait(250);
        QCOMPARE(doc.problem(), QString("None"));
    }

    // Arguments.

    void parsesArguments()
    {
        auto a = Arguments::parse({"plain", "+12", "--", "-weird name"});
        QCOMPARE(a.line, 12);
        QCOMPARE(a.path, QString("-weird name"));
        QVERIFY(a.error.isEmpty());

        QCOMPARE(Arguments::parse({"plain", "--", "+5"}).path, QString("+5"));
        QCOMPARE(Arguments::parse({"plain", "file:///etc/hosts"}).path, QString("/etc/hosts"));
        QCOMPARE(Arguments::parse({"plain", "a:12"}).path, QString("a:12"));
        QVERIFY(!Arguments::parse({"plain", "https://example.com/x"}).error.isEmpty());
        QVERIFY(!Arguments::parse({"plain", "a", "b"}).error.isEmpty());
        QVERIFY(!Arguments::parse({"plain", "--wait", "a"}).error.isEmpty());
        QVERIFY(Arguments::parse({"plain"}).path.isEmpty());
    }

    // Languages and highlighting.

    void picksLanguages()
    {
        QCOMPARE(Highlighter::languageFor("/h/.config/hypr/bindings.lua", ""), QString("lua"));
        QCOMPARE(Highlighter::languageFor("/h/.config/hypr/hypridle.conf", ""), QString("conf"));
        QCOMPARE(Highlighter::languageFor("/h/.config/waybar/config.jsonc", ""), QString("jsonc"));
        QCOMPARE(Highlighter::languageFor("/h/.config/waybar/style.css", ""), QString("css"));
        QCOMPARE(Highlighter::languageFor("/h/.bashrc", ""), QString("shell"));
        QCOMPARE(Highlighter::languageFor("/h/PKGBUILD", ""), QString("shell"));
        QCOMPARE(Highlighter::languageFor("/h/bin/thing", "#!/bin/bash"), QString("shell"));
        QCOMPARE(Highlighter::languageFor("/h/notes", "hello"), QString("text"));
        QCOMPARE(Highlighter::commentPrefix("jsonc"), QString("//"));
        QCOMPARE(Highlighter::commentPrefix("text"), QString());
    }

    void highlightsLua()
    {
        QCOMPARE(Highlighter::tokens("lua", {"local x = \"a -- b\" -- c"}),
                 QString("wwwww.k...ssssssss.cccc"));
        // A long comment holding ]] ends only at its own level.
        QCOMPARE(Highlighter::tokens("lua", {"--[==[ a ]] b", "]==] x = 1"}),
                 QString("ccccccccccccc\ncccc.k...n"));
        QCOMPARE(Highlighter::tokens("lua", {"s = 'it\\'s' == nil"}),
                 QString("k...sssssss....xxx"));
        QCOMPARE(Highlighter::tokens("conf", {"; ini comment", "key = a;b"}),
                 QString("ccccccccccccc\nkkk......"));
    }

    void highlightsShell()
    {
        QCOMPARE(Highlighter::tokens("shell", {"echo ${#arr} # note"}),
                 QString(".....xxxxxxx.cccccc"));
        QCOMPARE(Highlighter::tokens("shell", {"export EDITOR='a#b'"}),
                 QString("wwwwww.kkkkkk.sssss"));
    }

    void highlightsDataFormats()
    {
        QCOMPARE(Highlighter::tokens("jsonc", {"{\"a\": \"b\", // c"}),
                 QString(".kkk..sss..cccc"));
        QCOMPARE(Highlighter::tokens("toml", {"[server]", "msg = \"\"\"", "# not a comment", "\"\"\""}),
                 QString("wwwwwwww\nkkk...sss\nsssssssssssssss\nsss"));
        QCOMPARE(Highlighter::tokens("yaml", {"- name: x#y # z", "  on: true"}),
                 QString("..kkkk......ccc\n..xx..xxxx"));
        QCOMPARE(Highlighter::tokens("conf", {"general {", "    gaps_in = 5 # px", "}"}),
                 QString("wwwwwww..\n....kkkkkkk...n.cccc\n."));
        QCOMPARE(Highlighter::tokens("css", {"@define-color fg #ffcead; /* c */"}),
                 QString("wwwwwwwwwwwww....nnnnnnn..ccccccc"));
    }

    // Edits, through a real TextEdit and its own undo stack.

    void editsAreSingleUndoSteps()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nTextEdit { textFormat: TextEdit.PlainText }", {});
        std::unique_ptr<QQuickItem> edit(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY(edit);
        auto *quickDocument = edit->property("textDocument").value<QQuickTextDocument *>();

        QTemporaryDir dir;
        const QString path = dir.filePath("t.lua");
        write(path, "a = 1\n    b = 2\n");
        Document doc;
        doc.attach(quickDocument);
        Editing editing(&doc);
        QVERIFY(doc.open(path));
        auto text = [&] { return edit->property("text").toString(); };
        auto undo = [&] { QMetaObject::invokeMethod(edit.get(), "undo"); };
        auto redo = [&] { QMetaObject::invokeMethod(edit.get(), "redo"); };
        QVERIFY(!edit->property("canUndo").toBool());

        editing.toggleComment(0, 12);
        // Lined up at the shallowest indentation of the lines, as VSCode does.
        QCOMPARE(text(), QString("-- a = 1\n--     b = 2\n"));
        undo();
        QCOMPARE(text(), QString("a = 1\n    b = 2\n"));
        QVERIFY(!doc.modified());
        redo();
        QCOMPARE(text(), QString("-- a = 1\n--     b = 2\n"));
        editing.toggleComment(0, 20);
        QCOMPARE(text(), QString("a = 1\n    b = 2\n"));

        editing.indent(0, 10);
        QCOMPARE(text(), QString("    a = 1\n        b = 2\n"));
        editing.outdent(0, 20);
        editing.outdent(0, 20);
        QCOMPARE(text(), QString("a = 1\nb = 2\n"));
        undo();
        QCOMPARE(text(), QString("a = 1\n    b = 2\n"));

        // Enter keeps the indentation.
        const int end = text().indexOf("2") + 1;
        QCOMPARE(editing.newline(end, end), end + 5);
        QCOMPARE(text(), QString("a = 1\n    b = 2\n    \n"));
        undo();

        QCOMPARE(editing.matchCount("= ", false), 2);
        QCOMPARE(editing.replaceAll("= ", ":= ", true), 2);
        QCOMPARE(text(), QString("a := 1\n    b := 2\n"));
        undo();
        QCOMPARE(text(), QString("a = 1\n    b = 2\n"));
        QCOMPARE(editing.find("=", 3, false, false), QVariantList({12, 13}));
        QCOMPARE(editing.find("=", 13, false, false), QVariantList({2, 3})); // wraps around
        QCOMPARE(editing.lineOf(7), 2);
        QCOMPARE(editing.positionOfLine(2), 6);
    }

    // Theme and translator, from the template.

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QColor("#FFD60A"));
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(contents);
    }

    // Replaces the file the way other editors do, so the watch has to re-arm.
    static void replace(const QString &path, const QByteArray &contents)
    {
        write(path + ".tmp", contents);
        QVERIFY(::rename(QFile::encodeName(path + ".tmp").constData(), QFile::encodeName(path).constData()) == 0);
    }

    static QByteArray read(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<unreadable>");
    }

    static void append(QTextDocument *document, const QString &text)
    {
        QTextCursor cursor(document);
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(text);
    }

    static int mode(const QString &path)
    {
        struct stat st;
        return ::stat(QFile::encodeName(path).constData(), &st) == 0 ? int(st.st_mode & 07777) : -1;
    }

    static ino_t inode(const QString &path)
    {
        struct stat st;
        return ::stat(QFile::encodeName(path).constData(), &st) == 0 ? st.st_ino : 0;
    }
};

QTEST_MAIN(PlainTests)
#include "plain_tests.moc"
