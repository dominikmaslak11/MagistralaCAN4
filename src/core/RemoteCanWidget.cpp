#include "RemoteCanWidget.h"
#include "CanSniffer.h"
#include "Logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>

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
    // Ramki zdalne są wstrzykiwane do pipeline'u przez sygnał newFrame
    // (połączenia robione w MainWindow). Nie wysyłamy na vcan0,
    // żeby uniknąć nieskończonej pętli WSS ↔ vcan0.
    Q_UNUSED(frame);
}
