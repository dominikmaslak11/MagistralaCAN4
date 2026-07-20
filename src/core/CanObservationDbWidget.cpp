#include "CanObservationDbWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QFont>
#include <QMessageBox>
#include <format>

CanObservationDbWidget::CanObservationDbWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void CanObservationDbWidget::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // ── Connection group ────────────────────────────────────────────────────
    auto *connGrp  = new QGroupBox("SQLite Database", this);
    auto *connForm = new QFormLayout(connGrp);

    auto *pathRow = new QHBoxLayout;
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText("Path to .db file…");
    m_openBtn  = new QPushButton("Open…", this);
    m_closeBtn = new QPushButton("Close", this);
    m_closeBtn->setEnabled(false);
    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(m_openBtn);
    pathRow->addWidget(m_closeBtn);
    connForm->addRow("File:", pathRow);

    auto *sessionRow = new QHBoxLayout;
    m_newSessionBtn = new QPushButton("New Session", this);
    m_newSessionBtn->setEnabled(false);
    m_resetBtn = new QPushButton("Reset DB", this);
    m_resetBtn->setEnabled(false);
    sessionRow->addWidget(m_newSessionBtn);
    sessionRow->addStretch();
    sessionRow->addWidget(m_resetBtn);
    connForm->addRow(sessionRow);
    root->addWidget(connGrp);

    // ── Query group ─────────────────────────────────────────────────────────
    auto *qryGrp  = new QGroupBox("Query by CAN ID", this);
    auto *qryForm = new QFormLayout(qryGrp);

    auto *qryRow = new QHBoxLayout;
    m_filterIdEdit = new QLineEdit("0x100", this);
    m_filterIdEdit->setMaximumWidth(90);
    m_limitSpin = new QSpinBox(this);
    m_limitSpin->setRange(1, 10000);
    m_limitSpin->setValue(500);
    m_limitSpin->setMaximumWidth(70);
    m_queryBtn = new QPushButton("Query", this);
    m_queryBtn->setEnabled(false);
    qryRow->addWidget(m_filterIdEdit);
    qryRow->addWidget(new QLabel("Limit:", this));
    qryRow->addWidget(m_limitSpin);
    qryRow->addStretch();
    qryRow->addWidget(m_queryBtn);
    qryForm->addRow("CAN ID:", qryRow);
    root->addWidget(qryGrp);

    // ── Results table ───────────────────────────────────────────────────────
    m_resultTable = new QTableWidget(0, 5, this);
    m_resultTable->setHorizontalHeaderLabels({"Session", "Timestamp (µs)", "CAN ID", "DLC", "Data"});
    m_resultTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_resultTable->horizontalHeader()->setDefaultSectionSize(100);
    m_resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultTable->setAlternatingRowColors(true);
    m_resultTable->setFont(QFont("Courier New", 9));
    root->addWidget(m_resultTable, 1);

    m_statsLabel = new QLabel("Not connected", this);
    root->addWidget(m_statsLabel);

    connect(m_openBtn,       &QPushButton::clicked, this, &CanObservationDbWidget::openDb);
    connect(m_closeBtn,      &QPushButton::clicked, this, &CanObservationDbWidget::closeDb);
    connect(m_newSessionBtn, &QPushButton::clicked, this, &CanObservationDbWidget::newSession);
    connect(m_resetBtn,      &QPushButton::clicked, this, &CanObservationDbWidget::resetDb);
    connect(m_queryBtn,      &QPushButton::clicked, this, &CanObservationDbWidget::queryById);
}

void CanObservationDbWidget::setConnected(bool connected) {
    m_openBtn->setEnabled(!connected);
    m_closeBtn->setEnabled(connected);
    m_newSessionBtn->setEnabled(connected);
    m_resetBtn->setEnabled(connected);
    m_queryBtn->setEnabled(connected);
}

void CanObservationDbWidget::openDb() {
    QString path = m_pathEdit->text().trimmed();
    if (path.isEmpty())
        path = QFileDialog::getSaveFileName(this, "Open / Create Database", {}, "SQLite DB (*.db);;All (*)");
    if (path.isEmpty()) return;

    m_pathEdit->setText(path);
    if (!m_db.open(path)) {
        QMessageBox::warning(this, "DB Error", "Could not open database.\nCheck the path and permissions.");
        return;
    }
    m_db.beginSession("auto");
    setConnected(true);
    refreshStats();
}

void CanObservationDbWidget::closeDb() {
    m_db.close();
    setConnected(false);
    m_statsLabel->setText("Not connected");
    m_resultTable->setRowCount(0);
}

void CanObservationDbWidget::newSession() {
    m_db.beginSession("manual");
    refreshStats();
}

void CanObservationDbWidget::resetDb() {
    auto answer = QMessageBox::question(this, "Reset DB",
        "This will delete ALL frames and sessions in the database. Continue?");
    if (answer != QMessageBox::Yes) return;
    m_db.reset();
    m_db.beginSession("auto");
    m_resultTable->setRowCount(0);
    refreshStats();
}

void CanObservationDbWidget::queryById() {
    bool ok = false;
    uint32_t id = m_filterIdEdit->text().toUInt(&ok, 16);
    if (!ok) { m_statsLabel->setText("Invalid CAN ID"); return; }

    auto rows = m_db.queryByCanId(id, -1, m_limitSpin->value());
    m_resultTable->setRowCount(0);

    for (const auto &row : rows) {
        int r = m_resultTable->rowCount();
        m_resultTable->insertRow(r);

        auto cell = [&](int col, const QString &txt) {
            auto *item = new QTableWidgetItem(txt);
            item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
            m_resultTable->setItem(r, col, item);
        };

        cell(0, QString::number(row.sessionId));
        cell(1, QString::number(row.timestampUs));
        cell(2, QString::fromStdString(std::format("0x{:X}{}", row.canId, row.extended ? "x" : "")));
        cell(3, QString::number(row.dlc));

        QString data;
        char bb[4];
        for (int i = 0; i < row.dlc && i < 8; ++i) {
            if (i) data += ' ';
            data += std::format("{:02X}", row.data[i]);
        }
        cell(4, data);
    }
    refreshStats();
}

void CanObservationDbWidget::onFrame(const CanFrame &frame, uint64_t timestampUs) {
    if (!m_db.isOpen()) return;
    m_db.recordFrame(frame, timestampUs);
}

void CanObservationDbWidget::refreshStats() {
    if (!m_db.isOpen()) return;
    int64_t total   = m_db.totalFrameCount();
    int64_t session = m_db.totalFrameCount(m_db.currentSessionId());
    auto sessions   = m_db.listSessions();
    m_statsLabel->setText(QString("Sessions: %1  |  Total frames: %2  |  Session #%3: %4 frames")
        .arg(sessions.size())
        .arg(total)
        .arg(m_db.currentSessionId())
        .arg(session));
}
