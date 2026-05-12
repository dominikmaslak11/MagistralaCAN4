#include "SomeIpWidget.h"
#include "SomeIpParser.h"
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSplitter>
#include <QVBoxLayout>
#include <iomanip>
#include <sstream>

SomeIpWidget::SomeIpWidget(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    // ── Hex input row ──────────────────────────────────────────────────────────
    auto *inputGroup = new QGroupBox("Parsuj SOME/IP (hex payload)");
    auto *inputLayout = new QHBoxLayout(inputGroup);
    m_hexInput = new QLineEdit;
    m_hexInput->setPlaceholderText("np. FFFF 8100 00000014 0000 0001 01 01 02 00 ...");
    m_hexInput->setFont(QFont("Courier New", 9));
    m_parseBtn = new QPushButton("Parsuj");
    m_clearBtn = new QPushButton("Wyczyść");
    inputLayout->addWidget(m_hexInput, 1);
    inputLayout->addWidget(m_parseBtn);
    inputLayout->addWidget(m_clearBtn);
    layout->addWidget(inputGroup);

    // ── Splitter: message table + detail view ──────────────────────────────────
    auto *splitter = new QSplitter(Qt::Vertical);

    m_msgTable = new QTableWidget(0, 8);
    m_msgTable->setHorizontalHeaderLabels({"Service ID", "Method ID", "Typ",
                                           "Client ID", "Session ID",
                                           "Return Code", "Bajty", "Opis"});
    m_msgTable->horizontalHeader()->setStretchLastSection(true);
    m_msgTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_msgTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_msgTable->setAlternatingRowColors(true);
    m_msgTable->verticalHeader()->setVisible(false);
    splitter->addWidget(m_msgTable);

    m_detailView = new QTextEdit;
    m_detailView->setReadOnly(true);
    m_detailView->setFont(QFont("Courier New", 9));
    m_detailView->setPlaceholderText("Wybierz wiersz, aby zobaczyć szczegóły...");
    splitter->addWidget(m_detailView);
    splitter->setSizes({320, 200});

    layout->addWidget(splitter, 1);

    m_statusLabel = new QLabel("Gotowy — wklej hex lub dostarcz payload przez processPayload()");
    layout->addWidget(m_statusLabel);

    connect(m_parseBtn, &QPushButton::clicked, this, &SomeIpWidget::parseManualInput);
    connect(m_clearBtn, &QPushButton::clicked, this, &SomeIpWidget::clearMessages);
    connect(m_hexInput, &QLineEdit::returnPressed, this, &SomeIpWidget::parseManualInput);
    connect(m_msgTable, &QTableWidget::currentRowChanged, this, &SomeIpWidget::updateDetails);
}

// ── Slots ──────────────────────────────────────────────────────────────────────

void SomeIpWidget::processPayload(const QByteArray &data) {
    std::vector<uint8_t> bytes(data.begin(), data.end());
    addMessage(SomeIpParser::parse(bytes));
    m_statusLabel->setText(QString("Odebrano payload: %1 B | łącznie: %2")
                           .arg(data.size()).arg(m_messages.size()));
}

void SomeIpWidget::parseManualInput() {
    QString hex = m_hexInput->text().simplified().remove(' ').remove(':').remove('-');
    if (hex.isEmpty()) return;

    std::vector<uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (int i = 0; i + 1 < hex.size(); i += 2) {
        bool ok;
        uint8_t b = static_cast<uint8_t>(hex.mid(i, 2).toUInt(&ok, 16));
        if (!ok) {
            m_statusLabel->setText("Błąd: nieprawidłowy hex na pozycji " + QString::number(i));
            return;
        }
        bytes.push_back(b);
    }
    addMessage(SomeIpParser::parse(bytes));
}

void SomeIpWidget::clearMessages() {
    m_msgTable->setRowCount(0);
    m_messages.clear();
    m_detailView->clear();
    m_statusLabel->setText("Wyczyszczono");
}

// ── Private helpers ────────────────────────────────────────────────────────────

static QString previewHex(const std::vector<uint8_t> &v, size_t maxBytes = 12) {
    QString s;
    for (size_t i = 0; i < v.size() && i < maxBytes; ++i)
        s += QString("%1 ").arg(v[i], 2, 16, QChar('0')).toUpper();
    if (v.size() > maxBytes) s += "...";
    return s.trimmed();
}

