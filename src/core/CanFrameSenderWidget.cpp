#include "CanFrameSenderWidget.h"
#include "CanSniffer.h"
#include "DbcParser.h"
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QVBoxLayout>
#include <sstream>
#include <iomanip>
#include <ctime>

CanFrameSenderWidget::CanFrameSenderWidget(CanSniffer *sniffer, QWidget *parent)
    : QWidget(parent), m_sniffer(sniffer) {
    buildUi();
}

void CanFrameSenderWidget::setDbcParser(const DbcParser *dbc) {
    m_dbc = dbc;

    m_dbcMsgCombo->clear();
    m_dbcMsgCombo->addItem("— wybierz z DBC —");
    if (!dbc) return;
    for (const auto &msg : dbc->messages()) {
        m_dbcMsgCombo->addItem(
            QString("0x%1  %2").arg(msg.id, 3, 16, QChar('0')).toUpper().arg(msg.name),
            msg.id);
    }
}

// ── UI ────────────────────────────────────────────────────────────────────────

void CanFrameSenderWidget::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    // ── Frame builder ──────────────────────────────────────────────────────────
    auto *frameGroup  = new QGroupBox("Konstruktor ramki CAN");
    auto *frameLayout = new QVBoxLayout(frameGroup);

    // ID row
    auto *idRow = new QHBoxLayout;
    idRow->addWidget(new QLabel("CAN ID (hex):"));
    m_idEdit = new QLineEdit("100");
    m_idEdit->setMaximumWidth(80);
    m_idEdit->setInputMask("HHHHH");
    idRow->addWidget(m_idEdit);

    m_extCheck = new QCheckBox("29-bit (Extended)");
    m_fdCheck  = new QCheckBox("CAN FD");
    idRow->addWidget(m_extCheck);
    idRow->addWidget(m_fdCheck);
    idRow->addStretch(1);
    frameLayout->addLayout(idRow);

    // DLC + bytes row
    auto *dlcRow = new QHBoxLayout;
    dlcRow->addWidget(new QLabel("DLC:"));
    m_dlcSpin = new QSpinBox;
    m_dlcSpin->setRange(0, 8);
    m_dlcSpin->setValue(8);
    dlcRow->addWidget(m_dlcSpin);
    dlcRow->addWidget(new QLabel("Dane (hex):"));

    for (int i = 0; i < 8; ++i) {
        m_byteEdits[i] = new QLineEdit("00");
        m_byteEdits[i]->setMaximumWidth(34);
        m_byteEdits[i]->setInputMask("HH");
        dlcRow->addWidget(m_byteEdits[i]);
    }
    dlcRow->addStretch(1);
    frameLayout->addLayout(dlcRow);

    // Load from DBC
    auto *dbcRow = new QHBoxLayout;
    dbcRow->addWidget(new QLabel("Wczytaj z DBC:"));
    m_dbcMsgCombo = new QComboBox;
    m_dbcMsgCombo->setMinimumWidth(260);
    m_dbcMsgCombo->addItem("— wybierz z DBC —");
    dbcRow->addWidget(m_dbcMsgCombo);
    dbcRow->addStretch(1);
    frameLayout->addLayout(dbcRow);

    root->addWidget(frameGroup);

    // ── Send controls ──────────────────────────────────────────────────────────
    auto *sendGroup  = new QGroupBox("Wysyłanie");
    auto *sendLayout = new QHBoxLayout(sendGroup);

    auto *sendOnceBtn = new QPushButton("Wyślij jednorazowo");
    sendOnceBtn->setStyleSheet("font-weight: bold; color: #44ff88;");

    sendLayout->addWidget(new QLabel("Okres [ms]:"));
    m_periodSpin = new QSpinBox;
    m_periodSpin->setRange(1, 60000);
    m_periodSpin->setValue(100);
    sendLayout->addWidget(m_periodSpin);

    m_startPeriodicBtn = new QPushButton("Start cykliczny");
    m_startPeriodicBtn->setStyleSheet("color: #44aaff;");
    m_stopPeriodicBtn  = new QPushButton("Stop");
    m_stopPeriodicBtn->setEnabled(false);

    m_sentCountLabel = new QLabel("Wysłano: 0");
    m_statusLabel    = new QLabel;
    m_statusLabel->setStyleSheet("color: #aaaaaa; font-size: 11px;");

    sendLayout->addWidget(sendOnceBtn);
    sendLayout->addWidget(m_startPeriodicBtn);
    sendLayout->addWidget(m_stopPeriodicBtn);
    sendLayout->addWidget(m_sentCountLabel);
    sendLayout->addStretch(1);
    sendLayout->addWidget(m_statusLabel);
    root->addWidget(sendGroup);

    // ── History ────────────────────────────────────────────────────────────────
    auto *histGroup  = new QGroupBox("Historia wysłanych ramek");
    auto *histLayout = new QVBoxLayout(histGroup);

    m_historyTable = new QTableWidget(0, 5);
    m_historyTable->setHorizontalHeaderLabels({"Czas", "CAN ID", "DLC", "Dane", "Typ"});
    m_historyTable->horizontalHeader()->setStretchLastSection(true);
    m_historyTable->verticalHeader()->setVisible(false);
    m_historyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_historyTable->setAlternatingRowColors(true);
    m_historyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_historyTable->setMaximumHeight(200);
    histLayout->addWidget(m_historyTable);

    auto *clearHistBtn = new QPushButton("Wyczyść historię");
    histLayout->addWidget(clearHistBtn);

    root->addWidget(histGroup, 1);

    // Connections
    connect(sendOnceBtn, &QPushButton::clicked, this, &CanFrameSenderWidget::sendOnce);
    connect(m_startPeriodicBtn, &QPushButton::clicked, this, &CanFrameSenderWidget::startPeriodic);
    connect(m_stopPeriodicBtn,  &QPushButton::clicked, this, &CanFrameSenderWidget::stopPeriodic);
    connect(m_dlcSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &CanFrameSenderWidget::onDlcChanged);
    connect(m_dbcMsgCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CanFrameSenderWidget::onLoadFromDbc);
    connect(m_historyTable, &QTableWidget::cellClicked,
            this, &CanFrameSenderWidget::onHistorySelected);
    connect(clearHistBtn, &QPushButton::clicked, this,
            [this]() { m_historyTable->setRowCount(0); });
    connect(m_fdCheck, &QCheckBox::toggled, this, [this](bool fd) {
        m_dlcSpin->setRange(0, fd ? 64 : 8);
    });

    m_periodicTimer = new QTimer(this);
    connect(m_periodicTimer, &QTimer::timeout, this, &CanFrameSenderWidget::onPeriodicTick);

    // Init: grey out bytes beyond DLC
    onDlcChanged(8);
}

