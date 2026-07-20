#include "CanBusHealthWidget.h"
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QGroupBox>
#include <QDateTime>
#include <QLabel>
#include <algorithm>

namespace {

const char *kSS =
    "QTabWidget::pane { border:1px solid #2a2a3c; background:#0a0e17; }"
    "QTabBar::tab { background:#1a1a2e; color:#c0c0c0; padding:5px 12px; "
    "  font-family:Consolas; font-size:11px; border:1px solid #2a2a3c; }"
    "QTabBar::tab:selected { background:#2a1a3e; color:#ff66cc; font-weight:bold; }"
    "QTableWidget { background:#0a0e17; color:#c0c0c0; gridline-color:#2a2a3c; "
    "  font-family:Consolas; font-size:11px; selection-background-color:#1a2a3a; }"
    "QHeaderView::section { background:#1a1a2e; color:#ff66cc; border:1px solid #2a2a3c; "
    "  padding:4px; font-weight:bold; font-family:Consolas; }"
    "QPushButton { background:#1a1a2e; color:#00ffaa; border:1px solid #e94560; "
    "  border-radius:4px; padding:4px 10px; font-family:Consolas; }"
    "QPushButton:hover { background:#2a2a3e; }"
    "QLineEdit,QSpinBox { background:#0a0e17; color:#00ffaa; border:1px solid #e94560; "
    "  border-radius:3px; padding:3px 6px; font-family:Consolas; }"
    "QCheckBox { color:#c0c0c0; font-family:Consolas; }"
    "QGroupBox { border:1px solid #2a2a3c; color:#ff66cc; margin-top:6px; "
    "  font-family:Consolas; font-weight:bold; }"
    "QGroupBox::title { subcontrol-origin:margin; left:8px; }"
    "QLabel { color:#c0c0c0; font-family:Consolas; }";

const char *kErrorNames[] = {
    "Tx Timeout", "Lost Arbitration", "Controller", "Protocol",
    "Transceiver", "ACK", "BUS-OFF", "Bus Error", "Restarted", "Unknown"
};

} // namespace

QString CanBusHealthWidget::errorClassName(CanBusErrorAnalyzer::ErrorClass cls) {
    auto idx = static_cast<int>(cls);
    if (idx >= 0 && idx < 10) return kErrorNames[idx];
    return "Unknown";
}

// ── Constructor ──────────────────────────────────────────────────────────────

CanBusHealthWidget::CanBusHealthWidget(QWidget *parent) : QWidget(parent) {
    setStyleSheet(kSS);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    auto *title = new QLabel("Stan zdrowia magistrali CAN — Monitor błędów + Walidator liczników");
    title->setStyleSheet("color:#ff66cc; font-weight:bold; font-size:13px; font-family:Consolas;");
    lay->addWidget(title);

    auto *tabs = new QTabWidget;
    tabs->addTab(makeErrorTab(),   "Błędy magistrali");
    tabs->addTab(makeCounterTab(), "Walidator liczników");
    lay->addWidget(tabs, 1);

    auto *resetBtn = new QPushButton("Reset wszystkiego");
    connect(resetBtn, &QPushButton::clicked, this, &CanBusHealthWidget::resetAll);
    lay->addWidget(resetBtn);

    auto *timer = new QTimer(this);
    timer->setInterval(500);
    connect(timer, &QTimer::timeout, this, &CanBusHealthWidget::refresh);
    timer->start();
}

// ── Tab builders ─────────────────────────────────────────────────────────────

QWidget *CanBusHealthWidget::makeErrorTab() {
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(6);

    // BUS-OFF alert
    m_busOffAlert = new QLabel(" !! BUS-OFF DETECTED !! ");
    m_busOffAlert->setStyleSheet(
        "color:#ff2222; font-weight:bold; font-size:14px; font-family:Consolas; "
        "background:#2e0000; border:2px solid #ff2222; padding:4px 12px; border-radius:4px;");
    m_busOffAlert->setAlignment(Qt::AlignCenter);
    m_busOffAlert->setVisible(false);
    lay->addWidget(m_busOffAlert);

    // Summary row: total + rate
    auto *summaryRow = new QHBoxLayout;
    m_totalLabel = new QLabel("Błędów łącznie: 0");
    m_totalLabel->setStyleSheet("color:#ffaa00; font-weight:bold; font-family:Consolas;");
    summaryRow->addWidget(m_totalLabel);
    summaryRow->addStretch();
    m_rateLabel = new QLabel("Częst. (5s): 0.00 err/s");
    m_rateLabel->setStyleSheet("color:#ffaa00; font-weight:bold; font-family:Consolas;");
    summaryRow->addWidget(m_rateLabel);
    lay->addLayout(summaryRow);

    // Error class counters grid
    auto *grp = new QGroupBox("Klasy błędów");
    auto *grid = new QGridLayout(grp);
    grid->setHorizontalSpacing(20);
    grid->setVerticalSpacing(4);
    for (int i = 0; i < 10; ++i) {
        int row = i / 5, col = i % 5;
        auto *nameLabel = new QLabel(QString(kErrorNames[i]) + ":");
        nameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_countLabels[i] = new QLabel("0");
        m_countLabels[i]->setStyleSheet("color:#00ffaa; font-weight:bold;");
        m_countLabels[i]->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        grid->addWidget(nameLabel,        row, col * 2);
        grid->addWidget(m_countLabels[i], row, col * 2 + 1);
    }
    lay->addWidget(grp);

    // Event log table
    auto *evtLabel = new QLabel("Ostatnie zdarzenia błędów:");
    lay->addWidget(evtLabel);
    m_eventTable = new QTableWidget(0, 4);
    m_eventTable->setHorizontalHeaderLabels({"Czas (ms)", "Klasa", "Raw ID (hex)", "Opis"});
    m_eventTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_eventTable->horizontalHeader()->setDefaultSectionSize(110);
    m_eventTable->verticalHeader()->hide();
    m_eventTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_eventTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_eventTable->setMaximumHeight(200);
    lay->addWidget(m_eventTable, 1);

    return w;
}

