#include "LuaSyntaxHighlighter.h"
#include <QColor>

LuaSyntaxHighlighter::LuaSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    // ── Słowa kluczowe Lua ────────────────────────────────────────────────────
    QTextCharFormat kwFmt;
    kwFmt.setForeground(QColor("#569CD6"));
    kwFmt.setFontWeight(QFont::Bold);
    const QStringList keywords = {
        "and", "break", "do", "else", "elseif", "end", "false", "for",
        "function", "goto", "if", "in", "local", "nil", "not", "or",
        "repeat", "return", "then", "true", "until", "while"
    };
    for (const QString &kw : keywords)
        addRule(QString("\\b%1\\b").arg(kw), kwFmt);

    // ── Funkcje API MagistralaCAN4 ────────────────────────────────────────────
    QTextCharFormat apiFmt;
    apiFmt.setForeground(QColor("#DCDCAA"));
    const QStringList api = { "sendFrame", "log", "getTick", "onFrame", "onAlert" };
    for (const QString &fn : api)
        addRule(QString("\\b%1\\b").arg(fn), apiFmt);

    // ── Wbudowane Lua ─────────────────────────────────────────────────────────
    QTextCharFormat builtinFmt;
    builtinFmt.setForeground(QColor("#4EC9B0"));
    const QStringList builtins = {
        "print", "tostring", "tonumber", "type", "pairs", "ipairs",
        "next", "select", "unpack", "rawget", "rawset", "rawequal",
        "setmetatable", "getmetatable", "require", "pcall", "xpcall",
        "error", "assert", "math", "string", "table", "io", "os"
    };
    for (const QString &b : builtins)
        addRule(QString("\\b%1\\b").arg(b), builtinFmt);

    // ── Liczby ────────────────────────────────────────────────────────────────
    QTextCharFormat numFmt;
    numFmt.setForeground(QColor("#B5CEA8"));
    addRule(R"(\b0[xX][0-9a-fA-F]+\b)", numFmt);  // hex
    addRule(R"(\b\d+\.?\d*([eE][+-]?\d+)?\b)", numFmt);

    // ── Łańcuchy znakowe ──────────────────────────────────────────────────────
    QTextCharFormat strFmt;
    strFmt.setForeground(QColor("#CE9178"));
    addRule(R"("(?:[^"\\]|\\.)*")", strFmt);
    addRule(R"('(?:[^'\\]|\\.)*')", strFmt);

    // ── Komentarze liniowe (--) — przed blokowym ──────────────────────────────
    QTextCharFormat commentFmt;
    commentFmt.setForeground(QColor("#6A9955"));
    commentFmt.setFontItalic(true);
    addRule(R"(--(?!\[)[^\n]*)", commentFmt);  // -- ale nie --[[ (blokowy)

    // ── Komentarze blokowe --[[ ]] ────────────────────────────────────────────
    m_blockCommentFormat = commentFmt;
}

void LuaSyntaxHighlighter::addRule(const QString &pattern, const QTextCharFormat &fmt) {
    Rule r;
    r.pattern = QRegularExpression(pattern);
    r.format  = fmt;
    m_rules.append(r);
}

void LuaSyntaxHighlighter::highlightBlock(const QString &text) {
    // ── Komentarz blokowy (stan: 1 = wewnątrz --[[ )
    setCurrentBlockState(0);

    int start = 0;
    if (previousBlockState() == 1) {
        int end = static_cast<int>(text.indexOf("]]"));
        if (end == -1) {
            setFormat(0, static_cast<int>(text.length()), m_blockCommentFormat);
            setCurrentBlockState(1);
            return;
        }
        setFormat(0, end + 2, m_blockCommentFormat);
        start = end + 2;
    }

    // Szukaj nowych bloków --[[
    while (true) {
        int pos = static_cast<int>(text.indexOf("--[[", start));
        if (pos == -1) break;
        int end = static_cast<int>(text.indexOf("]]", pos + 4));
        if (end == -1) {
            setFormat(pos, static_cast<int>(text.length()) - pos, m_blockCommentFormat);
            setCurrentBlockState(1);
            return;
        }
        setFormat(pos, end + 2 - pos, m_blockCommentFormat);
        start = end + 2;
    }

    // ── Reguły liniowe ───────────────────────────────────────────────────────
    for (const Rule &rule : m_rules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            setFormat(static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedLength()), rule.format);
        }
    }
}
