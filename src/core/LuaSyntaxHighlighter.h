#pragma once
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QVector>

/// Podświetlanie składni Lua dla QPlainTextEdit / QTextEdit.
class LuaSyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit LuaSyntaxHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat    format;
    };
    QVector<Rule> m_rules;

    QTextCharFormat m_blockCommentFormat;

    void addRule(const QString &pattern, const QTextCharFormat &fmt);
};
