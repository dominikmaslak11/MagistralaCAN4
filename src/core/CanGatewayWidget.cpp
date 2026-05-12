#include "CanGatewayWidget.h"
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QVBoxLayout>

CanGatewayWidget::CanGatewayWidget(QWidget *parent) : QWidget(parent) {
    buildUi();

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(500);
    connect(m_statsTimer, &QTimer::timeout, this, &CanGatewayWidget::onTick);
    m_statsTimer->start();

    connect(&m_gateway, &CanGateway::statsUpdated, this,
            [this](quint64 fwd, quint64 blk, quint64 tot) {
                m_fwdLabel->setText(QString("Przekazano: %1").arg(fwd));
                m_blkLabel->setText(QString("Zablokowano: %1").arg(blk));
                m_totLabel->setText(QString("Razem: %1").arg(tot));
            });
}

void CanGatewayWidget::addDriver(const QString &name, ICanDriver *driver) {
    m_drivers.push_back({name, driver});
    m_srcCombo->addItem(name);
    m_sinkCombo->addItem(name);
}

// ── UI ────────────────────────────────────────────────────────────────────────

void CanGatewayWidget::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    // ── Interface selection ────────────────────────────────────────────────────
    auto *ifGroup  = new QGroupBox("Interfejsy");
    auto *ifLayout = new QHBoxLayout(ifGroup);

    ifLayout->addWidget(new QLabel("Źródło:"));
    m_srcCombo = new QComboBox;
    m_srcCombo->addItem("— Główny sniffer —");
    ifLayout->addWidget(m_srcCombo);

    ifLayout->addWidget(new QLabel("→  Cel:"));
    m_sinkCombo = new QComboBox;
    m_sinkCombo->addItem("— brak —");
    ifLayout->addWidget(m_sinkCombo);

    m_startStopBtn = new QPushButton("▶  Uruchom gateway");
    m_startStopBtn->setStyleSheet("font-weight: bold; color: #44ff88;");
    ifLayout->addStretch(1);
    ifLayout->addWidget(m_startStopBtn);

    m_statusLabel = new QLabel("Zatrzymany");
    m_statusLabel->setStyleSheet("color: #aaaaaa;");
    ifLayout->addWidget(m_statusLabel);

    root->addWidget(ifGroup);

    // ── Filter mode ────────────────────────────────────────────────────────────
    auto *filterGroup  = new QGroupBox("Tryb filtrowania");
    auto *filterLayout = new QHBoxLayout(filterGroup);

    m_passAllBtn   = new QRadioButton("Przepuść wszystkie");
    m_whitelistBtn = new QRadioButton("Whitelist (tylko wybrane ID)");
    m_blacklistBtn = new QRadioButton("Blacklist (blokuj wybrane ID)");
    m_passAllBtn->setChecked(true);

    filterLayout->addWidget(m_passAllBtn);
    filterLayout->addWidget(m_whitelistBtn);
    filterLayout->addWidget(m_blacklistBtn);
    filterLayout->addStretch(1);
    root->addWidget(filterGroup);

    // ── Rule editor ────────────────────────────────────────────────────────────
    auto *ruleGroup  = new QGroupBox("Reguły ID (filtrowanie / remapping)");
    auto *ruleLayout = new QVBoxLayout(ruleGroup);

    auto *ruleInputRow = new QHBoxLayout;
    ruleInputRow->addWidget(new QLabel("CAN ID (hex):"));
    m_ruleIdEdit = new QLineEdit;
    m_ruleIdEdit->setPlaceholderText("np. 0x100");
    m_ruleIdEdit->setMaximumWidth(100);
    ruleInputRow->addWidget(m_ruleIdEdit);

    ruleInputRow->addWidget(new QLabel("Remapuj do (hex, opcjonalne):"));
    m_ruleDstEdit = new QLineEdit;
    m_ruleDstEdit->setPlaceholderText("np. 0x200");
    m_ruleDstEdit->setMaximumWidth(100);
    ruleInputRow->addWidget(m_ruleDstEdit);

    m_addRuleBtn = new QPushButton("Dodaj regułę");
    m_addRuleBtn->setStyleSheet("color: #44aaff;");
    m_delRuleBtn = new QPushButton("Usuń zaznaczone");
    ruleInputRow->addWidget(m_addRuleBtn);
    ruleInputRow->addWidget(m_delRuleBtn);
    ruleInputRow->addStretch(1);
    ruleLayout->addLayout(ruleInputRow);

    m_ruleTable = new QTableWidget(0, 3);
    m_ruleTable->setHorizontalHeaderLabels({"CAN ID (hex)", "Remap do", "Akcja"});
    m_ruleTable->horizontalHeader()->setStretchLastSection(true);
    m_ruleTable->verticalHeader()->setVisible(false);
    m_ruleTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ruleTable->setAlternatingRowColors(true);
    m_ruleTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ruleTable->setMaximumHeight(180);
    ruleLayout->addWidget(m_ruleTable);

    root->addWidget(ruleGroup);

    // ── Statistics ─────────────────────────────────────────────────────────────
    auto *statsGroup  = new QGroupBox("Statystyki");
    auto *statsLayout = new QHBoxLayout(statsGroup);

    m_fwdLabel = new QLabel("Przekazano: 0");
    m_fwdLabel->setStyleSheet("color: #44ff88; font-weight: bold;");
    m_blkLabel = new QLabel("Zablokowano: 0");
    m_blkLabel->setStyleSheet("color: #ff4444; font-weight: bold;");
    m_totLabel = new QLabel("Razem: 0");

    auto *resetStatsBtn = new QPushButton("Resetuj statystyki");
    statsLayout->addWidget(m_fwdLabel);
    statsLayout->addWidget(m_blkLabel);
    statsLayout->addWidget(m_totLabel);
    statsLayout->addStretch(1);
    statsLayout->addWidget(resetStatsBtn);

    root->addWidget(statsGroup);
    root->addStretch(1);

    // Connections
    connect(m_startStopBtn, &QPushButton::clicked, this, &CanGatewayWidget::onStartStop);
    connect(m_addRuleBtn,   &QPushButton::clicked, this, &CanGatewayWidget::addRule);
    connect(m_delRuleBtn,   &QPushButton::clicked, this, &CanGatewayWidget::removeSelectedRule);
    connect(resetStatsBtn,  &QPushButton::clicked, this, [this]() {
        m_gateway.resetStats();
        m_fwdLabel->setText("Przekazano: 0");
        m_blkLabel->setText("Zablokowano: 0");
        m_totLabel->setText("Razem: 0");
    });
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void CanGatewayWidget::processFrame(const CanFrame &frame) {
    if (m_gateway.isRunning())
        m_gateway.processFrame(frame);
}

