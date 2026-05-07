#!/usr/bin/env bash
# =============================================================================
# MagistralaCAN4 – WebSocket Server Deployment Script
# =============================================================================
# Dodaje tryb serwera WebSocket do istniejącego projektu bez naruszania
# pozostałej funkcjonalności. Wykonuje backup, tworzy nowe pliki, modyfikuje
# istniejące i weryfikuje kompilację.
#
# Uruchomienie:  bash deploy_websocket.sh
# Cofanie zmian: bash deploy_websocket.sh --rollback
# =============================================================================

set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
BACKUP_DIR="${PROJECT_ROOT}/.ws_backup_$(date +%Y%m%d_%H%M%S)"

# --- Kolory ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log()  { echo -e "${CYAN}[WS]${NC} $*"; }
ok()   { echo -e "${GREEN}[OK]${NC}  $*"; }
warn() { echo -e "${YELLOW}[!!]${NC} $*"; }
err()  { echo -e "${RED}[ERR]${NC} $*"; exit 1; }

# =============================================================================
# Rollback
# =============================================================================
if [[ "${1:-}" == "--rollback" ]]; then
    # Find most recent backup
    LATEST_BACKUP=$(ls -dt "${PROJECT_ROOT}"/.ws_backup_* 2>/dev/null | head -1)
    if [[ -z "$LATEST_BACKUP" ]]; then
        echo "No backup found. Nothing to roll back."
        exit 0
    fi
    echo "Rolling back from: $LATEST_BACKUP"
    for f in "$LATEST_BACKUP"/*; do
        base=$(basename "$f")
        cp "$f" "${PROJECT_ROOT}/${base}"
        echo "  Restored: $base"
    done
    # Remove new files
    rm -f "${PROJECT_ROOT}/src/core/WebSocketServer.h"
    rm -f "${PROJECT_ROOT}/src/core/WebSocketServer.cpp"
    echo "Rollback complete."
    exit 0
fi

# =============================================================================
# Pre-flight checks
# =============================================================================
log "MagistralaCAN4 – WebSocket Server Deployment"
log "============================================"
echo ""

# Check we're in project root
if [[ ! -f "${PROJECT_ROOT}/CMakeLists.txt" ]]; then
    err "CMakeLists.txt nie znaleziony. Uruchom skrypt z katalogu projektu."
fi

# Check qt6-websockets-dev
if ! dpkg -s qt6-websockets-dev &>/dev/null; then
    err "qt6-websockets-dev nie jest zainstalowany. Zainstaluj: sudo apt install qt6-websockets-dev"
fi
ok "qt6-websockets-dev wykryty"

# Check for existing deployment
if [[ -f "${PROJECT_ROOT}/src/core/WebSocketServer.h" ]]; then
    warn "WebSocketServer.h już istnieje – być może skrypt był już uruchomiony."
    warn "Kontynuacja nadpisze istniejące pliki. Użyj --rollback jeśli potrzebujesz cofnąć zmiany."
    echo ""
    read -rp "Kontynuować? [t/N] " confirm
    [[ "$confirm" =~ ^[tT]$ ]] || exit 0
fi

# =============================================================================
# Backup
# =============================================================================
log "Tworzenie backupu w $BACKUP_DIR ..."
mkdir -p "$BACKUP_DIR"
for f in CMakeLists.txt MainWindow.h MainWindow.cpp; do
    if [[ -f "${PROJECT_ROOT}/${f}" ]]; then
        cp "${PROJECT_ROOT}/${f}" "${BACKUP_DIR}/${f}"
        ok "Backup: $f"
    fi
done

# =============================================================================
# 1. Create src/core/WebSocketServer.h
# =============================================================================
log "1/5 Tworzenie src/core/WebSocketServer.h ..."
cat > "${PROJECT_ROOT}/src/core/WebSocketServer.h" << 'HEADER_EOF'
#pragma once
#include <QObject>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QList>
#include "CanFrame.h"

/**
 * @brief Serwer WebSocket streamujący ramki CAN do zdalnych klientów.
 *
 * Nasłuchuje na zadanym porcie (domyślnie 9000), akceptuje połączenia
 * i rozsyła każdą ramkę CAN jako kompaktowy JSON do wszystkich podłączonych
 * klientów. Obsługuje automatyczne czyszczenie po rozłączeniu.
 *
 * Format JSON ramki:
 *   {"type":"frame","id":123,"dlc":8,"data":"A1B2...","dataBytes":[...],"timestamp":...}
 */
class WebSocketServer : public QObject {
    Q_OBJECT
public:
    explicit WebSocketServer(QObject *parent = nullptr);
    ~WebSocketServer() override;

