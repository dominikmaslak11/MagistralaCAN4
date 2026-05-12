#include "CanPeriodicSenderWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QGroupBox>
#include <QFont>
#include <cstdio>

CanPeriodicSenderWidget::CanPeriodicSenderWidget(CanSniffer *sniffer, QWidget *parent)
    : QWidget(parent)
{
    m_sender = new CanPeriodicSender(sniffer, this);
    connect(m_sender, &CanPeriodicSender::statsUpdated, this, &CanPeriodicSenderWidget::onStatsUpdated);
    buildUi();
}

void CanPeriodicSenderWidget::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // ── Frame editor ─────────────────────────────────────────────────────────
    auto *grp  = new QGroupBox("Add Periodic Frame", this);
    auto *form = new QFormLayout(grp);

    m_idEdit = new QLineEdit("0x100", this);
    m_idEdit->setMaximumWidth(90);
    m_extCheck  = new QCheckBox("Extended", this);
    auto *idRow = new QHBoxLayout;
    idRow->addWidget(m_idEdit);
    idRow->addWidget(m_extCheck);
    idRow->addStretch();
    form->addRow("CAN ID:", idRow);

    m_dlcSpin = new QSpinBox(this);
    m_dlcSpin->setRange(0, 8);
    m_dlcSpin->setValue(8);
    m_dlcSpin->setMaximumWidth(50);
    form->addRow("DLC:", m_dlcSpin);

    auto *dataRow = new QHBoxLayout;
    for (int i = 0; i < 8; ++i) {
        m_byteEdits[i] = new QLineEdit("00", this);
        m_byteEdits[i]->setMaximumWidth(35);
        m_byteEdits[i]->setFont(QFont("Courier New", 9));
        dataRow->addWidget(m_byteEdits[i]);
    }
    form->addRow("Data (hex):", dataRow);

    m_periodSpin = new QSpinBox(this);
    m_periodSpin->setRange(10, 60000);
    m_periodSpin->setValue(100);
    m_periodSpin->setSuffix(" ms");
    m_periodSpin->setMaximumWidth(90);
    form->addRow("Period:", m_periodSpin);

    m_labelEdit = new QLineEdit(this);
    m_labelEdit->setPlaceholderText("Optional description");
    form->addRow("Label:", m_labelEdit);

    auto *btnRow = new QHBoxLayout;
    m_addBtn    = new QPushButton("Add", this);
    m_removeBtn = new QPushButton("Remove Selected", this);
    m_runBtn    = new QPushButton("Start", this);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_removeBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_runBtn);
    form->addRow(btnRow);
    root->addWidget(grp);

    // ── Schedule table ───────────────────────────────────────────────────────
    m_table = new QTableWidget(0, 6, this);
    m_table->setHorizontalHeaderLabels({"En", "Label", "ID", "Period (ms)", "Data", "TX Count"});
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->horizontalHeader()->setDefaultSectionSize(80);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setFont(QFont("Courier New", 9));
    root->addWidget(m_table, 1);

    m_statusLabel = new QLabel("Entries: 0  |  Stopped", this);
    root->addWidget(m_statusLabel);

    connect(m_addBtn,    &QPushButton::clicked, this, &CanPeriodicSenderWidget::addEntry);
    connect(m_removeBtn, &QPushButton::clicked, this, &CanPeriodicSenderWidget::removeSelected);
    connect(m_runBtn,    &QPushButton::clicked, this, &CanPeriodicSenderWidget::toggleRun);
}

CanFrame CanPeriodicSenderWidget::frameFromEditors() const {
    CanFrame f;
    bool ok = false;
    f.id       = m_idEdit->text().toUInt(&ok, 16);
    f.extended = m_extCheck->isChecked();
    f.dlc      = static_cast<uint8_t>(m_dlcSpin->value());
    for (int i = 0; i < f.dlc && i < 8; ++i)
        f.data[i] = static_cast<uint8_t>(m_byteEdits[i]->text().toUInt(nullptr, 16));
    return f;
}

void CanPeriodicSenderWidget::addEntry() {
    PeriodicEntry e;
    e.frame    = frameFromEditors();
    e.periodMs = m_periodSpin->value();
    e.label    = m_labelEdit->text().toStdString();
    e.enabled  = true;
    m_sender->addEntry(e);
    refreshTable();
}

void CanPeriodicSenderWidget::removeSelected() {
    int row = m_table->currentRow();
    if (row < 0) return;
    m_sender->removeEntry(row);
    refreshTable();
}

void CanPeriodicSenderWidget::toggleRun() {
    if (m_sender->isRunning()) {
        m_sender->stop();
        m_runBtn->setText("Start");
    } else {
        m_sender->start();
        m_runBtn->setText("Stop");
    }
    onStatsUpdated();
}

void CanPeriodicSenderWidget::onStatsUpdated() {
    refreshTable();
    QString state = m_sender->isRunning() ? "Running" : "Stopped";
    m_statusLabel->setText(QString("Entries: %1  |  %2").arg(m_sender->entryCount()).arg(state));
}

void CanPeriodicSenderWidget::onCellChanged(int /*row*/, int /*col*/) {}

void CanPeriodicSenderWidget::refreshTable() {
    m_table->setRowCount(0);
    for (int i = 0; i < m_sender->entryCount(); ++i) {
        const auto &e = m_sender->entries()[i];
        int row = m_table->rowCount();
        m_table->insertRow(row);

        auto cell = [&](int col, const QString &txt) {
            auto *item = new QTableWidgetItem(txt);
            item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
            m_table->setItem(row, col, item);
        };

        cell(0, e.enabled ? "Y" : "N");
        cell(1, QString::fromStdString(e.label));

        char idBuf[12];
        snprintf(idBuf, sizeof(idBuf), "0x%X%s", e.frame.id, e.frame.extended ? "x" : "");
        cell(2, idBuf);
        cell(3, QString::number(e.periodMs));

        QString dataStr;
        char bb[4];
        for (int b = 0; b < e.frame.dlc && b < 8; ++b) {
            if (b) dataStr += ' ';
            snprintf(bb, sizeof(bb), "%02X", e.frame.data[b]);
            dataStr += bb;
        }
        cell(4, dataStr);
        cell(5, QString::number(e.txCount));
    }
}