void CanGatewayWidget::onStartStop() {
    if (m_gateway.isRunning()) {
        m_gateway.stop();
        updateStatus(false);
    } else {
        // Configure sink
        int sinkIdx = m_sinkCombo->currentIndex() - 1; // -1 for "brak"
        ICanDriver *sink = nullptr;
        if (sinkIdx >= 0 && sinkIdx < static_cast<int>(m_drivers.size()))
            sink = m_drivers[static_cast<size_t>(sinkIdx)].driver;

        if (!sink) {
            m_statusLabel->setText("Brak celu — wybierz interfejs docelowy");
            return;
        }

        // Configure filter mode
        if (m_passAllBtn->isChecked())
            m_gateway.setFilterMode(CanGateway::FilterMode::PassAll);
        else if (m_whitelistBtn->isChecked())
            m_gateway.setFilterMode(CanGateway::FilterMode::Whitelist);
        else
            m_gateway.setFilterMode(CanGateway::FilterMode::Blacklist);

        applyRulesToGateway();
        m_gateway.setSink(sink);
        // Source: when srcCombo index == 0 → main sniffer feeds via processFrame()
        // When another driver is selected → gateway polls it internally
        int srcIdx = m_srcCombo->currentIndex() - 1;
        if (srcIdx >= 0 && srcIdx < static_cast<int>(m_drivers.size()))
            m_gateway.setSource(m_drivers[static_cast<size_t>(srcIdx)].driver);

        m_gateway.start();
        updateStatus(true);
    }
}