    /// Czy serwer aktualnie nasłuchuje?
    [[nodiscard]] bool isRunning() const { return m_running; }

    /// Liczba aktualnie podłączonych klientów.
    [[nodiscard]] int clientCount() const { return m_clients.size(); }

public slots:
    /// Rozpoczyna nasłuchiwanie na zadanym porcie.
    void start(quint16 port = 9000);

    /// Zatrzymuje serwer i rozłącza wszystkich klientów.
    void stop();

    /// Rozsyła ramkę CAN jako JSON do wszystkich klientów.
    /// Bezpieczne dla wątków – wywoływane przez Qt::QueuedConnection.
    void broadcastFrame(const CanFrame &frame);

signals:
    /// Emitowany przy zmianie stanu serwera (start/stop).
    void statusChanged(bool running);

    /// Emitowany przy podłączeniu/rozłączeniu klienta.
    void clientCountChanged(int count);

    /// Emitowany w przypadku błędu (np. port zajęty).
    void errorOccurred(const QString &msg);

private slots:
    void onNewConnection();
    void onClientDisconnected();
    void onTextMessageReceived(const QString &message);

private:
    /// Serializuje CanFrame do kompaktowego JSON.
    [[nodiscard]] QString frameToJson(const CanFrame &frame) const;

    QWebSocketServer *m_server = nullptr;
    QList<QWebSocket *> m_clients;
    bool m_running = false;
};
HEADER_EOF
ok "src/core/WebSocketServer.h utworzony"

# =============================================================================
# 2. Create src/core/WebSocketServer.cpp
# =============================================================================
log "2/5 Tworzenie src/core/WebSocketServer.cpp ..."
cat > "${PROJECT_ROOT}/src/core/WebSocketServer.cpp" << 'CPP_EOF'
#include "WebSocketServer.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHostAddress>
#include <QDebug>

WebSocketServer::WebSocketServer(QObject *parent) : QObject(parent) {}

WebSocketServer::~WebSocketServer() {
    stop();
}

void WebSocketServer::start(quint16 port) {
    if (m_running) {
        qDebug() << "WebSocket server already running";
        return;
    }

    m_server = new QWebSocketServer(
        QStringLiteral("MagistralaCAN4"),
        QWebSocketServer::NonSecureMode,
        this
    );

    if (!m_server->listen(QHostAddress::Any, port)) {
        const QString msg = QStringLiteral(
            "WebSocket: nie można nasłuchiwać na porcie %1 (%2)"
        ).arg(port).arg(m_server->errorString());
        emit errorOccurred(msg);
        delete m_server;
        m_server = nullptr;
        return;
    }

    connect(m_server, &QWebSocketServer::newConnection,
            this, &WebSocketServer::onNewConnection);
    connect(m_server, &QWebSocketServer::acceptError,
            this, [this]() {
        emit errorOccurred(QStringLiteral("WebSocket accept error: %1")
                          .arg(m_server->errorString()));
    });

    m_running = true;
    emit statusChanged(true);
    qDebug() << "WebSocket server nasłuchuje na porcie" << port;
}

void WebSocketServer::stop() {
    if (!m_running) return;

    // Zamknij socket nasłuchujący – zatrzyma nowe połączenia
    if (m_server) {
        m_server->close();
    }

    // Rozłącz wszystkich klientów
    for (QWebSocket *client : m_clients) {
        if (client->state() == QAbstractSocket::ConnectedState) {
            client->close();
        }
        client->deleteLater();
    }
    m_clients.clear();

    if (m_server) {
        m_server->deleteLater();
        m_server = nullptr;
    }

    m_running = false;
    emit statusChanged(false);
    emit clientCountChanged(0);
    qDebug() << "WebSocket server zatrzymany";
}

void WebSocketServer::broadcastFrame(const CanFrame &frame) {
    if (!m_running) return;

    // Usuń martwe sockety z listy
    m_clients.erase(
        std::remove_if(m_clients.begin(), m_clients.end(),
                       [](QWebSocket *ws) {
                           return !ws || ws->state() != QAbstractSocket::ConnectedState;
                       }),
        m_clients.end()
    );

    if (m_clients.isEmpty()) return;

    const QString json = frameToJson(frame);
    for (QWebSocket *client : m_clients) {
        client->sendTextMessage(json);
    }
}

