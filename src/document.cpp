#include "document.h"

#include "highlighter.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMetaEnum>
#include <QQuickTextDocument>
#include <QSaveFile>
#include <QStringDecoder>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>

#include <pwd.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace {
const QByteArray Bom("\xEF\xBB\xBF");

bool readFile(const QString &path, QByteArray *bytes) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    *bytes = file.readAll();
    return file.error() == QFile::NoError;
}

QString ownerName(uid_t uid) {
    const passwd *entry = getpwuid(uid);
    return entry ? QString::fromLocal8Bit(entry->pw_name) : QString::number(uid);
}
}

Document::Document(QObject *parent) : QObject(parent) {
    // Editors and Omarchy's scripts replace files rather than writing into
    // them, which drops the watch; the debounce collapses the burst and
    // diskChanged() re-arms it.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(100);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Document::diskChanged);
}

// Bytes to editable text, or not ok when saving the text again couldn't give
// back the same file: invalid UTF-8, NUL bytes, carriage returns that aren't
// part of CRLF, or U+2029, which QTextDocument turns into a line break.
Document::Decoded Document::decode(const QByteArray &input) {
    Decoded result;
    QByteArray bytes = input;
    if (bytes.startsWith(Bom)) {
        result.bom = true;
        bytes.remove(0, Bom.size());
    }
    if (bytes.contains('\0'))
        return result;

    // ConvertInitialBom keeps a second BOM as text instead of eating it.
    QStringDecoder decoder(QStringConverter::Utf8, QStringConverter::Flag::ConvertInitialBom);
    QString text = decoder.decode(bytes);
    if (decoder.hasError() || text.contains(QChar::ParagraphSeparator))
        return result;

    int crlf = 0, lf = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) == '\r') {
            if (i + 1 >= text.size() || text.at(i + 1) != '\n')
                return result;
            ++crlf;
            ++i;
        } else if (text.at(i) == '\n') {
            ++lf;
        }
    }
    result.crlf = crlf > lf;
    result.mixed = crlf > 0 && lf > 0;
    if (crlf > 0)
        text.replace("\r\n", "\n");
    result.text = text;
    result.indentUnit = detectIndent(text);
    result.ok = true;
    return result;
}

// Block by block rather than toPlainText(), which turns non-breaking spaces
// into ordinary ones. The last block has no line ending after it, so a final
// newline is the empty last line the user can see.
QByteArray Document::encode(const QTextDocument *document, bool bom, bool crlf) {
    QString text;
    const QString ending = crlf ? "\r\n" : "\n";
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        text += block.text();
        if (block.next().isValid())
            text += ending;
    }
    return (bom ? Bom : QByteArray()) + text.toUtf8();
}

// Tabs when most indented lines start with one; otherwise the most common
// step between one line's indentation and the next deeper one.
QString Document::detectIndent(const QString &text) {
    int tabs = 0, spaced = 0, previous = 0;
    QHash<int, int> steps;
    for (const QStringView line : QStringView(text).split(u'\n')) {
        if (line.trimmed().isEmpty())
            continue;
        if (line.startsWith(u'\t')) {
            ++tabs;
            continue;
        }
        int width = 0;
        while (width < line.size() && line.at(width) == u' ')
            ++width;
        if (width > 0)
            ++spaced;
        if (width > previous && width - previous <= 8)
            ++steps[width - previous];
        previous = width;
    }
    if (tabs > spaced)
        return "\t";
    int best = 0, bestCount = 0;
    for (auto it = steps.constBegin(); it != steps.constEnd(); ++it)
        if (it.value() > bestCount || (it.value() == bestCount && it.key() < best))
            best = it.key(), bestCount = it.value();
    return best > 0 ? QString(best, ' ') : QString();
}

QByteArray Document::hash(const QByteArray &bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

QString Document::displayPath() const {
    if (m_path.isEmpty())
        return {};
    const QString home = QDir::homePath();
    return m_path.startsWith(home + "/") ? "~" + m_path.mid(home.size()) : m_path;
}

QString Document::fileName() const {
    return m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).fileName();
}

QString Document::commentPrefix() const {
    return Highlighter::commentPrefix(m_language);
}

bool Document::modified() const {
    return m_document && m_document->isModified();
}

QString Document::problem() const {
    return QString::fromLatin1(QMetaEnum::fromType<Problem>().valueToKey(int(m_problem)));
}

void Document::attach(QQuickTextDocument *document) {
    if (document)
        attachDocument(document->textDocument());
}

void Document::attachDocument(QTextDocument *document) {
    if (!document || m_document == document)
        return;
    m_document = document;
    m_highlighter = new Highlighter(m_document, m_theme);
    connect(m_document, &QTextDocument::modificationChanged, this, &Document::modifiedChanged);
}

