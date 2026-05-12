#include "UdsSequenceWidget.h"
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QVBoxLayout>
#include <sstream>
#include <iomanip>

static std::vector<uint8_t> parseHexBytes(const QString &s) {
    std::vector<uint8_t> out;
    for (const auto &tok : s.split(' ', Qt::SkipEmptyParts)) {
        bool ok;
        uint32_t v = tok.toUInt(&ok, 16);
        if (ok) out.push_back(static_cast<uint8_t>(v & 0xFF));
    }
    return out;
}

static QString bytesToHex(const std::vector<uint8_t> &v) {
    std::ostringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) ss << ' ';
        ss << std::setw(2) << (int)v[i];
    }
    return QString::fromStdString(ss.str());
}

// ── Built-in sequence templates ───────────────────────────────────────────────

struct Template { QString name; std::vector<UdsStep> steps; };

static std::vector<Template> builtinTemplates() {
    std::vector<Template> t;

    // ECU identification scan
    Template id;
    id.name = "Identyfikacja ECU (0x22)";
    id.steps.push_back({ "DiagSession Default", 0x7DF, 0x7E8, {0x10, 0x01}, 1000, 2, false });
    id.steps.push_back({ "ReadDataByIdentifier: VIN",       0x7DF, 0x7E8, {0x22, 0xF1, 0x90}, 1000, 2, false });
    id.steps.push_back({ "ReadDataByIdentifier: ECU Name",  0x7DF, 0x7E8, {0x22, 0xF1, 0x8A}, 1000, 2, false });
    id.steps.push_back({ "ReadDataByIdentifier: SW Version",0x7DF, 0x7E8, {0x22, 0xF1, 0x89}, 1000, 2, false });
    t.push_back(std::move(id));

    // DTC read/clear
    Template dtc;
    dtc.name = "Odczyt i kasowanie DTC";
    dtc.steps.push_back({ "DiagSession Extended",       0x7DF, 0x7E8, {0x10, 0x03}, 1000, 2, true });
    dtc.steps.push_back({ "ReadDTC: All stored",        0x7DF, 0x7E8, {0x19, 0x02, 0xFF}, 2000, 1, false });
    dtc.steps.push_back({ "ClearDiagnosticInformation", 0x7DF, 0x7E8, {0x14, 0xFF, 0xFF, 0xFF}, 2000, 1, true });
    dtc.steps.push_back({ "Powrót do sesji domyślnej",  0x7DF, 0x7E8, {0x10, 0x01}, 500, 1, false });
    t.push_back(std::move(dtc));

    // Security access
    Template sec;
    sec.name = "Security Access (0x27)";
    sec.steps.push_back({ "DiagSession Extended",    0x7DF, 0x7E8, {0x10, 0x03}, 1000, 2, true });
    sec.steps.push_back({ "RequestSeed (level 0x01)",0x7DF, 0x7E8, {0x27, 0x01}, 1000, 2, true });
    // Note: SendKey (0x27 0x02 + key) requires dynamic seed — shown as placeholder
    sec.steps.push_back({ "SendKey (placeholder)",   0x7DF, 0x7E8, {0x27, 0x02, 0x00, 0x00}, 1000, 1, false });
    t.push_back(std::move(sec));

    return t;
}

// ── Widget ────────────────────────────────────────────────────────────────────

UdsSequenceWidget::UdsSequenceWidget(CanSniffer *sniffer, QWidget *parent)
    : QWidget(parent), m_sniffer(sniffer), m_runner(sniffer, this) {
    buildUi();
    connect(&m_runner, &UdsSequenceRunner::stepCompleted,
            this, &UdsSequenceWidget::onStepCompleted);
    connect(&m_runner, &UdsSequenceRunner::sequenceFinished,
            this, &UdsSequenceWidget::onSequenceFinished);
    connect(&m_runner, &UdsSequenceRunner::statusMessage,
            this, &UdsSequenceWidget::onStatus);
}