void WebSocketServer::onNewConnection() {
    while (m_server && m_server->hasPendingConnections()) {
        QWebSocket *client = m_server->nextPendingConnection();
        if (!client) continue;

        connect(client, &QWebSocket::disconnected,
                this, &WebSocketServer::onClientDisconnected);
        connect(client, &QWebSocket::textMessageReceived,
                this, &WebSocketServer::onTextMessageReceived);

        // Obsługa błędów per-klient (Qt 6.10+: errorOccurred)
        connect(client, \&QWebSocket::errorOccurred,
                this, [client](QAbstractSocket::SocketError) {
            qDebug() << "WebSocket client error:" << client->errorString();
        });

        m_clients.append(client);
        qDebug() << "WebSocket klient podłączony – razem:" << m_clients.size();
    }
    emit clientCountChanged(m_clients.size());
}

void WebSocketServer::onClientDisconnected() {
    auto *client = qobject_cast<QWebSocket *>(sender());
    if (!client) return;

    m_clients.removeAll(client);
    client->deleteLater();
    qDebug() << "WebSocket klient rozłączony – razem:" << m_clients.size();
    emit clientCountChanged(m_clients.size());
}

void WebSocketServer::onTextMessageReceived(const QString &message) {
    // Placeholder na przyszłe komendy od klientów
    qDebug() << "WebSocket wiadomość od klienta:" << message.left(120);
}

QString WebSocketServer::frameToJson(const CanFrame &frame) const {
    QJsonObject obj;
    obj[QStringLiteral("type")]      = QStringLiteral("frame");
    obj[QStringLiteral("id")]        = static_cast<int>(frame.id);
    obj[QStringLiteral("extended")]  = frame.extended;
    obj[QStringLiteral("rtr")]       = frame.rtr;
    obj[QStringLiteral("error")]     = frame.error;
    obj[QStringLiteral("fd")]        = frame.fd;
    obj[QStringLiteral("dlc")]       = static_cast<int>(frame.dlc);
    obj[QStringLiteral("timestamp")] = static_cast<qint64>(frame.timestamp);

    // Dane jako hex string (zwarty)
    QString hexData;
    hexData.reserve(frame.dlc * 2);
    for (int i = 0; i < frame.dlc && i < 64; ++i) {
        hexData += QStringLiteral("%1").arg(frame.data[i], 2, 16, QLatin1Char('0'));
    }
    obj[QStringLiteral("data")] = hexData;

    // Dane jako tablica bajtów (dla wygody JS)
    QJsonArray dataBytes;
    for (int i = 0; i < frame.dlc && i < 64; ++i) {
        dataBytes.append(static_cast<int>(frame.data[i]));
    }
    obj[QStringLiteral("dataBytes")] = dataBytes;

    return QString::fromUtf8(
        QJsonDocument(obj).toJson(QJsonDocument::Compact)
    );
}
CPP_EOF
ok "src/core/WebSocketServer.cpp utworzony"

# =============================================================================
# 3. Patch CMakeLists.txt
# =============================================================================
log "3/5 Modyfikacja CMakeLists.txt ..."

# 3a. Add WebSockets to find_package
if grep -q 'WebSockets' "${PROJECT_ROOT}/CMakeLists.txt"; then
    warn "CMakeLists.txt już zawiera WebSockets – pomijam find_package"
else
    sed -i 's/find_package(Qt6 REQUIRED COMPONENTS Widgets Core Concurrent Charts)/find_package(Qt6 REQUIRED COMPONENTS Widgets Core Concurrent Charts WebSockets)/' \
        "${PROJECT_ROOT}/CMakeLists.txt"
    ok "Dodano WebSockets do find_package"
fi

# 3b. Add WebSocketServer.cpp to SOURCES (before last line with .cpp)
if grep -q 'WebSocketServer.cpp' "${PROJECT_ROOT}/CMakeLists.txt"; then
    warn "WebSocketServer.cpp już w SOURCES – pomijam"
else
    sed -i '/^    src\/gui\/MainWindow.cpp$/i\    src/core/WebSocketServer.cpp' \
        "${PROJECT_ROOT}/CMakeLists.txt"
    ok "Dodano WebSocketServer.cpp do SOURCES"
fi

# 3c. Add WebSocketServer.h to HEADERS
if grep -q 'WebSocketServer.h' "${PROJECT_ROOT}/CMakeLists.txt"; then
    warn "WebSocketServer.h już w HEADERS – pomijam"
else
    sed -i '/^    src\/gui\/MainWindow.h$/i\    src/core/WebSocketServer.h' \
        "${PROJECT_ROOT}/CMakeLists.txt"
    ok "Dodano WebSocketServer.h do HEADERS"
fi