bool Document::openUrl(const QUrl &url) {
    return url.isLocalFile() && open(url.toLocalFile());
}

bool Document::open(const QString &path) {
    // Absolute, but not canonical: the user opened the link, not its target.
    m_path = QFileInfo(path).absoluteFilePath();
    return load(m_path, false);
}

bool Document::load(const QString &path, bool replaceUndoably) {
    if (!m_document)
        return false;
    auto setText = [&](const QString &text) {
        if (replaceUndoably) {
            QTextCursor cursor(m_document);
            cursor.beginEditBlock();
            cursor.select(QTextCursor::Document);
            cursor.insertText(text);
            cursor.endEditBlock();
        } else {
            m_document->setPlainText(text);
        }
        m_document->setModified(false);
    };
    auto refuse = [&](Problem problem) {
        m_editable = false;
        m_target.clear();
        m_diskHash.clear();
        setText({});
        setLanguageFor(path, {});
        setProblem(problem, displayPath());
        watch();
        emit fileChanged();
        return false;
    };

    const QFileInfo info(path);
    if (!info.exists()) {
        // A new file, created on first save. A dangling link saves to where
        // it points.
        const QString target = info.isSymLink() ? info.symLinkTarget() : path;
        if (!QFileInfo(QFileInfo(target).absolutePath()).isDir())
            return refuse(Problem::NotFound);
        m_target = target;
        m_diskHash.clear();
        m_bom = m_crlf = false;
        m_editable = true;
        setText({});
        setLanguageFor(path, {});
        m_indentUnit = Highlighter::defaultIndent(m_language);
        setProblem(Problem::None);
        watch();
        emit fileChanged();
        return true;
    }
    if (!info.isFile())
        return refuse(Problem::NotRegular);

    QByteArray bytes;
    if (!readFile(info.canonicalFilePath(), &bytes))
        return refuse(Problem::NotReadable);
    const Decoded decoded = decode(bytes);
    if (!decoded.ok)
        return refuse(Problem::NotText);

    if (replaceUndoably)
        emit aboutToReload();
    m_target = info.canonicalFilePath();
    m_diskHash = hash(bytes);
    m_bom = decoded.bom;
    m_crlf = decoded.crlf;
    m_editable = true;
    setText(decoded.text);
    setLanguageFor(path, decoded.text.left(decoded.text.indexOf('\n')));
    m_indentUnit = decoded.indentUnit.isEmpty() ? Highlighter::defaultIndent(m_language) : decoded.indentUnit;
    setProblem(decoded.mixed ? Problem::MixedEndings : Problem::None);
    watch();
    emit fileChanged();
    if (replaceUndoably)
        emit reloaded();
    return true;
}

void Document::setLanguageFor(const QString &path, const QString &firstLine) {
    m_language = Highlighter::languageFor(path, firstLine);
    if (m_highlighter)
        m_highlighter->setLanguage(m_language);
}

// Where a save would go right now: the link's current target, or the path.
QString Document::currentTarget() const {
    const QFileInfo info(m_path);
    if (info.exists())
        return info.canonicalFilePath();
    return info.isSymLink() ? info.symLinkTarget() : m_path;
}

bool Document::save() {
    if (m_path.isEmpty() || !m_editable || !m_document)
        return false;

    const QString target = currentTarget();
    if (!m_target.isEmpty() && target != m_target) {
        setProblem(Problem::Retargeted, target);
        return false;
    }

    // Never save over a version of the file the user hasn't seen. The
    // watcher usually says so earlier; this check is the one that counts.
    QByteArray bytes;
    const bool exists = QFileInfo::exists(target);
    if (m_diskHash.isEmpty() && exists) {
        setProblem(Problem::Conflict, displayPath());
        return false;
    }
    if (!m_diskHash.isEmpty()) {
        if (!exists) {
            setProblem(Problem::DeletedOnDisk, displayPath());
            return false;
        }
        if (readFile(target, &bytes) && hash(bytes) != m_diskHash) {
            setProblem(Problem::Conflict, displayPath());
            return false;
        }
    }
    return write(false);
}

// Accepts the file as it is on disk now (or its absence) as the version the
// user has seen, then saves over it. Also how a retargeted link is accepted.
bool Document::overwrite() {
    m_target = currentTarget();
    QByteArray bytes;
    m_diskHash = QFileInfo::exists(m_target) && readFile(m_target, &bytes) ? hash(bytes) : QByteArray();
    setProblem(Problem::None);
    return save();
}

bool Document::overwriteInPlace() {
    return write(true);
}

void Document::keepMine() {
    QByteArray bytes;
    m_diskHash = QFileInfo::exists(m_target) && readFile(m_target, &bytes) ? hash(bytes) : QByteArray();
    setProblem(Problem::None);
}

void Document::reload() {
    if (!m_path.isEmpty())
        load(m_path, true);
}