void UdsSequenceWidget::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    // ── Templates ─────────────────────────────────────────────────────────────
    auto *tmplRow = new QHBoxLayout;
    tmplRow->addWidget(new QLabel("Szablon:"));
    m_templateCombo = new QComboBox;
    m_templateCombo->addItem("— wybierz szablon —");
    for (const auto &t : builtinTemplates())
        m_templateCombo->addItem(t.name);
    tmplRow->addWidget(m_templateCombo);
    tmplRow->addStretch(1);
    root->addLayout(tmplRow);

    // ── Step editor ───────────────────────────────────────────────────────────
    auto *editorGroup  = new QGroupBox("Edytor kroków");
    auto *editorLayout = new QVBoxLayout(editorGroup);

    auto *fieldRow = new QHBoxLayout;
    fieldRow->addWidget(new QLabel("Nazwa:"));
    m_nameEdit = new QLineEdit;
    m_nameEdit->setPlaceholderText("np. DiagSession");
    fieldRow->addWidget(m_nameEdit);

    fieldRow->addWidget(new QLabel("TX ID (hex):"));
    m_txIdEdit = new QLineEdit("7DF");
    m_txIdEdit->setMaximumWidth(60);
    fieldRow->addWidget(m_txIdEdit);

    fieldRow->addWidget(new QLabel("RX ID (hex):"));
    m_rxIdEdit = new QLineEdit("7E8");
    m_rxIdEdit->setMaximumWidth(60);
    fieldRow->addWidget(m_rxIdEdit);

    fieldRow->addWidget(new QLabel("Payload (hex):"));
    m_payloadEdit = new QLineEdit;
    m_payloadEdit->setPlaceholderText("np. 10 03");
    m_payloadEdit->setMinimumWidth(180);
    fieldRow->addWidget(m_payloadEdit);

    fieldRow->addWidget(new QLabel("Timeout [ms]:"));
    m_timeoutSpin = new QSpinBox;
    m_timeoutSpin->setRange(100, 30000);
    m_timeoutSpin->setValue(1000);
    fieldRow->addWidget(m_timeoutSpin);

    fieldRow->addWidget(new QLabel("Retry:"));
    m_retriesSpin = new QSpinBox;
    m_retriesSpin->setRange(0, 5);
    m_retriesSpin->setValue(2);
    fieldRow->addWidget(m_retriesSpin);

    m_stopNegCheck = new QCheckBox("Abort on NRC");
    m_stopNegCheck->setChecked(true);
    fieldRow->addWidget(m_stopNegCheck);

    editorLayout->addLayout(fieldRow);

    auto *btnRow = new QHBoxLayout;
    auto *addBtn  = new QPushButton("Dodaj krok");
    addBtn->setStyleSheet("color: #44aaff;");
    auto *delBtn  = new QPushButton("Usuń");
    auto *upBtn   = new QPushButton("↑");
    auto *downBtn = new QPushButton("↓");
    btnRow->addWidget(addBtn);
    btnRow->addWidget(delBtn);
    btnRow->addWidget(upBtn);
    btnRow->addWidget(downBtn);
    btnRow->addStretch(1);
    editorLayout->addLayout(btnRow);

    m_stepTable = new QTableWidget(0, 7);
    m_stepTable->setHorizontalHeaderLabels({"Nazwa", "TX ID", "RX ID", "Payload", "Timeout", "Retry", "Abort NRC"});
    m_stepTable->horizontalHeader()->setStretchLastSection(false);
    m_stepTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_stepTable->verticalHeader()->setVisible(false);
    m_stepTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_stepTable->setAlternatingRowColors(true);
    m_stepTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_stepTable->setMaximumHeight(180);
    editorLayout->addWidget(m_stepTable);
    root->addWidget(editorGroup);

    // ── Control ───────────────────────────────────────────────────────────────
    auto *ctrlRow = new QHBoxLayout;
    m_startBtn = new QPushButton("▶  Uruchom sekwencję");
    m_startBtn->setStyleSheet("font-weight: bold; color: #44ff88;");
    m_abortBtn = new QPushButton("⏹  Przerwij");
    m_abortBtn->setStyleSheet("color: #ff4444;");
    m_abortBtn->setEnabled(false);
    m_progressLabel = new QLabel("0/0");
    m_statusLabel   = new QLabel("Gotowy");
    m_statusLabel->setStyleSheet("color: #aaaaaa;");
    ctrlRow->addWidget(m_startBtn);
    ctrlRow->addWidget(m_abortBtn);
    ctrlRow->addWidget(m_progressLabel);
    ctrlRow->addStretch(1);
    ctrlRow->addWidget(m_statusLabel);
    root->addLayout(ctrlRow);

    // ── Results ───────────────────────────────────────────────────────────────
    auto *resGroup  = new QGroupBox("Wyniki");
    auto *resLayout = new QVBoxLayout(resGroup);

    m_resultTable = new QTableWidget(0, 5);
    m_resultTable->setHorizontalHeaderLabels({"Krok", "Status", "Szczegóły", "Czas [ms]", "Odpowiedź (hex)"});
    m_resultTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_resultTable->verticalHeader()->setVisible(false);
    m_resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultTable->setAlternatingRowColors(true);
    resLayout->addWidget(m_resultTable);
    root->addWidget(resGroup, 1);

    // Connections
    connect(addBtn,  &QPushButton::clicked, this, &UdsSequenceWidget::addStep);
    connect(delBtn,  &QPushButton::clicked, this, &UdsSequenceWidget::removeStep);
    connect(upBtn,   &QPushButton::clicked, this, &UdsSequenceWidget::moveUp);
    connect(downBtn, &QPushButton::clicked, this, &UdsSequenceWidget::moveDown);
    connect(m_startBtn, &QPushButton::clicked, this, &UdsSequenceWidget::onStart);
    connect(m_abortBtn, &QPushButton::clicked, this, &UdsSequenceWidget::onAbort);
    connect(m_templateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &UdsSequenceWidget::loadTemplate);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void UdsSequenceWidget::processFrame(const CanFrame &frame) {
    m_runner.processFrame(frame);
}

