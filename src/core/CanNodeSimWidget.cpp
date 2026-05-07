#include "CanNodeSimWidget.h"
#include "CanSniffer.h"
#include "LuaScriptEngine.h"
#include "Logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>

CanNodeSimWidget::CanNodeSimWidget(CanSniffer *sniffer,
                                     LuaScriptEngine *lua,
                                     QWidget *parent)
    : QWidget(parent), m_sniffer(sniffer), m_lua(lua) {
    setupUi();
}

void CanNodeSimWidget::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);

    // ── Toolbar ──
    auto *toolbar = new QHBoxLayout;
    m_addBtn  = new QPushButton("+ Dodaj węzeł");
    m_delBtn  = new QPushButton("− Usuń");
    m_loadBtn = new QPushButton("Wczytaj konfigurację");
    m_saveBtn = new QPushButton("Zapisz konfigurację");
    toolbar->addWidget(m_addBtn);
    toolbar->addWidget(m_delBtn);
    toolbar->addSpacing(20);
    toolbar->addWidget(m_loadBtn);
    toolbar->addWidget(m_saveBtn);
    toolbar->addStretch();

    m_statusLabel = new QLabel("Symulator gotowy. 0 węzłów.");
    m_statusLabel->setStyleSheet("color: #00ffaa; font-weight: bold;");
    toolbar->addWidget(m_statusLabel);
    mainLayout->addLayout(toolbar);

    // ── Tabela węzłów ──
    m_nodeTable = new QTableWidget(0, _COUNT);
    m_nodeTable->setHorizontalHeaderLabels({
        "Aktywny", "Nazwa", "Trigger ID", "Trigger Data",
        "Response ID", "Response Data", "Delay (ms)",
        "Trafienia", "Odpowiedzi"
    });
    m_nodeTable->verticalHeader()->hide();
    m_nodeTable->horizontalHeader()->setStretchLastSection(true);
    m_nodeTable->setShowGrid(false);
    m_nodeTable->setAlternatingRowColors(false);
    m_nodeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_nodeTable->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(m_nodeTable, 1);

    // ── Połączenia ──
    connect(m_addBtn,  &QPushButton::clicked, this, &CanNodeSimWidget::addNode);
    connect(m_delBtn,  &QPushButton::clicked, this, &CanNodeSimWidget::removeNode);
    connect(m_loadBtn, &QPushButton::clicked, this, &CanNodeSimWidget::loadConfig);
    connect(m_saveBtn, &QPushButton::clicked, this, &CanNodeSimWidget::saveConfig);
    connect(m_nodeTable, &QTableWidget::cellClicked, this, &CanNodeSimWidget::onNodeSelected);
    connect(m_nodeTable, &QTableWidget::cellChanged, this, &CanNodeSimWidget::onCellChanged);

    // Odświeżanie statystyk co 500 ms
    auto *statsTimer = new QTimer(this);
    connect(statsTimer, &QTimer::timeout, this, &CanNodeSimWidget::refreshTable);
    statsTimer->start(500);

    setStyleSheet(R"(
        QPushButton { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560;
            border-radius: 4px; padding: 5px 15px; font-weight: bold; }
        QPushButton:hover { background: #e94560; color: #0a0e17; }
        QTableWidget { background-color: #0a0e17; color: #c0c0c0; gridline-color: #2a2a3c;
            selection-background-color: #e94560; selection-color: #ffffff;
            font-family: Consolas, monospace; font-size: 11px; }
        QHeaderView::section { background-color: #1a1a2e; color: #ff66cc; font-weight: bold;
            padding: 4px; border: none; border-bottom: 2px solid #e94560; }
        QLabel { color: #c0c0c0; }
    )");
}

void CanNodeSimWidget::addNode() {
    CanNodeDefinition node;
    node.name       = QString("Node_%1").arg(m_simulator.nodes().size());
    node.triggerId  = 0x100;
    node.responseId = 0x200;
    node.staticResponse = {0x01, 0x02, 0x03};
    node.enabled    = true;
    m_simulator.nodes().append(node);

    int row = m_nodeTable->rowCount();
    m_nodeTable->insertRow(row);
    populateRow(row, node);
    m_statusLabel->setText(QString("Symulator: %1 węzłów.").arg(m_simulator.nodes().size()));
}

void CanNodeSimWidget::removeNode() {
    int row = m_nodeTable->currentRow();
    if (row < 0 || row >= m_simulator.nodes().size()) return;
    m_simulator.nodes().removeAt(row);
    m_nodeTable->removeRow(row);
    m_statusLabel->setText(QString("Symulator: %1 węzłów.").arg(m_simulator.nodes().size()));
}

void CanNodeSimWidget::onNodeSelected(int row, int col) {
    Q_UNUSED(row); Q_UNUSED(col);
    // Placeholder – można rozbudować o panel edycji
}

void CanNodeSimWidget::onCellChanged(int row, int col) {
    if (row < 0 || row >= m_simulator.nodes().size()) return;
    auto &node = m_simulator.nodes()[row];
    auto *item = m_nodeTable->item(row, col);
    if (!item) return;

    bool ok;
    switch (col) {
    case COL_ENABLED:
        node.enabled = (item->checkState() == Qt::Checked);
        break;
    case COL_NAME:
        node.name = item->text().trimmed();
        break;
    case COL_TRIG_ID:
        node.triggerId = item->text().toUInt(&ok, 16);
        if (!ok) node.triggerId = 0x100;
        break;
    case COL_TRIG_DATA: {
        node.triggerData.clear();
        auto hexBytes = item->text().trimmed().split(' ', Qt::SkipEmptyParts);
        for (const auto &h : hexBytes) {
            node.triggerData.append((uint8_t)h.toUInt(&ok, 16));
        }
        break;
    }
    case COL_RESP_ID:
        node.responseId = item->text().toUInt(&ok, 16);
        if (!ok) node.responseId = node.triggerId + 1;
        break;
    case COL_RESP_DATA: {
        node.staticResponse.clear();
        auto hexBytes = item->text().trimmed().split(' ', Qt::SkipEmptyParts);
        for (const auto &h : hexBytes)
            node.staticResponse.append((uint8_t)h.toUInt(&ok, 16));
        break;
    }
    case COL_DELAY:
        node.responseDelayMs = item->text().toInt(&ok);
        if (!ok) node.responseDelayMs = 0;
        break;
    }
}

void CanNodeSimWidget::refreshTable() {
    m_nodeTable->blockSignals(true);
    for (int row = 0; row < m_simulator.nodes().size() && row < m_nodeTable->rowCount(); ++row) {
        const auto &node = m_simulator.nodes().at(row);

        // Aktualizuj checkbox enabled
        auto *enItem = m_nodeTable->item(row, COL_ENABLED);
        if (enItem) enItem->setCheckState(node.enabled ? Qt::Checked : Qt::Unchecked);

        // Aktualizuj statystyki (tylko jeśli komórki istnieją)
        auto *matchItem = m_nodeTable->item(row, COL_MATCHES);
        if (matchItem) matchItem->setText(QString::number(node.matchCount));

        auto *respItem = m_nodeTable->item(row, COL_RESPONSES);
        if (respItem) respItem->setText(QString::number(node.responseCount));
    }
    m_nodeTable->blockSignals(false);
}

void CanNodeSimWidget::populateRow(int row, const CanNodeDefinition &node) {
    m_nodeTable->blockSignals(true);

    // Checkbox enabled
    auto *enItem = new QTableWidgetItem;
    enItem->setCheckState(node.enabled ? Qt::Checked : Qt::Unchecked);
    enItem->setFlags(enItem->flags() | Qt::ItemIsUserCheckable);
    m_nodeTable->setItem(row, COL_ENABLED, enItem);

    m_nodeTable->setItem(row, COL_NAME,      new QTableWidgetItem(node.name));
    m_nodeTable->setItem(row, COL_TRIG_ID,   new QTableWidgetItem(
        QString("0x%1").arg(node.triggerId, 3, 16, QChar('0')).toUpper()));
    m_nodeTable->setItem(row, COL_TRIG_DATA, new QTableWidgetItem(
        [&]() { QStringList l; for (auto b : node.triggerData) l.append(QString("%1").arg(b, 2, 16, QChar('0'))); return l.join(' '); }()));
    m_nodeTable->setItem(row, COL_RESP_ID,   new QTableWidgetItem(
        QString("0x%1").arg(node.responseId, 3, 16, QChar('0')).toUpper()));
    m_nodeTable->setItem(row, COL_RESP_DATA, new QTableWidgetItem(
        [&]() { QStringList l; for (auto b : node.staticResponse) l.append(QString("%1").arg(b, 2, 16, QChar('0'))); return l.join(' '); }()));
    m_nodeTable->setItem(row, COL_DELAY,     new QTableWidgetItem(QString::number(node.responseDelayMs)));

    // Statystyki – read only
    auto *matchItem = new QTableWidgetItem(QString::number(node.matchCount));
    matchItem->setFlags(matchItem->flags() & ~Qt::ItemIsEditable);
    matchItem->setForeground(QColor("#ffaa00"));
    m_nodeTable->setItem(row, COL_MATCHES, matchItem);

    auto *respItem = new QTableWidgetItem(QString::number(node.responseCount));
    respItem->setFlags(respItem->flags() & ~Qt::ItemIsEditable);
    respItem->setForeground(QColor("#00ffaa"));
    m_nodeTable->setItem(row, COL_RESPONSES, respItem);

    // Kolorowanie
    for (int c = 0; c < _COUNT; ++c) {
        auto *it = m_nodeTable->item(row, c);
        if (it) it->setForeground(QColor("#c0c0c0"));
    }

    m_nodeTable->blockSignals(false);
}

CanNodeDefinition CanNodeSimWidget::nodeFromRow(int row) const {
    CanNodeDefinition node;
    if (row < 0 || row >= m_simulator.nodes().size()) return node;
    node = m_simulator.nodes().at(row);
    return node;
}

void CanNodeSimWidget::loadConfig() {
    QString path = QFileDialog::getOpenFileName(this, "Wczytaj konfigurację symulatora", "", "JSON (*.json)");
    if (path.isEmpty()) return;
    if (m_simulator.loadConfig(path)) {
        // Odbuduj tabelę
        m_nodeTable->setRowCount(0);
        for (int i = 0; i < m_simulator.nodes().size(); ++i) {
            m_nodeTable->insertRow(i);
            populateRow(i, m_simulator.nodes().at(i));
        }
        m_statusLabel->setText(QString("Symulator: %1 węzłów (wczytano).").arg(m_simulator.nodes().size()));
    } else {
        QMessageBox::warning(this, "Błąd", "Nie udało się wczytać pliku.");
    }
}

void CanNodeSimWidget::saveConfig() {
    QString path = QFileDialog::getSaveFileName(this, "Zapisz konfigurację symulatora", "", "JSON (*.json)");
    if (path.isEmpty()) return;
    if (m_simulator.saveConfig(path)) {
        m_statusLabel->setText(QString("Symulator: %1 węzłów (zapisano).").arg(m_simulator.nodes().size()));
    } else {
        QMessageBox::warning(this, "Błąd", "Nie udało się zapisać pliku.");
    }
}