void Document::dismissProblem() {
    setProblem(Problem::None);
}

bool Document::saveAsUrl(const QUrl &url) {
    if (!url.isLocalFile() || !m_document)
        return false;
    // The portal already asked about replacing an existing file, so what's
    // there now is the version the user agreed to replace.
    m_path = QFileInfo(url.toLocalFile()).absoluteFilePath();
    m_target = currentTarget();
    QByteArray bytes;
    m_diskHash = QFileInfo::exists(m_target) && readFile(m_target, &bytes) ? hash(bytes) : QByteArray();
    m_editable = true;
    setLanguageFor(m_path, m_document->firstBlock().text());
    emit fileChanged();
    return write(false);
}

// Why replacing the file by rename would lose something, or empty when it's
// safe. The rename makes a new inode owned by the user with ordinary
// permission bits, so anything beyond those is a reason to ask.
QString Document::unsafeForRename(const QString &target) const {
    const QByteArray path = QFile::encodeName(target);
    struct stat st;
    if (::stat(path.constData(), &st) != 0)
        return {};
    if (::access(QFile::encodeName(QFileInfo(target).absolutePath()).constData(), W_OK) != 0)
        return tr("its folder isn't writable");
    if (st.st_nlink > 1)
        return tr("it has other hard links");
    if (st.st_uid != ::geteuid())
        return tr("it belongs to %1").arg(ownerName(st.st_uid));
    if (st.st_mode & (S_ISUID | S_ISGID | S_ISVTX))
        return tr("it has special permission bits");
    if (::listxattr(path.constData(), nullptr, 0) > 0)
        return tr("it has extended attributes or ACLs");
    return {};
}

bool Document::write(bool inPlace) {
    if (!m_document || m_target.isEmpty())
        return false;
    const QString target = m_target;
    const QByteArray targetPath = QFile::encodeName(target);
    const bool exists = QFileInfo::exists(target);

    if (exists ? ::access(targetPath.constData(), W_OK) != 0
               : ::access(QFile::encodeName(QFileInfo(target).absolutePath()).constData(), W_OK) != 0) {
        setProblem(Problem::NotWritable, target);
        return false;
    }
    if (!inPlace) {
        const QString reason = unsafeForRename(target);
        if (!reason.isEmpty()) {
            setProblem(Problem::NeedsInPlace, reason);
            return false;
        }
    }

    const QByteArray bytes = encode(m_document, m_bom, m_crlf);
    if (inPlace) {
        struct stat before;
        const bool known = ::stat(targetPath.constData(), &before) == 0;
        QFile file(target);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size()
            || !file.flush() || ::fsync(file.handle()) != 0) {
            setProblem(Problem::SaveFailed, file.errorString());
            return false;
        }
        // The kernel clears setuid and setgid when a non-root user writes a
        // file; put them back, which the owner is allowed to do.
        if (known)
            ::fchmod(file.handle(), before.st_mode & 07777);
    } else {
        QSaveFile file(target);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            setProblem(Problem::SaveFailed, file.errorString());
            return false;
        }
    }

    m_diskHash = hash(bytes);
    m_document->setModified(false);
    setProblem(Problem::None);
    watch();
    return true;
}

void Document::setProblem(Problem problem, const QString &detail) {
    if (problem == m_problem && detail == m_problemDetail)
        return;
    m_problem = problem;
    m_problemDetail = detail;
    emit problemChanged();
}

void Document::watch() {
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    if (m_path.isEmpty())
        return;
    QStringList paths{m_path, QFileInfo(m_path).absolutePath()};
    if (!m_target.isEmpty())
        paths << m_target << QFileInfo(m_target).absolutePath();
    paths.removeDuplicates();
    for (const QString &path : paths)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);
}

// News from the disk. Only content counts: a touch or Plain's own save
// leaves the hash as it was and changes nothing.
void Document::diskChanged() {
    watch();
    if (m_path.isEmpty() || !m_editable)
        return;
    const QString target = currentTarget();
    if (!QFileInfo::exists(target)) {
        if (!m_diskHash.isEmpty())
            setProblem(Problem::DeletedOnDisk, displayPath());
        return;
    }
    QByteArray bytes;
    if (!readFile(target, &bytes))
        return;
    if (target == m_target && hash(bytes) == m_diskHash) {
        if (m_problem == Problem::DeletedOnDisk || m_problem == Problem::ChangedOnDisk)
            setProblem(Problem::None);
        return;
    }
    if (m_diskHash.isEmpty() && !modified() && m_document->isEmpty()) {
        // A new, untouched file someone else just created: show it.
        load(m_path, true);
        return;
    }
    if (!modified() && target == m_target)
        load(m_path, true);
    else
        setProblem(Problem::ChangedOnDisk, displayPath());
}