void SomeIpWidget::addMessage(const SomeIpMessage &msg) {
    m_messages.push_back(msg);
    int row = m_msgTable->rowCount();
    m_msgTable->insertRow(row);

    auto col = [&](int c, const QString &t) {
        m_msgTable->setItem(row, c, new QTableWidgetItem(t));
    };

    col(0, QString("0x%1").arg(msg.header.serviceId, 4, 16, QChar('0')).toUpper());
    col(1, QString("0x%1").arg(msg.header.methodId,  4, 16, QChar('0')).toUpper());
    col(2, QString::fromStdString(SomeIpParser::messageTypeName(msg.header.messageType)));
    col(3, QString("0x%1").arg(msg.header.clientId,  4, 16, QChar('0')).toUpper());
    col(4, QString("0x%1").arg(msg.header.sessionId, 4, 16, QChar('0')).toUpper());
    col(5, QString::fromStdString(SomeIpParser::returnCodeName(msg.header.returnCode)));
    col(6, QString::number(msg.payload.size()));

    QString desc;
    if (!msg.valid)
        desc = "BŁĄD: " + QString::fromStdString(msg.error);
    else if (msg.isServiceDiscovery)
        desc = QString("SOME/IP-SD (%1 wpisów)").arg(msg.sdEntries.size());
    else if (msg.header.serviceId == SOMEIP_MAGIC_SERVICE_ID)
        desc = "Magic Cookie";
    else
        desc = previewHex(msg.payload);

    m_msgTable->setItem(row, 7, new QTableWidgetItem(desc));

    if (!msg.valid) {
        for (int c = 0; c < 8; ++c)
            if (m_msgTable->item(row, c))
                m_msgTable->item(row, c)->setForeground(Qt::red);
    } else if (msg.isServiceDiscovery) {
        m_msgTable->item(row, 2)->setForeground(QColor(0x44, 0xAA, 0xFF));
    }

    m_msgTable->scrollToBottom();
    m_statusLabel->setText(QString("Wiadomości: %1").arg(m_messages.size()));
}

void SomeIpWidget::updateDetails(int row) {
    if (row < 0 || row >= static_cast<int>(m_messages.size())) return;
    const auto &msg = m_messages[static_cast<size_t>(row)];

    std::ostringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');
    ss << "=== SOME/IP Header ===\n";
    ss << "Service ID       : 0x" << std::setw(4) << msg.header.serviceId << "\n";
    ss << "Method ID        : 0x" << std::setw(4) << msg.header.methodId  << "\n";
    ss << "Length           : " << std::dec << msg.header.length << " (payload+" << SOMEIP_MIN_LENGTH_FIELD << ")\n";
    ss << "Client ID        : 0x" << std::hex << std::setw(4) << msg.header.clientId  << "\n";
    ss << "Session ID       : 0x" << std::setw(4) << msg.header.sessionId << "\n";
    ss << "Protocol Version : " << std::dec << (int)msg.header.protocolVersion  << "\n";
    ss << "Interface Version: " << (int)msg.header.interfaceVersion << "\n";
    ss << "Message Type     : 0x" << std::hex << std::setw(2) << (int)msg.header.messageType
       << " (" << SomeIpParser::messageTypeName(msg.header.messageType) << ")\n";
    ss << "Return Code      : 0x" << std::setw(2) << (int)msg.header.returnCode
       << " (" << SomeIpParser::returnCodeName(msg.header.returnCode) << ")\n";

    if (!msg.valid) {
        ss << "\n[BŁĄD] " << msg.error << "\n";
        m_detailView->setPlainText(QString::fromStdString(ss.str()));
        return;
    }

    if (msg.isServiceDiscovery) {
        ss << "\n=== SOME/IP-SD Entries (" << msg.sdEntries.size() << ") ===\n";
        for (size_t i = 0; i < msg.sdEntries.size(); ++i) {
            const auto &e = msg.sdEntries[i];
            ss << "\nEntry[" << i << "]: " << SomeIpParser::sdEntryTypeName(e.type) << "\n";
            ss << "  Service ID  : 0x" << std::hex << std::setw(4) << e.serviceId  << "\n";
            ss << "  Instance ID : 0x" << std::setw(4) << e.instanceId << "\n";
            ss << "  Major Ver   : " << std::dec << (int)e.majorVersion << "\n";
            ss << "  TTL         : " << e.ttl << " s"
               << (e.ttl == 0 ? " (stop/unsubscribe)" : "") << "\n";
            ss << "  Minor/EG ID : 0x" << std::hex << std::setw(8) << e.minorVersion << "\n";
        }
    } else {
        ss << "\n=== Payload (" << std::dec << msg.payload.size() << " bytes) ===\n";
        for (size_t i = 0; i < msg.payload.size(); ++i) {
            ss << std::hex << std::setw(2) << (int)msg.payload[i];
            ss << ((i % 16 == 15) ? "\n" : " ");
        }
        if (!msg.payload.empty() && msg.payload.size() % 16 != 0) ss << "\n";
    }

    m_detailView->setPlainText(QString::fromStdString(ss.str()));
}
