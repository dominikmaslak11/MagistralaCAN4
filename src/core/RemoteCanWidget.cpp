#include "RemoteCanWidget.h"
#include "CanSniffer.h"
#include "Logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QDateTime>
#include <QTime>

RemoteCanWidget::RemoteCanWidget(CanSniffer *sniffer, QWidget *parent)
    : QWidget(parent), m_sniffer(sniffer) {
    setupUi();
}

void RemoteCanWidget::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);

    // ═══ Panel serwera ═══
    auto *serverGroup = new QGroupBox("Serwer WSS (wysyłanie ramek)");
    serverGroup->setStyleSheet(
        "QGroupBox { color: #00ffaa; font-weight: bold; border: 1px solid #e94560; "
        "border-radius: 4px; margin-top: 8px; padding-top: 16px; }");
    auto *serverLayout = new QVBoxLayout(serverGroup);

    auto *serverRow = new QHBoxLayout;
    serverRow->addWidget(new QLabel("Port:"));
    m_serverPort = new QSpinBox; m_serverPort->setRange(1024, 65535); m_serverPort->setValue(9001);
    serverRow->addWidget(m_serverPort);
    m_serverBtn = new QPushButton("Uruchom serwer WSS");
    serverRow->addWidget(m_serverBtn);
    serverRow->addStretch();
    serverLayout->addLayout(serverRow);

    auto *tokenRow = new QHBoxLayout;
    tokenRow->addWidget(new QLabel("Token:"));
    m_tokenLabel = new QLabel(m_server.token());
    m_tokenLabel->setStyleSheet("color: #ffaa00; font-family: Consolas, monospace; "
                                "background: #1a1a2e; padding: 4px 8px; border-radius: 4px;");
    m_tokenLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    tokenRow->addWidget(m_tokenLabel, 1);
    m_copyTokenBtn = new QPushButton("Kopiuj");
    m_copyTokenBtn->setFixedWidth(80);
    tokenRow->addWidget(m_copyTokenBtn);
    serverLayout->addLayout(tokenRow);

    m_serverStatus = new QLabel("Serwer: zatrzymany");
    serverLayout->addWidget(m_serverStatus);
    mainLayout->addWidget(serverGroup);

    // ═══ Panel klienta ═══
    auto *clientGroup = new QGroupBox("Klient zdalny (odbieranie ramek)");
    clientGroup->setStyleSheet(
        "QGroupBox { color: #ff66cc; font-weight: bold; border: 1px solid #e94560; "
        "border-radius: 4px; margin-top: 8px; padding-top: 16px; }");
    auto *clientLayout = new QVBoxLayout(clientGroup);

    auto *urlRow = new QHBoxLayout;
    urlRow->addWidget(new QLabel("URL serwera:"));
    m_clientUrl = new QLineEdit; m_clientUrl->setPlaceholderText("wss://192.168.1.10:9001");
    urlRow->addWidget(m_clientUrl, 1);
    clientLayout->addLayout(urlRow);

    auto *tokenClientRow = new QHBoxLayout;
    tokenClientRow->addWidget(new QLabel("Token:"));
    m_clientToken = new QLineEdit;
    m_clientToken->setPlaceholderText("64-znakowy hex");
    m_clientToken->setEchoMode(QLineEdit::Password);
    tokenClientRow->addWidget(m_clientToken, 1);
    auto *showTokenBtn = new QPushButton("👁");
    showTokenBtn->setFixedWidth(40);
    showTokenBtn->setCheckable(true);
    connect(showTokenBtn, &QPushButton::toggled, this, [this](bool checked) {
        m_clientToken->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
    });
    tokenClientRow->addWidget(showTokenBtn);
    clientLayout->addLayout(tokenClientRow);

    auto *clientBtnRow = new QHBoxLayout;
    m_clientBtn = new QPushButton("Połącz ze zdalnym CAN");
    m_clientBtn->setStyleSheet("QPushButton { color: #ff66cc; }");
    clientBtnRow->addWidget(m_clientBtn);
    clientBtnRow->addStretch();
    clientLayout->addLayout(clientBtnRow);

    m_clientStatus = new QLabel("Klient: rozłączony");
    m_frameCountLabel = new QLabel("Odebrane ramki: 0");
    m_frameCountLabel->setStyleSheet("color: #ffaa00;");
    clientLayout->addWidget(m_clientStatus);
    clientLayout->addWidget(m_frameCountLabel);
    mainLayout->addWidget(clientGroup);

    // ═══ Panel wysyłania ramek ═══
    auto *sendGroup = new QGroupBox("Wyślij ramkę CAN (przez klienta)");
    sendGroup->setStyleSheet(
        "QGroupBox { color: #ffaa00; font-weight: bold; border: 1px solid #e94560; "
        "border-radius: 4px; margin-top: 8px; padding-top: 16px; }");
    auto *sendLayout = new QVBoxLayout(sendGroup);

    // Wiersz: CAN ID + flagi
    auto *idRow = new QHBoxLayout;
    idRow->addWidget(new QLabel("CAN ID (hex):"));
    m_sendIdEdit = new QLineEdit;
    m_sendIdEdit->setPlaceholderText("7DF");
    m_sendIdEdit->setMaximumWidth(90);
    idRow->addWidget(m_sendIdEdit);
    idRow->addSpacing(12);
    m_sendExtended = new QCheckBox("Extended (29-bit)");
    m_sendRtr      = new QCheckBox("RTR");
    m_sendFd       = new QCheckBox("CAN FD");
    idRow->addWidget(m_sendExtended);
    idRow->addWidget(m_sendRtr);
    idRow->addWidget(m_sendFd);
    idRow->addStretch();
    sendLayout->addLayout(idRow);

    // Wiersz: dane
    auto *dataRow = new QHBoxLayout;
    dataRow->addWidget(new QLabel("Dane (hex, spacje):"));
    m_sendDataEdit = new QLineEdit;
    m_sendDataEdit->setPlaceholderText("02 01 0C 00 00 00 00 00");
    dataRow->addWidget(m_sendDataEdit, 1);
    sendLayout->addLayout(dataRow);

    // Przycisk wyślij + status
    auto *sendBtnRow = new QHBoxLayout;
    m_sendBtn = new QPushButton("→ Wyślij ramkę");
    m_sendBtn->setStyleSheet("QPushButton { color: #ffaa00; border-color: #ffaa00; }");
    m_sendBtn->setEnabled(false);
    sendBtnRow->addWidget(m_sendBtn);
    m_sendStatusLabel = new QLabel("—");
    m_sendStatusLabel->setStyleSheet("color: #888;");
    sendBtnRow->addWidget(m_sendStatusLabel, 1);
    sendLayout->addLayout(sendBtnRow);

    // Historia ostatnich wysłanych ramek
    sendLayout->addWidget(new QLabel("Historia wysłanych:"));
    m_sendHistory = new QListWidget;
    m_sendHistory->setMaximumHeight(110);
    m_sendHistory->setStyleSheet(
        "QListWidget { background: #0a0e17; color: #ffaa00; "
        "border: 1px solid #333; font-family: Consolas, monospace; font-size: 12px; }");
    sendLayout->addWidget(m_sendHistory);
    mainLayout->addWidget(sendGroup);

    mainLayout->addStretch();

    // ── Połączenia ──
    connect(m_serverBtn, &QPushButton::clicked, this, &RemoteCanWidget::toggleServer);
    connect(&m_server, &WebSocketServer::statusChanged, this, &RemoteCanWidget::updateServerStatus);
    connect(&m_server, &WebSocketServer::errorOccurred, this, [this](const QString &msg) {
        QMessageBox::warning(this, "Błąd serwera", msg);
    });
    connect(m_copyTokenBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_server.token());
        m_copyTokenBtn->setText("Skopiowano!");
        QTimer::singleShot(2000, this, [this]() { m_copyTokenBtn->setText("Kopiuj"); });
    });

    connect(m_clientBtn, &QPushButton::clicked, this, &RemoteCanWidget::toggleClient);
    connect(&m_client, &RemoteCanClient::statusChanged, this, &RemoteCanWidget::updateClientStatus);
    connect(&m_client, &RemoteCanClient::newFrame, this, &RemoteCanWidget::onRemoteFrame);
    connect(&m_client, &RemoteCanClient::errorOccurred, this, [this](const QString &msg) {
        QMessageBox::warning(this, "Błąd klienta", msg);
    });
    connect(&m_client, &RemoteCanClient::frameAckReceived, this, &RemoteCanWidget::onFrameAck);

    // Panel wysyłania — aktywny tylko gdy klient jest połączony
    connect(&m_client, &RemoteCanClient::statusChanged, this, [this](bool connected, const QString &) {
        m_sendBtn->setEnabled(connected && m_client.isConnected());
    });
    connect(m_sendBtn, &QPushButton::clicked, this, &RemoteCanWidget::onSendFrame);

    // Statystyki
    connect(&m_statsTimer, &QTimer::timeout, this, [this]() {
        m_frameCountLabel->setText(QString("Odebrane ramki: %1").arg(m_client.frameCount()));
    });
    m_statsTimer.start(1000);

    setStyleSheet(R"(
        QPushButton { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560;
            border-radius: 4px; padding: 5px 15px; font-weight: bold; }
        QPushButton:hover { background: #e94560; color: #0a0e17; }
        QLineEdit, QSpinBox { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560;
            border-radius: 4px; padding: 3px 8px; }
        QLabel { color: #c0c0c0; }
        QGroupBox { color: #ff66cc; }
    )");
}

// ── Sloty ────────────────────────────────────────────────────

void RemoteCanWidget::toggleServer() {
    if (m_server.isRunning()) {
        m_server.stop();
    } else {
        m_server.startSecure(m_serverPort->value());
    }
}

void RemoteCanWidget::toggleClient() {
    if (m_client.isConnected()) {
        m_client.disconnect();
    } else {
        QString url = m_clientUrl->text().trimmed();
        QString tok = m_clientToken->text().trimmed();
        if (url.isEmpty() || tok.isEmpty()) {
            QMessageBox::warning(this, "Brak danych", "Podaj URL i token.");
            return;
        }
        m_client.connectToServer(url, tok);
    }
}

void RemoteCanWidget::updateServerStatus(bool running) {
    if (running) {
        m_serverBtn->setText("Zatrzymaj serwer WSS");
        m_serverStatus->setText(QString("Serwer: NASŁUCHUJE na porcie %1 (klientów: %2)")
                                .arg(m_serverPort->value()).arg(m_server.clientCount()));
        m_serverStatus->setStyleSheet("color: #00ffaa; font-weight: bold;");
        m_serverPort->setEnabled(false);
        Logger::log(QString("Serwer WSS uruchomiony na porcie %1").arg(m_serverPort->value()));
    } else {
        m_serverBtn->setText("Uruchom serwer WSS");
        m_serverStatus->setText("Serwer: zatrzymany");
        m_serverStatus->setStyleSheet("color: #ff4444;");
        m_serverPort->setEnabled(true);
    }
}

void RemoteCanWidget::updateClientStatus(bool connected, const QString &info) {
    m_clientStatus->setText("Klient: " + info);
    if (connected && m_client.isConnected()) {
        m_clientBtn->setText("Rozłącz");
        m_clientStatus->setStyleSheet("color: #00ffaa; font-weight: bold;");
    } else {
        m_clientBtn->setText("Połącz ze zdalnym CAN");
        m_clientStatus->setStyleSheet(connected ? "color: #ffaa00;" : "color: #ff4444;");
    }
}

void RemoteCanWidget::onRemoteFrame(const CanFrame &frame) {
    if (m_sniffer)
        m_sniffer->submitFrame(frame);
}

void RemoteCanWidget::onSendFrame() {
    if (!m_client.isConnected()) return;

    // Parsowanie CAN ID
    bool ok = false;
    QString idStr = m_sendIdEdit->text().trimmed();
    if (idStr.startsWith("0x", Qt::CaseInsensitive)) idStr = idStr.mid(2);
    uint32_t canId = idStr.toUInt(&ok, 16);
    if (!ok || idStr.isEmpty()) {
        m_sendStatusLabel->setText("⚠ Nieprawidłowy ID");
        m_sendStatusLabel->setStyleSheet("color: #ff4444;");
        return;
    }

    // Parsowanie danych (hex bajty oddzielone spacjami lub bez)
    CanFrame frame;
    frame.id       = canId;
    frame.extended = m_sendExtended->isChecked();
    frame.rtr      = m_sendRtr->isChecked();
    frame.fd       = m_sendFd->isChecked();

    QString dataStr = m_sendDataEdit->text().simplified().remove(' ');
    if (dataStr.length() % 2 != 0 && !dataStr.isEmpty()) dataStr.prepend('0');
    int byteCount = qMin(static_cast<int>(dataStr.length() / 2), m_sendFd->isChecked() ? 64 : 8);
    for (int i = 0; i < byteCount; ++i)
        frame.data[i] = static_cast<uint8_t>(dataStr.mid(i * 2, 2).toUInt(nullptr, 16));
    frame.dlc = static_cast<uint8_t>(byteCount);
    frame.timestamp = static_cast<uint64_t>(
        QDateTime::currentMSecsSinceEpoch()) * 1000;

    m_client.sendFrame(frame);

    // UI feedback — status i historia
    m_sendStatusLabel->setText("Wysyłanie...");
    m_sendStatusLabel->setStyleSheet("color: #ffaa00;");

    QString dataHex;
    for (int i = 0; i < frame.dlc; ++i)
        dataHex += QString("%1 ").arg(frame.data[i], 2, 16, QChar('0')).toUpper();

    QString entry = QString("%1  ID=0x%2  DLC=%3  [%4]")
        .arg(QTime::currentTime().toString("HH:mm:ss.zzz"))
        .arg(frame.id, frame.extended ? 8 : 3, 16, QChar('0')).toUpper()
        .arg(frame.dlc)
        .arg(dataHex.trimmed());
    m_sendHistory->insertItem(0, entry);
    if (m_sendHistory->count() > 20) delete m_sendHistory->takeItem(20);

    Logger::log(QString("Zdalna ramka CAN wysłana: %1").arg(entry));
}

void RemoteCanWidget::onFrameAck(uint32_t canId) {
    m_sendStatusLabel->setText(
        QString("✓ Potwierdzone: 0x%1").arg(canId, 0, 16).toUpper());
    m_sendStatusLabel->setStyleSheet("color: #00ffaa; font-weight: bold;");
}