QWidget *CanBusHealthWidget::makeCounterTab() {
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(6);

    // Input row for adding a new rule
    auto *grp = new QGroupBox("Dodaj regułę licznika AUTOSAR");
    auto *inputRow = new QHBoxLayout(grp);

    inputRow->addWidget(new QLabel("CAN ID (hex):"));
    m_ctIdEdit = new QLineEdit;
    m_ctIdEdit->setPlaceholderText("0x1A0");
    m_ctIdEdit->setFixedWidth(90);
    inputRow->addWidget(m_ctIdEdit);

    inputRow->addWidget(new QLabel("Bajt:"));
    m_ctByteSpin = new QSpinBox;
    m_ctByteSpin->setRange(0, 7);
    m_ctByteSpin->setFixedWidth(55);
    inputRow->addWidget(m_ctByteSpin);

    m_ctNibbleChk = new QCheckBox("Górny nibble");
    inputRow->addWidget(m_ctNibbleChk);

    inputRow->addWidget(new QLabel("Modulus:"));
    m_ctModulusSpin = new QSpinBox;
    m_ctModulusSpin->setRange(2, 256);
    m_ctModulusSpin->setValue(16);
    m_ctModulusSpin->setFixedWidth(60);
    inputRow->addWidget(m_ctModulusSpin);

    auto *addBtn = new QPushButton("+ Dodaj");
    connect(addBtn, &QPushButton::clicked, this, &CanBusHealthWidget::addCounterRule);
    inputRow->addWidget(addBtn);

    auto *removeBtn = new QPushButton("Usuń zaznaczoną");
    connect(removeBtn, &QPushButton::clicked, this, &CanBusHealthWidget::removeSelectedRule);
    inputRow->addWidget(removeBtn);

    inputRow->addStretch();
    lay->addWidget(grp);

    // Rules + stats table
    auto *tableLabel = new QLabel("Aktywne reguły i statystyki:");
    lay->addWidget(tableLabel);

    m_rulesTable = new QTableWidget(0, 8);
    m_rulesTable->setHorizontalHeaderLabels(
        {"CAN ID", "Bajt", "Nibble", "Modulus", "OK", "Błędy", "Razem", "% Błędów"});
    m_rulesTable->horizontalHeader()->setDefaultSectionSize(80);
    m_rulesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_rulesTable->verticalHeader()->hide();
    m_rulesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_rulesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    lay->addWidget(m_rulesTable, 1);

    return w;
}

// ── Data ingestion ───────────────────────────────────────────────────────────

void CanBusHealthWidget::processFrame(const CanFrame &frame) {
    m_errAnalyzer.processFrame(frame);
    m_counterValidator.update(frame);
}

// ── Timer refresh ────────────────────────────────────────────────────────────

void CanBusHealthWidget::refresh() {
    updateErrorCounts();
    updateCounterTable();

    // Append any new events since last check
    const auto &evts = m_errAnalyzer.events();
    while (m_lastEventIdx < evts.size())
        appendErrorEvent(evts[m_lastEventIdx++]);
}

// ── Error tab updates ─────────────────────────────────────────────────────────

void CanBusHealthWidget::updateErrorCounts() {
    m_busOffAlert->setVisible(m_errAnalyzer.isBusOff());

    for (int i = 0; i < 10; ++i) {
        auto cls = static_cast<CanBusErrorAnalyzer::ErrorClass>(i);
        int cnt = m_errAnalyzer.errorCount(cls);
        m_countLabels[i]->setText(QString::number(cnt));
        m_countLabels[i]->setStyleSheet(
            cnt > 0 ? "color:#ff6644; font-weight:bold;" : "color:#00ffaa; font-weight:bold;");
    }

    m_totalLabel->setText(QString("Błędów łącznie: %1").arg(m_errAnalyzer.totalErrors()));

    const double rate = m_errAnalyzer.errorRate(5'000'000ULL);
    m_rateLabel->setText(QString("Częst. (5s): %1 err/s").arg(rate, 0, 'f', 2));
    m_rateLabel->setStyleSheet(
        rate > 10.0 ? "color:#ff2222; font-weight:bold; font-family:Consolas;"
                    : "color:#ffaa00; font-weight:bold; font-family:Consolas;");
}

