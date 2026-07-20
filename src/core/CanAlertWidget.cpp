#include "CanAlertWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QHeaderView>
#include <QFont>
#include <QDateTime>
#include <format>

CanAlertWidget::CanAlertWidget(QWidget *parent) : QWidget(parent) {
    buildUi();
    connect(&m_engine, &CanAlertEngine::alertTriggered, this, &CanAlertWidget::onAlertTriggered);
}

void CanAlertWidget::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // ── Rule editor ─────────────────────────────────────────────────────────
    auto *editorGrp  = new QGroupBox("New Alert Rule", this);
    auto *editorForm = new QFormLayout(editorGrp);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText("Rule name…");
    editorForm->addRow("Name:", m_nameEdit);

    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItems({"New CAN ID", "DLC Change", "Byte Value", "Rate Anomaly", "IsoForest Score"});
    editorForm->addRow("Type:", m_typeCombo);

    m_actionCombo = new QComboBox(this);
    m_actionCombo->addItems({"Log only", "Tray only", "Log + Tray"});
    m_actionCombo->setCurrentIndex(2);
    editorForm->addRow("Action:", m_actionCombo);

    // ByteValue-specific row (hidden for other types)
    auto *byteRow = new QHBoxLayout;
    m_idEdit = new QLineEdit("0", this);
    m_idEdit->setMaximumWidth(80);
    m_idEdit->setPlaceholderText("0=any");
    m_byteIdxSpin = new QSpinBox(this);
    m_byteIdxSpin->setRange(0, 7);
    m_byteIdxSpin->setMaximumWidth(50);
    m_opCombo = new QComboBox(this);
    m_opCombo->addItems({">", "<", ">=", "<=", "==", "!="});
    m_opCombo->setMaximumWidth(55);
    m_thresholdSpin = new QSpinBox(this);
    m_thresholdSpin->setRange(0, 255);
    m_thresholdSpin->setMaximumWidth(60);
    byteRow->addWidget(new QLabel("ID:", this));
    byteRow->addWidget(m_idEdit);
    byteRow->addWidget(new QLabel("byte[", this));
    byteRow->addWidget(m_byteIdxSpin);
    byteRow->addWidget(new QLabel("]", this));
    byteRow->addWidget(m_opCombo);
    byteRow->addWidget(m_thresholdSpin);
    byteRow->addStretch();
    editorForm->addRow("Condition:", byteRow);

    m_rateDevSpin = new QDoubleSpinBox(this);
    m_rateDevSpin->setRange(5.0, 500.0);
    m_rateDevSpin->setValue(50.0);
    m_rateDevSpin->setSuffix("  %  deviation");
    m_rateDevSpin->setMaximumWidth(150);
    editorForm->addRow("Rate threshold:", m_rateDevSpin);

    m_isoThresholdSpin = new QDoubleSpinBox(this);
    m_isoThresholdSpin->setRange(0.0, 1.0);
    m_isoThresholdSpin->setSingleStep(0.05);
    m_isoThresholdSpin->setValue(0.6);
    m_isoThresholdSpin->setDecimals(2);
    m_isoThresholdSpin->setMaximumWidth(100);
    editorForm->addRow("IsoForest threshold:", m_isoThresholdSpin);

    auto *btnRow = new QHBoxLayout;
    m_addBtn    = new QPushButton("Add Rule", this);
    m_removeBtn = new QPushButton("Remove Selected", this);
    m_removeBtn->setEnabled(false);
    auto *resetBtn = new QPushButton("Reset State", this);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_removeBtn);
    btnRow->addStretch();
    btnRow->addWidget(resetBtn);
    editorForm->addRow(btnRow);
    root->addWidget(editorGrp);

    // ── Splitter: rules + alert log ─────────────────────────────────────────
    auto *splitter = new QSplitter(Qt::Vertical, this);

    m_ruleTable = new QTableWidget(0, 4, this);
    m_ruleTable->setHorizontalHeaderLabels({"En", "Name", "Type", "Action"});
    m_ruleTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_ruleTable->horizontalHeader()->setDefaultSectionSize(90);
    m_ruleTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ruleTable->setAlternatingRowColors(true);
    m_ruleTable->setFont(QFont("Courier New", 9));
    splitter->addWidget(m_ruleTable);

    auto *alertGrp = new QGroupBox("Alert Log", this);
    auto *alertLayout = new QVBoxLayout(alertGrp);
    m_alertTable = new QTableWidget(0, 4, this);
    m_alertTable->setHorizontalHeaderLabels({"Time", "Rule", "CAN ID", "Description"});
    m_alertTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_alertTable->horizontalHeader()->setDefaultSectionSize(100);
    m_alertTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_alertTable->setAlternatingRowColors(true);
    m_alertTable->setFont(QFont("Courier New", 9));
    auto *clearBtn = new QPushButton("Clear Alerts", this);
    alertLayout->addWidget(m_alertTable);
    alertLayout->addWidget(clearBtn);
    splitter->addWidget(alertGrp);
    splitter->setSizes({150, 300});
    root->addWidget(splitter, 1);

    m_statsLabel = new QLabel("Rules: 0  |  Alerts: 0", this);
    root->addWidget(m_statsLabel);

    connect(m_addBtn,    &QPushButton::clicked, this, &CanAlertWidget::addRule);
    connect(m_removeBtn, &QPushButton::clicked, this, &CanAlertWidget::removeRule);
    connect(clearBtn,    &QPushButton::clicked, this, &CanAlertWidget::clearAlerts);
    connect(resetBtn,    &QPushButton::clicked, this, &CanAlertWidget::resetEngine);
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CanAlertWidget::onTypeChanged);
    connect(m_ruleTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        m_removeBtn->setEnabled(m_ruleTable->currentRow() >= 0);
    });

    onTypeChanged(0);
}

