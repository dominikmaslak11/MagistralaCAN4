#include "CanScriptWidget.h"
#include "LuaSyntaxHighlighter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QShortcut>
#include <QKeySequence>
#include <QFont>
#include <QColor>
#include <QScrollBar>
#include <QMessageBox>

static const char *DEFAULT_SCRIPT = R"(-- Skrypt Lua — MagistralaCAN4
-- API dostępne:
--   sendFrame(id, {b0, b1, ...})  — wyślij ramkę CAN
--   log(msg)                       — wypisz do panelu logów
--   getTick()                      — ms od uruchomienia skryptu
--
-- Wymagane: funkcja onFrame(id, data, timestamp)
-- Opcjonalne: onAlert(rule, id, desc, data, timestamp)

local counter = 0

function onFrame(id, data, timestamp)
    -- Przykład: loguj co 100-tą ramkę o ID 0x7E8
    if id == 0x7E8 then
        counter = counter + 1
        if counter % 100 == 0 then
            log(string.format("[%d ms] ID=0x%03X DLC=%d B0=0x%02X cnt=%d",
                getTick(), id, #data, data[1] or 0, counter))
        end
    end
end

function onAlert(rule, id, desc, data, timestamp)
    log(string.format("ALERT: %s ID=0x%03X %s", rule, id, desc))
end
)";

CanScriptWidget::CanScriptWidget(LuaScriptEngine *engine, QWidget *parent)
    : QWidget(parent), m_engine(engine)
{
    setupUi();

    connect(m_engine, &LuaScriptEngine::logMessage,    this, &CanScriptWidget::onLogMessage);
    connect(m_engine, &LuaScriptEngine::errorOccurred, this, &CanScriptWidget::onErrorOccurred);
}

void CanScriptWidget::setupUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // ── Pasek narzędzi ─────────────────────────────────────────────────────────
    auto *toolbar = new QHBoxLayout;
    m_applyBtn = new QPushButton("▶ Zastosuj  (Ctrl+Enter)");
    m_stopBtn  = new QPushButton("■ Zatrzymaj");
    auto *clearBtn = new QPushButton("Wyczyść logi");
    auto *loadBtn  = new QPushButton("Wczytaj plik...");
    auto *saveBtn  = new QPushButton("Zapisz plik...");
    m_statusLbl = new QLabel("— brak aktywnego skryptu —");

    m_stopBtn->setEnabled(false);

    toolbar->addWidget(m_applyBtn);
    toolbar->addWidget(m_stopBtn);
    toolbar->addWidget(clearBtn);
    toolbar->addWidget(loadBtn);
    toolbar->addWidget(saveBtn);
    toolbar->addStretch();
    toolbar->addWidget(m_statusLbl);
    root->addLayout(toolbar);

    // ── Splitter: edytor (góra) + logi (dół) ───────────────────────────────────
    auto *splitter = new QSplitter(Qt::Vertical);

    // Edytor
    m_editor = new QPlainTextEdit;
    m_editor->setPlainText(DEFAULT_SCRIPT);
    QFont mono("Consolas");
    if (!mono.exactMatch()) mono.setFamily("Courier New");
    mono.setPointSize(10);
    m_editor->setFont(mono);
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_editor->setTabStopDistance(24);  // 4 spacje

    m_highlighter = new LuaSyntaxHighlighter(m_editor->document());

    // Panel logów (read-only)
    m_logPanel = new QPlainTextEdit;
    m_logPanel->setReadOnly(true);
    m_logPanel->setFont(mono);
    m_logPanel->setMaximumBlockCount(2000);
    m_logPanel->setPlaceholderText("Logi skryptu pojawią się tutaj...");

    splitter->addWidget(m_editor);
    splitter->addWidget(m_logPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    // ── Połączenia ─────────────────────────────────────────────────────────────
    connect(m_applyBtn, &QPushButton::clicked, this, &CanScriptWidget::applyScript);
    connect(m_stopBtn,  &QPushButton::clicked, this, &CanScriptWidget::stopScript);
    connect(clearBtn,   &QPushButton::clicked, this, &CanScriptWidget::clearLog);
    connect(loadBtn,    &QPushButton::clicked, this, &CanScriptWidget::loadFromFile);
    connect(saveBtn,    &QPushButton::clicked, this, &CanScriptWidget::saveToFile);

    // Skróty klawiszowe
    auto *sc1 = new QShortcut(QKeySequence("Ctrl+Return"), this);
    auto *sc2 = new QShortcut(QKeySequence(Qt::Key_F5),    this);
    connect(sc1, &QShortcut::activated, this, &CanScriptWidget::applyScript);
    connect(sc2, &QShortcut::activated, this, &CanScriptWidget::applyScript);
}

void CanScriptWidget::applyScript() {
    QString code = m_editor->toPlainText();
    appendLog(QString("[%1] Ładowanie skryptu...")
              .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));

    bool ok = m_engine->loadScriptFromString(code, "<editor>");
    if (ok) {
        setStatus("Skrypt aktywny", true);
        m_stopBtn->setEnabled(true);
    } else {
        setStatus("Błąd składni — patrz logi", false);
        m_stopBtn->setEnabled(false);
    }
}

void CanScriptWidget::stopScript() {
    m_engine->unloadScript();
    setStatus("— skrypt zatrzymany —", false);
    m_stopBtn->setEnabled(false);
    appendLog(QString("[%1] Skrypt zatrzymany.")
              .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
}

void CanScriptWidget::clearLog() {
    m_logPanel->clear();
}

void CanScriptWidget::loadFromFile() {
    QString path = QFileDialog::getOpenFileName(
        this, "Wczytaj skrypt Lua", "", "Skrypty Lua (*.lua);;Wszystkie pliki (*)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Błąd", "Nie można otworzyć pliku: " + path);
        return;
    }
    m_editor->setPlainText(QString::fromUtf8(f.readAll()));
    f.close();
    appendLog(QString("[wczytano] %1").arg(path));
}

void CanScriptWidget::saveToFile() {
    QString path = QFileDialog::getSaveFileName(
        this, "Zapisz skrypt Lua", "skrypt.lua", "Skrypty Lua (*.lua);;Wszystkie pliki (*)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Błąd", "Nie można zapisać pliku: " + path);
        return;
    }
    QTextStream(&f) << m_editor->toPlainText();
    f.close();
    appendLog(QString("[zapisano] %1").arg(path));
}

void CanScriptWidget::onLogMessage(const QString &msg) {
    appendLog(msg, QColor("#87CEEB"));  // niebieskawa
}

void CanScriptWidget::onErrorOccurred(const QString &err) {
    appendLog("[BŁĄD] " + err, QColor("#FF6B6B"));
    setStatus("Błąd: " + err.left(60), false);
}

void CanScriptWidget::appendLog(const QString &msg, const QColor &color) {
    if (color.isValid()) {
        m_logPanel->appendHtml(
            QString("<span style='color:%1'>%2</span>")
            .arg(color.name())
            .arg(msg.toHtmlEscaped()));
    } else {
        m_logPanel->appendPlainText(msg);
    }
    m_logPanel->verticalScrollBar()->setValue(
        m_logPanel->verticalScrollBar()->maximum());
}

void CanScriptWidget::setStatus(const QString &text, bool ok) {
    m_statusLbl->setText(text);
    m_statusLbl->setStyleSheet(ok
        ? "color: #00ffaa; font-weight: bold;"
        : "color: #ff6b6b; font-weight: bold;");
}