# 3d. Add Qt6::WebSockets to target_link_libraries
if grep -q 'Qt6::WebSockets' "${PROJECT_ROOT}/CMakeLists.txt"; then
    warn "Qt6::WebSockets już w linkowaniu – pomijam"
else
    sed -i '/^    Qt6::Charts$/a\    Qt6::WebSockets' \
        "${PROJECT_ROOT}/CMakeLists.txt"
    ok "Dodano Qt6::WebSockets do linkowania"
fi

# =============================================================================
# 4. Patch MainWindow.h
# =============================================================================
log "4/5 Modyfikacja MainWindow.h ..."

# 4a. Forward declaration (after last #include line)
if grep -q 'class WebSocketServer' "${PROJECT_ROOT}/MainWindow.h"; then
    warn "Forward-decl WebSocketServer już w MainWindow.h – pomijam"
else
    sed -i '/^#include "core\/CanFrameModel.h"$/a\
class WebSocketServer;' "${PROJECT_ROOT}/MainWindow.h"
    ok "Dodano forward-decl WebSocketServer"
fi

# 4b. Add toggleWebSocket slot (after updateTableBatch)
if grep -q 'toggleWebSocket' "${PROJECT_ROOT}/MainWindow.h"; then
    warn "toggleWebSocket już w MainWindow.h – pomijam"
else
    sed -i '/^    void updateTableBatch();$/a\    void toggleWebSocket();' \
        "${PROJECT_ROOT}/MainWindow.h"
    ok "Dodano deklarację toggleWebSocket()"
fi

# 4c. Add members (after m_btnStartStop)
if grep -q 'm_btnWsServer' "${PROJECT_ROOT}/MainWindow.h"; then
    warn "m_btnWsServer już w MainWindow.h – pomijam"
else
    sed -i '/^    QPushButton \*m_btnStartStop;$/a\    QPushButton *m_btnWsServer;\n    WebSocketServer *m_wsServer;' \
        "${PROJECT_ROOT}/MainWindow.h"
    ok "Dodano membery WebSocketServer"
fi

# 4d. Add m_wsRunning (after m_sniffing)
if grep -q 'm_wsRunning' "${PROJECT_ROOT}/MainWindow.h"; then
    warn "m_wsRunning już w MainWindow.h – pomijam"
else
    sed -i '/^    bool m_sniffing = false;$/a\    bool m_wsRunning = false;' \
        "${PROJECT_ROOT}/MainWindow.h"
    ok "Dodano flagę m_wsRunning"
fi

# =============================================================================
# 5. Patch MainWindow.cpp
# =============================================================================
log "5/5 Modyfikacja MainWindow.cpp ..."

# 5a. Add include
if grep -q 'WebSocketServer.h' "${PROJECT_ROOT}/MainWindow.cpp"; then
    warn "#include WebSocketServer.h już obecny – pomijam"
else
    sed -i '/^#include "MainWindow.h"$/a\#include "core/WebSocketServer.h"' \
        "${PROJECT_ROOT}/MainWindow.cpp"
    ok "Dodano #include WebSocketServer.h"
fi

# 5b. Add WebSocketServer creation and connections (after m_batchTimer.setInterval line)
if grep -q 'm_wsServer = new WebSocketServer' "${PROJECT_ROOT}/MainWindow.cpp"; then
    warn "Tworzenie WebSocketServer już obecne – pomijam"
else
    sed -i '/^    m_batchTimer.setInterval(33);$/a\
\
    // --- WebSocket Server ---\
    m_wsServer = new WebSocketServer(this);\
    connect(m_wsServer, \&WebSocketServer::errorOccurred, this, [this](const QString \&msg) {\
        QMessageBox::warning(this, QStringLiteral("Błąd WebSocket"), msg);\
    });\
    connect(m_model, \&CanFrameModel::frameUpdated, m_wsServer, \&WebSocketServer::broadcastFrame);' \
        "${PROJECT_ROOT}/MainWindow.cpp"
    ok "Dodano tworzenie WebSocketServer i połączenia sygnałów"
fi

# 5c. Add toggleWebSocket method (before setupToolBar)
# We'll add it after the closing brace of updateTableBatch
if grep -q 'void MainWindow::toggleWebSocket' "${PROJECT_ROOT}/MainWindow.cpp"; then
    warn "toggleWebSocket() już zdefiniowana – pomijam"
else
    # Find the end of updateTableBatch and add the new method after it
    # updateTableBatch ends with: m_tableView->scrollToBottom();\n}
    # We insert toggleWebSocket between updateTableBatch() and setupToolBar()
    sed -i '/^    m_tableView->scrollToBottom();$/,/^}$/{
        /^}$/a\