void CanAlertWidget::onTypeChanged(int idx) {
    bool isByte = (idx == 2);
    bool isRate = (idx == 3);
    bool isIso  = (idx == 4);
    m_idEdit->setVisible(isByte);
    m_byteIdxSpin->setVisible(isByte);
    m_opCombo->setVisible(isByte);
    m_thresholdSpin->setVisible(isByte);
    m_rateDevSpin->setVisible(isRate);
    m_isoThresholdSpin->setVisible(isIso);
}

void CanAlertWidget::addRule() {
    CanAlertRule rule;
    rule.name   = m_nameEdit->text().trimmed();
    if (rule.name.isEmpty()) rule.name = QString("Rule %1").arg(m_engine.rules().size() + 1);

    rule.type   = static_cast<AlertType>(m_typeCombo->currentIndex());
    const int actionIdx = m_actionCombo->currentIndex();
    rule.action = (actionIdx == 0) ? AlertAction::Log
                : (actionIdx == 1) ? AlertAction::Tray
                :                    AlertAction::Both;
    rule.enabled = true;

    if (rule.type == AlertType::ByteValue) {
        bool ok = false;
        rule.targetId  = m_idEdit->text().toUInt(&ok, 16);
        rule.matchAny  = !ok || rule.targetId == 0;
        rule.byteIdx   = m_byteIdxSpin->value();
        rule.op        = static_cast<ByteOp>(m_opCombo->currentIndex());
        rule.threshold = static_cast<uint8_t>(m_thresholdSpin->value());
    }
    if (rule.type == AlertType::RateAnomaly)
        rule.rateDeviationPct = m_rateDevSpin->value();

    if (rule.type == AlertType::IsolationForestScore) {
        rule.isThreshold = m_isoThresholdSpin->value();
        rule.scorer = m_scorer;  // injected from outside (MainWindow)
    }

    m_engine.addRule(rule);
    refreshRuleTable();
}

void CanAlertWidget::removeRule() {
    int row = m_ruleTable->currentRow();
    if (row < 0) return;
    m_engine.removeRule(row);
    refreshRuleTable();
}

void CanAlertWidget::clearAlerts() {
    m_alertTable->setRowCount(0);
    m_statsLabel->setText(QString("Rules: %1  |  Alerts: 0").arg(m_engine.rules().size()));
}

void CanAlertWidget::resetEngine() {
    m_engine.reset();
    clearAlerts();
}

void CanAlertWidget::onAlertTriggered(const CanAlert &alert) {
    int row = m_alertTable->rowCount();
    m_alertTable->insertRow(row);

    auto cell = [&](int col, const QString &txt, QColor bg = {}) {
        auto *item = new QTableWidgetItem(txt);
        item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        if (bg.isValid()) item->setBackground(bg);
        m_alertTable->setItem(row, col, item);
    };

    QString ts = QDateTime::fromMSecsSinceEpoch(
        static_cast<qint64>(alert.timestampUs / 1000)).toString("hh:mm:ss.zzz");

    cell(0, ts);
    cell(1, alert.ruleName, QColor(255, 80, 80, 60));
    cell(2, QString::fromStdString(std::format("0x{:X}", alert.frame.id)));
    cell(3, alert.description);

    m_alertTable->scrollToBottom();
    m_statsLabel->setText(
        QString("Rules: %1  |  Alerts: %2")
            .arg(m_engine.rules().size())
            .arg(m_engine.totalAlerts()));
}

void CanAlertWidget::onFrame(const CanFrame &frame, uint64_t tsUs) {
    m_engine.evaluate(frame, tsUs);
}

void CanAlertWidget::refreshRuleTable() {
    m_ruleTable->setRowCount(0);
    static const char *typeNames[] = {"New CAN ID", "DLC Change", "Byte Value", "Rate Anomaly"};
    static const char *actionNames[] = {"Log", "Tray", "Log+Tray"};
    for (const auto &r : m_engine.rules()) {
        int row = m_ruleTable->rowCount();
        m_ruleTable->insertRow(row);
        auto cell = [&](int col, const QString &txt) {
            auto *item = new QTableWidgetItem(txt);
            item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
            m_ruleTable->setItem(row, col, item);
        };
        cell(0, r.enabled ? "Y" : "N");
        cell(1, r.name);
        cell(2, typeNames[static_cast<int>(r.type)]);
        cell(3, actionNames[static_cast<int>(r.action)]);
    }
    m_statsLabel->setText(QString("Rules: %1  |  Alerts: %2")
        .arg(m_engine.rules().size()).arg(m_engine.totalAlerts()));
}
