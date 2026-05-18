#pragma once
#include <QWidget>
#include "LuaScriptEngine.h"

class QPlainTextEdit;
class QPushButton;
class QLabel;
class QShortcut;
class LuaSyntaxHighlighter;

/// Wbudowany edytor skryptów Lua z live-reload (bez pliku na dysku).
/// Ctrl+Enter / F5 → ładuje bieżący kod do LuaScriptEngine.
class CanScriptWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanScriptWidget(LuaScriptEngine *engine, QWidget *parent = nullptr);

private slots:
    void applyScript();
    void stopScript();
    void clearLog();
    void loadFromFile();
    void saveToFile();
    void onLogMessage(const QString &msg);
    void onErrorOccurred(const QString &err);

private:
    void setupUi();
    void appendLog(const QString &msg, const QColor &color = {});
    void setStatus(const QString &text, bool ok);

    LuaScriptEngine      *m_engine;
    LuaSyntaxHighlighter *m_highlighter = nullptr;

    QPlainTextEdit *m_editor    = nullptr;
    QPlainTextEdit *m_logPanel  = nullptr;
    QPushButton    *m_applyBtn  = nullptr;
    QPushButton    *m_stopBtn   = nullptr;
    QLabel         *m_statusLbl = nullptr;
};
