#include "DoIpWidget.h"
#include "DoIpParser.h"
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSplitter>
#include <QVBoxLayout>
#include <iomanip>
#include <sstream>

DoIpWidget::DoIpWidget(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    // ── Hex input ──────────────────────────────────────────────────────────────
    auto *inputGroup  = new QGroupBox("Parsuj DoIP (ISO 13400-2) — hex payload");
    auto *inputLayout = new QHBoxLayout(inputGroup);
    m_hexInput = new QLineEdit;
    m_hexInput->setPlaceholderText("02 FD 80 01 00 00 00 06 0E 80 10 01 00 00 00 00...");
    m_hexInput->setFont(QFont("Courier New", 9));
    m_parseBtn = new QPushButton("Parsuj");
    m_clearBtn = new QPushButton("Wyczyść");
    inputLayout->addWidget(m_hexInput, 1);
    inputLayout->addWidget(m_parseBtn);
    inputLayout->addWidget(m_clearBtn);
    layout->addWidget(inputGroup);

    // ── Splitter: table | detail ───────────────────────────────────────────────
    auto *splitter = new QSplitter(Qt::Vertical);

    m_msgTable = new QTableWidget(0, 5);
    m_msgTable->setHorizontalHeaderLabels({"Proto V", "Typ komunikatu", "Rozmiar", "Src→Dst", "Szczegóły"});
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
    splitter->setSizes({280, 200});

    layout->addWidget(splitter, 1);

    m_statusLabel = new QLabel("Gotowy — wklej hex lub dostarcz payload przez processPayload()");
    layout->addWidget(m_statusLabel);

    connect(m_parseBtn, &QPushButton::clicked, this, &DoIpWidget::parseManualInput);
    connect(m_clearBtn, &QPushButton::clicked, this, &DoIpWidget::clearMessages);
    connect(m_hexInput, &QLineEdit::returnPressed, this, &DoIpWidget::parseManualInput);
    connect(m_msgTable, &QTableWidget::currentCellChanged,
            this, [this](int row, int, int, int) { updateDetails(row); });
}

// ── Slots ──────────────────────────────────────────────────────────────────────

void DoIpWidget::processPayload(const QByteArray &data) {
    std::vector<uint8_t> bytes(data.begin(), data.end());
    addMessage(DoIpParser::parse(bytes));
    m_statusLabel->setText(QString("Odebrano: %1 B | łącznie: %2 wiad.")
                           .arg(data.size()).arg(m_messages.size()));
}

void DoIpWidget::parseManualInput() {
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
    addMessage(DoIpParser::parse(bytes));
}

void DoIpWidget::clearMessages() {
    m_msgTable->setRowCount(0);
    m_messages.clear();
    m_detailView->clear();
    m_statusLabel->setText("Wyczyszczono");
}

// ── Private helpers ────────────────────────────────────────────────────────────

void DoIpWidget::addMessage(const DoIpMessage &msg) {
    m_messages.push_back(msg);
    int row = m_msgTable->rowCount();
    m_msgTable->insertRow(row);

    auto col = [&](int c, const QString &t) {
        m_msgTable->setItem(row, c, new QTableWidgetItem(t));
    };

    col(0, QString("0x%1").arg(msg.header.protocolVersion, 2, 16, QChar('0')).toUpper());
    col(1, QString::fromStdString(DoIpParser::payloadTypeName(msg.header.payloadType)));
    col(2, QString::number(msg.header.payloadLength) + " B");

    // Src→Dst or error
    QString srcdst;
    if (!msg.valid) {
        srcdst = "—";
    } else if (msg.type == DoIpPayloadType::DiagnosticMessage ||
               msg.type == DoIpPayloadType::DiagnosticMessageAck ||
               msg.type == DoIpPayloadType::DiagnosticMessageNack) {
        uint16_t src = (msg.type == DoIpPayloadType::DiagnosticMessage)
                        ? msg.diagMsg.sourceAddr : msg.diagAck.sourceAddr;
        uint16_t dst = (msg.type == DoIpPayloadType::DiagnosticMessage)
                        ? msg.diagMsg.targetAddr : msg.diagAck.targetAddr;
        srcdst = QString("0x%1→0x%2")
                 .arg(src, 4, 16, QChar('0')).toUpper()
                 .arg(dst, 4, 16, QChar('0')).toUpper();
    } else if (msg.type == DoIpPayloadType::VehicleAnnouncement) {
        srcdst = QString::fromStdString(msg.vehicleAnnouncement.vin);
    } else if (msg.type == DoIpPayloadType::RoutingActivationResp) {
        srcdst = QString("0x%1").arg(msg.routingResp.entityLogicalAddr, 4, 16, QChar('0')).toUpper();
    }
    col(3, srcdst);

    // Details summary
    QString details;
    if (!msg.valid)
        details = "BŁĄD: " + QString::fromStdString(msg.error);
    else if (!msg.header.validPattern)
        details = "Nieprawidłowy wzorzec nagłówka";
    else if (msg.type == DoIpPayloadType::DiagnosticMessage && !msg.diagMsg.udsPayload.empty())
        details = QString("UDS: %1 B").arg(msg.diagMsg.udsPayload.size());
    else if (msg.type == DoIpPayloadType::RoutingActivationResp)
        details = QString::fromStdString(
            DoIpParser::routingResponseCodeName(msg.routingResp.responseCode));

    col(4, details);

    if (!msg.valid)
        for (int c = 0; c < 5; ++c)
            if (m_msgTable->item(row, c)) m_msgTable->item(row, c)->setForeground(Qt::red);
    else if (msg.type == DoIpPayloadType::DiagnosticMessage)
        m_msgTable->item(row, 1)->setForeground(QColor(0x44, 0xAA, 0xFF));

    m_msgTable->scrollToBottom();
    m_statusLabel->setText(QString("Wiadomości: %1").arg(m_messages.size()));
}