void UdsSequenceWidget::addStep() {
    UdsStep step;
    step.name          = m_nameEdit->text().isEmpty() ? QString("Krok %1").arg(m_stepTable->rowCount() + 1)
                                                      : m_nameEdit->text();
    step.txCanId       = m_txIdEdit->text().toUInt(nullptr, 16);
    step.rxCanId       = m_rxIdEdit->text().toUInt(nullptr, 16);
    step.payload       = parseHexBytes(m_payloadEdit->text());
    step.timeoutMs     = m_timeoutSpin->value();
    step.maxRetries    = m_retriesSpin->value();
    step.stopOnNegResponse = m_stopNegCheck->isChecked();

    int row = m_stepTable->rowCount();
    m_stepTable->insertRow(row);
    m_stepTable->setItem(row, 0, new QTableWidgetItem(step.name));
    m_stepTable->setItem(row, 1, new QTableWidgetItem(
        QString("0x%1").arg(step.txCanId, 3, 16, QChar('0')).toUpper()));
    m_stepTable->setItem(row, 2, new QTableWidgetItem(
        QString("0x%1").arg(step.rxCanId, 3, 16, QChar('0')).toUpper()));
    m_stepTable->setItem(row, 3, new QTableWidgetItem(bytesToHex(step.payload)));
    m_stepTable->setItem(row, 4, new QTableWidgetItem(QString::number(step.timeoutMs)));
    m_stepTable->setItem(row, 5, new QTableWidgetItem(QString::number(step.maxRetries)));
    m_stepTable->setItem(row, 6, new QTableWidgetItem(step.stopOnNegResponse ? "Tak" : "Nie"));
}

void UdsSequenceWidget::removeStep() {
    auto sel = m_stepTable->selectedItems();
    if (!sel.isEmpty())
        m_stepTable->removeRow(sel.first()->row());
}

void UdsSequenceWidget::moveUp() {
    int row = m_stepTable->currentRow();
    if (row <= 0) return;
    for (int col = 0; col < m_stepTable->columnCount(); ++col) {
        auto *a = m_stepTable->takeItem(row, col);
        auto *b = m_stepTable->takeItem(row - 1, col);
        m_stepTable->setItem(row, col, b);
        m_stepTable->setItem(row - 1, col, a);
    }
    m_stepTable->setCurrentCell(row - 1, 0);
}

void UdsSequenceWidget::moveDown() {
    int row = m_stepTable->currentRow();
    if (row < 0 || row >= m_stepTable->rowCount() - 1) return;
    for (int col = 0; col < m_stepTable->columnCount(); ++col) {
        auto *a = m_stepTable->takeItem(row, col);
        auto *b = m_stepTable->takeItem(row + 1, col);
        m_stepTable->setItem(row, col, b);
        m_stepTable->setItem(row + 1, col, a);
    }
    m_stepTable->setCurrentCell(row + 1, 0);
}

void UdsSequenceWidget::onStart() {
    if (m_stepTable->rowCount() == 0) return;

    std::vector<UdsStep> steps;
    for (int r = 0; r < m_stepTable->rowCount(); ++r)
        steps.push_back(stepFromRow(r));

    m_runner.setSteps(steps);

    // Reset result table
    m_resultTable->setRowCount(0);
    for (int r = 0; r < m_stepTable->rowCount(); ++r) {
        m_resultTable->insertRow(r);
        m_resultTable->setItem(r, 0, new QTableWidgetItem(
            m_stepTable->item(r, 0) ? m_stepTable->item(r, 0)->text() : QString()));
        m_resultTable->setItem(r, 1, new QTableWidgetItem("Oczekiwanie..."));
        m_resultTable->setItem(r, 2, new QTableWidgetItem(""));
        m_resultTable->setItem(r, 3, new QTableWidgetItem(""));
        m_resultTable->setItem(r, 4, new QTableWidgetItem(""));
    }

    m_startBtn->setEnabled(false);
    m_abortBtn->setEnabled(true);
    m_progressLabel->setText(QString("0/%1").arg(steps.size()));
    m_runner.start();
}