void CanGatewayWidget::addRule() {
    QString idStr  = m_ruleIdEdit->text().trimmed();
    QString dstStr = m_ruleDstEdit->text().trimmed();
    if (idStr.isEmpty()) return;

    bool ok;
    uint32_t srcId = idStr.toUInt(&ok, 16);
    if (!ok) { m_statusLabel->setText("Nieprawidłowy ID (hex)"); return; }

    uint32_t dstId = 0;
    if (!dstStr.isEmpty()) {
        dstId = dstStr.toUInt(&ok, 16);
        if (!ok) { m_statusLabel->setText("Nieprawidłowy Remap ID (hex)"); return; }
    }

    int row = m_ruleTable->rowCount();
    m_ruleTable->insertRow(row);
    m_ruleTable->setItem(row, 0, new QTableWidgetItem(
        QString("0x%1").arg(srcId, 3, 16, QChar('0')).toUpper()));
    m_ruleTable->setItem(row, 1, new QTableWidgetItem(
        dstId ? QString("0x%1").arg(dstId, 3, 16, QChar('0')).toUpper() : "—"));

    QString action = m_blacklistBtn->isChecked() ? "BLOKUJ" : (dstId ? "REMAP" : "PRZEPUŚĆ");
    m_ruleTable->setItem(row, 2, new QTableWidgetItem(action));

    m_ruleIdEdit->clear();
    m_ruleDstEdit->clear();
    applyRulesToGateway();
}

void CanGatewayWidget::removeSelectedRule() {
    QList<QTableWidgetItem*> sel = m_ruleTable->selectedItems();
    if (sel.isEmpty()) return;
    int row = sel.first()->row();
    m_ruleTable->removeRow(row);
    applyRulesToGateway();
}

void CanGatewayWidget::applyRulesToGateway() {
    std::vector<CanGateway::IdRule> rules;
    for (int r = 0; r < m_ruleTable->rowCount(); ++r) {
        auto *idItem  = m_ruleTable->item(r, 0);
        auto *dstItem = m_ruleTable->item(r, 1);
        auto *actItem = m_ruleTable->item(r, 2);
        if (!idItem) continue;

        CanGateway::IdRule rule;
        rule.srcId = idItem->text().mid(2).toUInt(nullptr, 16);
        rule.dstId = (dstItem && dstItem->text() != "—")
                   ? dstItem->text().mid(2).toUInt(nullptr, 16) : 0;
        rule.drop  = actItem && actItem->text() == "BLOKUJ";
        rules.push_back(rule);
    }
    m_gateway.setRules(rules);
}

void CanGatewayWidget::updateStatus(bool running) {
    if (running) {
        m_startStopBtn->setText("⏹  Zatrzymaj gateway");
        m_startStopBtn->setStyleSheet("font-weight: bold; color: #ff4444;");
        m_statusLabel->setText("Aktywny");
        m_statusLabel->setStyleSheet("color: #44ff88; font-weight: bold;");
    } else {
        m_startStopBtn->setText("▶  Uruchom gateway");
        m_startStopBtn->setStyleSheet("font-weight: bold; color: #44ff88;");
        m_statusLabel->setText("Zatrzymany");
        m_statusLabel->setStyleSheet("color: #aaaaaa;");
    }
}

void CanGatewayWidget::onTick() {
    // nothing extra needed — statsUpdated signal handles live updates
}
