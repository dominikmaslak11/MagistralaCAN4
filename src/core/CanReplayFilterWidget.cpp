#include "CanReplayFilterWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QGroupBox>
#include <QScrollArea>
#include <QFont>
#include <format>

CanReplayFilterWidget::CanReplayFilterWidget(CanSniffer *sniffer, QWidget *parent)
    : QWidget(parent), m_sniffer(sniffer)
{
    buildUi();
}

void CanReplayFilterWidget::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // ── Rule editor ──────────────────────────────────────────────────────────
    auto *editGrp  = new QGroupBox("New Rule", this);
    auto *editForm = new QFormLayout(editGrp);

    m_srcIdEdit  = new QLineEdit("0x000", this);
    m_srcIdEdit->setMaximumWidth(100);
    m_dstIdEdit  = new QLineEdit("0x000", this);
    m_dstIdEdit->setMaximumWidth(100);
    m_remapCheck = new QCheckBox("Remap to Dst ID", this);
    m_dropCheck  = new QCheckBox("Drop frame", this);

    editForm->addRow("Src ID:", m_srcIdEdit);

    auto *dstRow = new QHBoxLayout;
    dstRow->addWidget(m_remapCheck);
    dstRow->addWidget(new QLabel("Dst ID:", this));
    dstRow->addWidget(m_dstIdEdit);
    dstRow->addStretch();
    editForm->addRow(dstRow);
    editForm->addRow(m_dropCheck);

    // Byte transforms
    auto *byteGrp  = new QGroupBox("Byte Transforms (new = round(old * scale + offset), clamped 0-255)", editGrp);
    auto *byteGrid = new QHBoxLayout(byteGrp);
    for (int i = 0; i < 8; ++i) {
        auto *col = new QVBoxLayout;
        m_byteActiveChecks[i] = new QCheckBox(QString("B%1").arg(i), this);
        m_scaleSpins[i]  = new QDoubleSpinBox(this);
        m_scaleSpins[i]->setRange(-1000, 1000);
        m_scaleSpins[i]->setSingleStep(0.1);
        m_scaleSpins[i]->setValue(1.0);
        m_scaleSpins[i]->setDecimals(2);
        m_scaleSpins[i]->setMaximumWidth(70);
        m_offsetSpins[i] = new QDoubleSpinBox(this);
        m_offsetSpins[i]->setRange(-255, 255);
        m_offsetSpins[i]->setSingleStep(1.0);
        m_offsetSpins[i]->setValue(0.0);
        m_offsetSpins[i]->setDecimals(1);
        m_offsetSpins[i]->setMaximumWidth(70);
        col->addWidget(m_byteActiveChecks[i]);
        col->addWidget(new QLabel("×:", this));
        col->addWidget(m_scaleSpins[i]);
        col->addWidget(new QLabel("+:", this));
        col->addWidget(m_offsetSpins[i]);
        byteGrid->addLayout(col);
    }
    editForm->addRow(byteGrp);

    auto *btnRow = new QHBoxLayout;
    m_addBtn    = new QPushButton("Add Rule", this);
    m_removeBtn = new QPushButton("Remove Selected", this);
    m_applyBtn  = new QPushButton("Apply Rules", this);
    m_clearBtn  = new QPushButton("Clear All", this);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_removeBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_applyBtn);
    btnRow->addWidget(m_clearBtn);
    editForm->addRow(btnRow);

    root->addWidget(editGrp);

    // ── Rules table ──────────────────────────────────────────────────────────
    m_rulesTable = new QTableWidget(0, 4, this);
    m_rulesTable->setHorizontalHeaderLabels({"Src ID", "Action", "Byte Transforms", "Drop"});
    m_rulesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_rulesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rulesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_rulesTable->setAlternatingRowColors(true);
    m_rulesTable->setFont(QFont("Courier New", 9));
    root->addWidget(m_rulesTable, 1);

    m_statusLabel = new QLabel("Rules: 0", this);
    root->addWidget(m_statusLabel);

    connect(m_addBtn,    &QPushButton::clicked, this, &CanReplayFilterWidget::addRule);
    connect(m_removeBtn, &QPushButton::clicked, this, &CanReplayFilterWidget::removeSelectedRule);
    connect(m_applyBtn,  &QPushButton::clicked, this, &CanReplayFilterWidget::applyRules);
    connect(m_clearBtn,  &QPushButton::clicked, this, &CanReplayFilterWidget::clearRules);
}

void CanReplayFilterWidget::addRule() {
    bool ok = false;
    uint32_t srcId = m_srcIdEdit->text().toUInt(&ok, 16);
    if (!ok) return;

    ReplayRule r;
    r.srcId   = srcId;
    r.drop    = m_dropCheck->isChecked();
    r.remapId = m_remapCheck->isChecked();
    if (r.remapId) r.dstId = m_dstIdEdit->text().toUInt(nullptr, 16);

    for (int i = 0; i < 8; ++i) {
        r.byteXforms[i].active = m_byteActiveChecks[i]->isChecked();
        r.byteXforms[i].scale  = m_scaleSpins[i]->value();
        r.byteXforms[i].offset = m_offsetSpins[i]->value();
    }

    auto rules = m_filter.rules();
    rules.push_back(r);
    m_filter.setRules(rules);
    refreshRuleTable();
}

void CanReplayFilterWidget::removeSelectedRule() {
    auto selected = m_rulesTable->selectedItems();
    if (selected.isEmpty()) return;
    int row = m_rulesTable->currentRow();
    auto rules = m_filter.rules();
    if (row >= 0 && row < static_cast<int>(rules.size())) {
        rules.erase(rules.begin() + row);
        m_filter.setRules(rules);
        refreshRuleTable();
    }
}

void CanReplayFilterWidget::applyRules() {
    m_statusLabel->setText(QString("Rules active: %1").arg(m_filter.rules().size()));
}

void CanReplayFilterWidget::clearRules() {
    m_filter.clearRules();
    refreshRuleTable();
}

void CanReplayFilterWidget::replayBatch(const std::vector<CanFrame> &frames) {
    if (!m_sniffer) return;
    auto out = m_filter.applyBatch(frames);
    for (auto &f : out) m_sniffer->writeFrame(f);
}

void CanReplayFilterWidget::refreshRuleTable() {
    m_rulesTable->setRowCount(0);
    const auto &rules = m_filter.rules();
    for (auto &r : rules) {
        int row = m_rulesTable->rowCount();
        m_rulesTable->insertRow(row);

        m_rulesTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(std::format("0x{:X}", r.srcId))));

        QString action;
        if (r.drop) action = "DROP";
        else if (r.remapId) {
            action = QString::fromStdString(std::format("REMAP → 0x{:X}", r.dstId));
        } else {
            action = "TRANSFORM";
        }
        m_rulesTable->setItem(row, 1, new QTableWidgetItem(action));

        QString xfDesc;
        for (int i = 0; i < 8; ++i) {
            if (r.byteXforms[i].active) {
                if (!xfDesc.isEmpty()) xfDesc += " ";
                xfDesc += QString("B%1(×%2+%3)").arg(i)
                            .arg(r.byteXforms[i].scale, 0, 'f', 2)
                            .arg(r.byteXforms[i].offset, 0, 'f', 1);
            }
        }
        m_rulesTable->setItem(row, 2, new QTableWidgetItem(xfDesc.isEmpty() ? "-" : xfDesc));
        m_rulesTable->setItem(row, 3, new QTableWidgetItem(r.drop ? "Yes" : "No"));
    }
    m_statusLabel->setText(QString("Rules: %1").arg(rules.size()));
}