void DoIpWidget::updateDetails(int row) {
    if (row < 0 || row >= static_cast<int>(m_messages.size())) return;
    const auto &msg = m_messages[static_cast<size_t>(row)];

    std::ostringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');
    ss << "=== DoIP Header (ISO 13400-2) ===\n";
    ss << "Protocol Version     : 0x" << std::setw(2) << static_cast<int>(msg.header.protocolVersion)
       << (msg.header.protocolVersion == DOIP_PROTO_VERSION ? " (2019)" : " (!= 0x02)") << "\n";
    ss << "Inv. Proto Version   : 0x" << std::setw(2) << static_cast<int>(msg.header.inverseProtoVersion) << "\n";
    ss << "Header pattern valid : " << (msg.header.validPattern ? "YES" : "NO") << "\n";
    ss << "Payload Type         : 0x" << std::setw(4) << msg.header.payloadType
       << " (" << DoIpParser::payloadTypeName(msg.header.payloadType) << ")\n";
    ss << "Payload Length       : " << std::dec << msg.header.payloadLength << " bytes\n";

    if (!msg.valid) {
        ss << "\n[BŁĄD] " << msg.error << "\n";
        m_detailView->setPlainText(QString::fromStdString(ss.str()));
        return;
    }

    switch (msg.type) {
        case DoIpPayloadType::VehicleAnnouncement: {
            const auto &v = msg.vehicleAnnouncement;
            ss << "\n=== Vehicle Announcement ===\n";
            ss << "VIN          : " << v.vin << "\n";
            ss << "Logical Addr : 0x" << std::hex << std::setw(4) << v.logicalAddr << "\n";
            ss << "EID (MAC)    : ";
            for (int i = 0; i < 6; ++i)
                ss << std::setw(2) << static_cast<int>(v.eid[i]) << (i < 5 ? ":" : "");
            ss << "\nGID          : ";
            for (int i = 0; i < 6; ++i)
                ss << std::setw(2) << static_cast<int>(v.gid[i]) << (i < 5 ? ":" : "");
            ss << "\nFurther Action: 0x" << std::setw(2) << static_cast<int>(v.furtherAction) << "\n";
            if (v.hasSyncStatus)
                ss << "Sync Status  : 0x" << std::setw(2) << static_cast<int>(v.syncStatus) << "\n";
            break;
        }
        case DoIpPayloadType::RoutingActivationReq: {
            const auto &r = msg.routingReq;
            ss << "\n=== Routing Activation Request ===\n";
            ss << "Source Addr      : 0x" << std::hex << std::setw(4) << r.sourceAddr << "\n";
            ss << "Activation Type  : 0x" << std::setw(2) << static_cast<int>(r.activationType)
               << (r.activationType == 0 ? " (Default)" : (r.activationType == 1 ? " (WWH-OBD)" : "")) << "\n";
            break;
        }
        case DoIpPayloadType::RoutingActivationResp: {
            const auto &r = msg.routingResp;
            ss << "\n=== Routing Activation Response ===\n";
            ss << "Client Logical Addr : 0x" << std::hex << std::setw(4) << r.clientLogicalAddr << "\n";
            ss << "Entity Logical Addr : 0x" << std::setw(4) << r.entityLogicalAddr << "\n";
            ss << "Response Code       : 0x" << std::setw(2) << static_cast<int>(r.responseCode)
               << " (" << DoIpParser::routingResponseCodeName(r.responseCode) << ")\n";
            break;
        }
        case DoIpPayloadType::DiagnosticMessage: {
            const auto &d = msg.diagMsg;
            ss << "\n=== Diagnostic Message (UDS) ===\n";
            ss << "Source Addr : 0x" << std::hex << std::setw(4) << d.sourceAddr << "\n";
            ss << "Target Addr : 0x" << std::setw(4) << d.targetAddr << "\n";
            ss << "UDS Payload : " << std::dec << d.udsPayload.size() << " bytes\n";
            for (size_t i = 0; i < d.udsPayload.size(); ++i) {
                ss << std::hex << std::setw(2) << static_cast<int>(d.udsPayload[i]);
                ss << ((i % 16 == 15) ? "\n" : " ");
            }
            if (!d.udsPayload.empty() && d.udsPayload.size() % 16 != 0) ss << "\n";
            break;
        }
        case DoIpPayloadType::DiagnosticMessageAck:
        case DoIpPayloadType::DiagnosticMessageNack: {
            const auto &a = msg.diagAck;
            ss << "\n=== Diagnostic Message "
               << (msg.type == DoIpPayloadType::DiagnosticMessageAck ? "ACK" : "NACK") << " ===\n";
            ss << "Source Addr : 0x" << std::hex << std::setw(4) << a.sourceAddr << "\n";
            ss << "Target Addr : 0x" << std::setw(4) << a.targetAddr << "\n";
            ss << "Code        : 0x" << std::setw(2) << static_cast<int>(a.ackCode)
               << " (" << DoIpParser::diagAckCodeName(a.ackCode) << ")\n";
            break;
        }
        default: {
            ss << "\n=== Raw Payload (" << std::dec << msg.rawPayload.size() << " bytes) ===\n";
            for (size_t i = 0; i < msg.rawPayload.size(); ++i) {
                ss << std::hex << std::setw(2) << static_cast<int>(msg.rawPayload[i]);
                ss << ((i % 16 == 15) ? "\n" : " ");
            }
            if (!msg.rawPayload.empty() && msg.rawPayload.size() % 16 != 0) ss << "\n";
            break;
        }
    }

    m_detailView->setPlainText(QString::fromStdString(ss.str()));
}