// ── Frame building ────────────────────────────────────────────────────────────

CanFrame CanFrameSenderWidget::buildFrame() const {
    CanFrame f{};
    bool ok;
    f.id       = m_idEdit->text().toUInt(&ok, 16);
    f.extended = m_extCheck->isChecked();
    f.fd       = m_fdCheck->isChecked();
    f.dlc      = static_cast<uint8_t>(m_dlcSpin->value());
    for (int i = 0; i < 8 && i < f.dlc; ++i)
        f.data[i] = static_cast<uint8_t>(m_byteEdits[i]->text().toUInt(nullptr, 16));
    f.timestamp = 0; // driver fills this
    return f;
}

bool CanFrameSenderWidget::validateInput() {
    bool ok;
    uint32_t id = m_idEdit->text().toUInt(&ok, 16);
    if (!ok) { m_statusLabel->setText("Błąd: nieprawidłowy CAN ID"); return false; }
    uint32_t maxId = m_extCheck->isChecked() ? 0x1FFFFFFF : 0x7FF;
    if (id > maxId) { m_statusLabel->setText("Błąd: CAN ID poza zakresem"); return false; }
    return true;
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void CanFrameSenderWidget::sendOnce() {
    if (!validateInput()) return;
    if (!m_sniffer) { m_statusLabel->setText("Brak sterownika CAN"); return; }

    CanFrame f = buildFrame();
    m_sniffer->writeFrame(f);
    ++m_sentCount;
    m_sentCountLabel->setText(QString("Wysłano: %1").arg(m_sentCount));
    m_statusLabel->setText("OK");
    addToHistory(f);
}

void CanFrameSenderWidget::startPeriodic() {
    if (!validateInput()) return;
    m_periodicTimer->start(m_periodSpin->value());
    m_startPeriodicBtn->setEnabled(false);
    m_stopPeriodicBtn->setEnabled(true);
    m_periodSpin->setEnabled(false);
    m_statusLabel->setText(QString("Cykliczne @ %1 ms").arg(m_periodSpin->value()));
}

void CanFrameSenderWidget::stopPeriodic() {
    m_periodicTimer->stop();
    m_startPeriodicBtn->setEnabled(true);
    m_stopPeriodicBtn->setEnabled(false);
    m_periodSpin->setEnabled(true);
    m_statusLabel->setText("Zatrzymano");
}

void CanFrameSenderWidget::onPeriodicTick() {
    if (!m_sniffer) return;
    CanFrame f = buildFrame();
    m_sniffer->writeFrame(f);
    ++m_sentCount;
    m_sentCountLabel->setText(QString("Wysłano: %1").arg(m_sentCount));

    // Only log every 10th frame to avoid flooding the table
    if (m_sentCount % 10 == 0)
        addToHistory(f);
}

void CanFrameSenderWidget::onDlcChanged(int dlc) {
    for (int i = 0; i < 8; ++i)
        m_byteEdits[i]->setEnabled(i < dlc);
}

void CanFrameSenderWidget::onLoadFromDbc(int index) {
    if (index <= 0 || !m_dbc) return;
    uint32_t id = m_dbcMsgCombo->itemData(index).toUInt();

    for (const auto &msg : m_dbc->messages()) {
        if (msg.id != id) continue;
        m_idEdit->setText(QString("%1").arg(id, 5, 16, QChar('0')).toUpper());
        m_extCheck->setChecked(id > 0x7FF);
        m_dlcSpin->setValue(msg.dlc);
        // Fill zeros
        for (int i = 0; i < 8; ++i) m_byteEdits[i]->setText("00");
        break;
    }
}

// ── History ───────────────────────────────────────────────────────────────────

void CanFrameSenderWidget::addToHistory(const CanFrame &frame) {
    // Cap history
    if (m_historyTable->rowCount() >= kMaxHistory)
        m_historyTable->removeRow(m_historyTable->rowCount() - 1);

    m_historyTable->insertRow(0);

    auto now = std::time(nullptr);
    char timeBuf[16];
    std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", std::localtime(&now));

    // Build hex data string
    std::ostringstream data;
    data << std::hex << std::uppercase << std::setfill('0');
    for (int i = 0; i < frame.dlc; ++i) {
        if (i) data << ' ';
        data << std::setw(2) << (int)frame.data[i];
    }

    m_historyTable->setItem(0, 0, new QTableWidgetItem(timeBuf));
    m_historyTable->setItem(0, 1, new QTableWidgetItem(
        QString("0x%1").arg(frame.id, 3, 16, QChar('0')).toUpper()));
    m_historyTable->setItem(0, 2, new QTableWidgetItem(QString::number(frame.dlc)));
    m_historyTable->setItem(0, 3, new QTableWidgetItem(QString::fromStdString(data.str())));
    m_historyTable->setItem(0, 4, new QTableWidgetItem(
        frame.fd ? "FD" : (frame.extended ? "EXT" : "STD")));
}

void CanFrameSenderWidget::onHistorySelected(int row, int) {
    // Re-populate the editor from the clicked history row
    auto *idItem   = m_historyTable->item(row, 1);
    auto *dlcItem  = m_historyTable->item(row, 2);
    auto *dataItem = m_historyTable->item(row, 3);
    auto *typeItem = m_historyTable->item(row, 4);
    if (!idItem || !dlcItem || !dataItem || !typeItem) return;

    QString idStr = idItem->text().mid(2); // strip "0x"
    m_idEdit->setText(idStr);

    int dlc = dlcItem->text().toInt();
    m_dlcSpin->setValue(dlc);

    m_extCheck->setChecked(typeItem->text().contains("EXT") || typeItem->text().contains("FD"));
    m_fdCheck->setChecked(typeItem->text() == "FD");

    QStringList bytes = dataItem->text().split(' ', Qt::SkipEmptyParts);
    for (int i = 0; i < 8; ++i)
        m_byteEdits[i]->setText(i < bytes.size() ? bytes[i] : "00");
}