\
void MainWindow::toggleWebSocket() {\
    if (!m_wsRunning) {\
        m_wsServer->start(9000);\
        m_wsRunning = true;\
        m_btnWsServer->setText(QStringLiteral("🔌 WS Stop"));\
    } else {\
        m_wsServer->stop();\
        m_wsRunning = false;\
        m_btnWsServer->setText(QStringLiteral("🔌 WS Start"));\
    }\
}
    }' "${PROJECT_ROOT}/MainWindow.cpp"
    ok "Dodano implementację toggleWebSocket()"
fi

# 5d. Add WS button in setupToolBar (after the Clear action)
if grep -q 'm_btnWsServer = new QPushButton' "${PROJECT_ROOT}/MainWindow.cpp"; then
    warn "Przycisk WS już w setupToolBar – pomijam"
else
    sed -i '/^    toolbar->addAction.*Wyczyść/a\
\
    // WebSocket\
    toolbar->addSeparator();\
    m_btnWsServer = new QPushButton(QStringLiteral("🔌 WS Start"));\
    connect(m_btnWsServer, \&QPushButton::clicked, this, \&MainWindow::toggleWebSocket);\
    toolbar->addWidget(m_btnWsServer);' \
        "${PROJECT_ROOT}/MainWindow.cpp"
    ok "Dodano przycisk WebSocket do toolbaru"
fi

# 5e. Update destructor to stop WS server
if grep -q 'm_wsServer->stop()' "${PROJECT_ROOT}/MainWindow.cpp"; then
    warn "Zatrzymanie WS w destruktorze już obecne – pomijam"
else
    sed -i '/^        m_sniffer.stop();$/a\        m_wsServer->stop();' \
        "${PROJECT_ROOT}/MainWindow.cpp"
    ok "Dodano zatrzymanie WS w destruktorze"
fi

# =============================================================================
# Verification – build
# =============================================================================
log "============================================"
log "Wszystkie pliki zmodyfikowane. Weryfikacja przez kompilację..."
echo ""

BUILD_DIR="${PROJECT_ROOT}/build_ws_test"

# Clean any previous test build
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

log "Uruchamianie cmake ..."
if cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" 2>&1 | tail -5; then
    ok "CMake konfiguracja – OK"
else
    err "CMake konfiguracja NIEUDANA. Sprawdź log powyżej."
fi

log "Uruchamianie make ..."
if cmake --build "$BUILD_DIR" -- -j"$(nproc)" 2>&1 | tail -15; then
    ok "Kompilacja – OK"
else
    err "Kompilacja NIEUDANA. Przywracanie backupu..."
    for f in "$BACKUP_DIR"/*; do
        base=$(basename "$f")
        cp "$f" "${PROJECT_ROOT}/${base}"
        echo "  Przywrócono: $base"
    done
    rm -f "${PROJECT_ROOT}/src/core/WebSocketServer.h"
    rm -f "${PROJECT_ROOT}/src/core/WebSocketServer.cpp"
    err "Backup przywrócony. Sprawdź błędy kompilacji powyżej."
fi

# Cleanup build dir
rm -rf "$BUILD_DIR"
ok "Katalog build_ws_test usunięty"

# =============================================================================
# Summary
# =============================================================================
echo ""
log "============================================"
log "  WDROŻENIE ZAKOŃCZONE POMYŚLNIE"
log "============================================"
echo ""
echo "  Nowe pliki:"
echo "    src/core/WebSocketServer.h"
echo "    src/core/WebSocketServer.cpp"
echo ""
echo "  Zmodyfikowane pliki:"
echo "    CMakeLists.txt       (+WebSockets, +linkowanie)"
echo "    MainWindow.h          (+forward-decl, +sloty, +membery)"
echo "    MainWindow.cpp        (+include, +inicjalizacja, +przycisk)"
echo ""
echo "  Backup: $BACKUP_DIR"
echo ""
echo "  Aby cofnąć zmiany:  bash deploy_websocket.sh --rollback"
echo ""
echo "  Użycie w aplikacji:"
echo "    - Przycisk '🔌 WS Start' w toolbarze uruchamia serwer na porcie 9000"
echo "    - Ramki CAN są streamowane jako JSON do wszystkich klientów WebSocket"
echo "    - Klient testowy (przeglądarka): otwórz DevTools i wykonaj:"
echo "        let ws = new WebSocket('ws://localhost:9000');"
echo "        ws.onmessage = e => console.log(JSON.parse(e.data));"
echo ""
log "Gotowe."