void CanBusHealthWidget::appendErrorEvent(const CanBusErrorAnalyzer::ErrorEvent &ev) {
    // Limit table to 500 rows
    if (m_eventTable->rowCount() >= 500)
        m_eventTable->removeRow(0);

    const int row = m_eventTable->rowCount();
    m_eventTable->insertRow(row);

    const double ms = ev.timestampUs / 1000.0;
    const QString clsName = errorClassName(ev.cls);
    const bool isCritical = (ev.cls == CanBusErrorAnalyzer::ErrorClass::BusOff);
    const QColor bg = isCritical ? QColor("#2e0000") : QColor("#0a0e17");
    const QColor fg = isCritical ? QColor("#ff2222") : QColor("#c0c0c0");

    auto setCell = [&](int col, const QString &text) {
        auto *it = new QTableWidgetItem(text);
        it->setBackground(bg);
        it->setForeground(fg);
        it->setTextAlignment(Qt::AlignCenter);
        m_eventTable->setItem(row, col, it);
    };

    setCell(0, QString::number(ms, 'f', 1));
    setCell(1, clsName);
    setCell(2, QString("0x%1").arg(ev.rawId, 3, 16, QChar('0')).toUpper());
    setCell(3, QString("SocketCAN error: %1 (ID=0x%2)")
               .arg(clsName)
               .arg(ev.rawId, 8, 16, QChar('0')).toUpper());

    m_eventTable->scrollToBottom();
}

// ── Counter tab updates ───────────────────────────────────────────────────────

void CanBusHealthWidget::updateCounterTable() {
    if (m_rules.isEmpty()) return;

    m_rulesTable->setRowCount(m_rules.size());
    for (int i = 0; i < m_rules.size(); ++i) {
        const auto &cfg = m_rules[i];
        const auto stats = m_counterValidator.statsFor(cfg.canId);
        const double pct = (stats.totalFrames > 0)
            ? (static_cast<double>(stats.mismatchCount) / stats.totalFrames * 100.0)
            : 0.0;

        const QColor bg = (pct > 5.0) ? QColor("#2e1000")
                        : (pct > 0.0) ? QColor("#1a1800")
                                      : QColor("#0a140a");

        auto setCell = [&](int col, const QString &text) {
            auto *it = m_rulesTable->item(i, col);
            if (!it) { it = new QTableWidgetItem; m_rulesTable->setItem(i, col, it); }
            it->setText(text);
            it->setBackground(bg);
            it->setForeground(QColor("#c0c0c0"));
            it->setTextAlignment(Qt::AlignCenter);
        };

        setCell(0, QString("0x%1").arg(cfg.canId, 3, 16, QChar('0')).toUpper());
        setCell(1, QString::number(cfg.byteIndex));
        setCell(2, cfg.upperNibble ? "Tak" : "Nie");
        setCell(3, QString::number(cfg.modulus));
        setCell(4, QString::number(stats.okCount));
        setCell(5, QString::number(stats.mismatchCount));
        setCell(6, QString::number(stats.totalFrames));
        setCell(7, QString::number(pct, 'f', 2) + "%");
    }
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void CanBusHealthWidget::resetAll() {
    m_errAnalyzer.reset();
    m_counterValidator.reset();
    m_lastEventIdx = 0;
    m_eventTable->setRowCount(0);
    updateErrorCounts();
    updateCounterTable();
}

void CanBusHealthWidget::addCounterRule() {
    bool ok = false;
    const QString idText = m_ctIdEdit->text().trimmed()
                               .replace("0x", "", Qt::CaseInsensitive)
                               .replace("0X", "");
    const uint32_t canId = idText.toUInt(&ok, 16);
    if (!ok || idText.isEmpty()) return;

    // Don't add duplicate
    for (const auto &r : m_rules)
        if (r.canId == canId) return;

    CanCounterValidator::Config cfg;
    cfg.canId       = canId;
    cfg.byteIndex   = m_ctByteSpin->value();
    cfg.upperNibble = m_ctNibbleChk->isChecked();
    cfg.modulus     = static_cast<uint8_t>(m_ctModulusSpin->value());

    m_rules.append(cfg);
    m_counterValidator.addConfig(cfg);
    m_rulesTable->setRowCount(m_rules.size());
    updateCounterTable();
    m_ctIdEdit->clear();
}

void CanBusHealthWidget::removeSelectedRule() {
    const int row = m_rulesTable->currentRow();
    if (row < 0 || row >= m_rules.size()) return;

    m_counterValidator.removeConfig(m_rules[row].canId);
    m_rules.removeAt(row);
    m_rulesTable->removeRow(row);
}
