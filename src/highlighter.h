#pragma once

#include <QObject>
#include <QPointer>
#include <QTextCharFormat>

#include "theme.h"

class QTextDocument;

// Colors for config files: comments, strings, numbers, keys, keywords and
// sections, constants. One small scanner shared by every language, told per
// language which comment, string and key forms exist, so strings with
// escapes, comment markers inside strings and multi-line strings and
// comments come out right where regular expressions would overlap.
class Highlighter : public QObject {
    Q_OBJECT

public:
    enum Token { Comment, String, Number, Key, Keyword, Constant, TokenCount };

    Highlighter(QTextDocument *document, Theme *theme);

    void setLanguage(const QString &language);
    QString language() const { return m_language; }
    void rehighlight();

    // By file name, then extension, then a shebang on the first line.
    static QString languageFor(const QString &path, const QString &firstLine);
    // What Ctrl+/ puts in front of a line; empty where the language has none
    // (JSON), and "/*" for CSS, which closes it with " */".
    static QString commentPrefix(const QString &language);
    static QString defaultIndent(const QString &language);

    // The token at each position of the lines in a language, for tests: one
    // character per position, '.' for none, otherwise "csnkwx"[token].
    static QString tokens(const QString &language, const QStringList &lines);

private:
    void highlight(int from, int to);
    void updateFormats();

    QTextDocument *m_document;
    QPointer<Theme> m_theme;
    QString m_language = "text";
    QTextCharFormat m_formats[TokenCount];
    bool m_marking = false;
};