void UdsSequenceWidget::onAbort() {
    m_runner.abort();
}

UdsStep UdsSequenceWidget::stepFromRow(int row) const {
    UdsStep s;
    auto get = [&](int col) -> QString {
        auto *it = m_stepTable->item(row, col);
        return it ? it->text() : QString();
    };
    s.name          = get(0);
    s.txCanId       = get(1).mid(2).toUInt(nullptr, 16);
    s.rxCanId       = get(2).mid(2).toUInt(nullptr, 16);
    s.payload       = parseHexBytes(get(3));
    s.timeoutMs     = get(4).toInt();
    s.maxRetries    = get(5).toInt();
    s.stopOnNegResponse = (get(6) == "Tak");
    return s;
}

void UdsSequenceWidget::onStepCompleted(int stepIndex, UdsStepResult result) {
    if (stepIndex >= m_resultTable->rowCount()) return;

    static const QString statusNames[] = { "Oczekiwanie", "PASS", "FAIL", "NRC", "TIMEOUT", "POMINIĘTY" };
    static const QColor  statusColors[] = {
        QColor(0xAA, 0xAA, 0xAA),
        QColor(0x44, 0xFF, 0x88),
        QColor(0xFF, 0x33, 0x33),
        QColor(0xFF, 0xAA, 0x00),
        QColor(0xFF, 0x66, 0x00),
        QColor(0x88, 0x88, 0x88),
    };

    int st = static_cast<int>(result.status);
    m_resultTable->setItem(stepIndex, 1, new QTableWidgetItem(statusNames[st]));
    m_resultTable->setItem(stepIndex, 2, new QTableWidgetItem(result.detail));
    m_resultTable->setItem(stepIndex, 3, new QTableWidgetItem(
        QString::number(result.elapsedMs, 'f', 1)));
    m_resultTable->setItem(stepIndex, 4, new QTableWidgetItem(bytesToHex(result.response)));

    // Color the status cell
    for (int col = 0; col < m_resultTable->columnCount(); ++col) {
        if (auto *item = m_resultTable->item(stepIndex, col))
            item->setBackground(QColor(statusColors[st].red(), statusColors[st].green(),
                                       statusColors[st].blue(), 40));
    }

    m_progressLabel->setText(QString("%1/%2").arg(stepIndex + 1)
                              .arg(m_resultTable->rowCount()));
}

void UdsSequenceWidget::onSequenceFinished(bool allPassed) {
    m_startBtn->setEnabled(true);
    m_abortBtn->setEnabled(false);
    m_statusLabel->setStyleSheet(allPassed ? "color: #44ff88; font-weight: bold;"
                                           : "color: #ff3333; font-weight: bold;");
}

void UdsSequenceWidget::onStatus(const QString &msg) {
    m_statusLabel->setText(msg);
}

void UdsSequenceWidget::loadTemplate(int index) {
    if (index <= 0) return;
    auto templates = builtinTemplates();
    if (index - 1 >= static_cast<int>(templates.size())) return;

    m_stepTable->setRowCount(0);
    const auto &tmpl = templates[static_cast<size_t>(index - 1)];
    for (const auto &step : tmpl.steps) {
        int row = m_stepTable->rowCount();
        m_stepTable->insertRow(row);
        m_stepTable->setItem(row, 0, new QTableWidgetItem(step.name));
        m_stepTable->setItem(row, 1, new QTableWidgetItem(
            QString("0x%1").arg(step.txCanId, 3, 16, QChar('0')).toUpper()));
        m_stepTable->setItem(row, 2, new QTableWidgetItem(
            QString("0x%1").arg(step.rxCanId, 3, 16, QChar('0')).toUpper()));
        m_stepTable->setItem(row, 3, new QTableWidgetItem(bytesToHex(step.payload)));
        m_stepTable->setItem(row, 4, new QTableWidgetItem(QString::number(step.timeoutMs)));
        m_stepTable->setItem(row, 5, new QTableWidgetItem(QString::number(step.maxRetries)));
        m_stepTable->setItem(row, 6, new QTableWidgetItem(step.stopOnNegResponse ? "Tak" : "Nie"));
    }
}
