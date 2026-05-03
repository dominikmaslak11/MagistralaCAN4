#!/usr/bin/env bash
# update_modern.sh - wdraża nowoczesny sniffer CAN do projektu MagistralaCAN4
# Uruchom w katalogu z projektem (zawierającym CMakeLists.txt)
set -e

echo "Aktualizacja MagistralaCAN4 -> wersja cyberpunk sniffer"

# Katalogi (na wszelki wypadek)
mkdir -p src/core src/gui/resources lua tests

# 1. CMakeLists.txt (dodanie CanFrame.h + drobne porządki)
cat > CMakeLists.txt << 'CMEOF'
cmake_minimum_required(VERSION 3.16)
project(MagistralaCAN4 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Concurrent)
find_package(Lua REQUIRED)

set(SOURCES
    main.cpp
    src/core/CanSniffer.cpp
    src/core/CanFrameModel.cpp
    src/core/FrameDetailWidget.cpp
    src/core/AssociativeLearner.cpp
    src/core/LuaScriptEngine.cpp
    src/core/CanExporter.cpp
    src/gui/MainWindow.cpp
)

set(HEADERS
    src/core/CanFrame.h
    src/core/CanSniffer.h
    src/core/CanFrameModel.h
    src/core/FrameDetailWidget.h
    src/core/AssociativeLearner.h
    src/core/LuaScriptEngine.h
    src/core/CanExporter.h
    src/gui/MainWindow.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${LUA_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Concurrent
    ${LUA_LIBRARIES}
)

if(UNIX AND NOT APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE LINUX_SOCKETCAN)
endif()
CMEOF

# 2. Nowy plik: CanFrame.h
cat > src/core/CanFrame.h << 'HERE'
#pragma once
#include <cstdint>
#include <array>
#include <QMetaType>
#include <QString>

struct CanFrame {
    uint32_t id = 0;
    bool     extended = false;
    bool     rtr = false;
    bool     error = false;
    uint8_t  dlc = 0;
    std::array<uint8_t, 8> data{};
    uint64_t timestamp = 0;

    [[nodiscard]] QString toString() const {
        return QString("ID: %1 | DLC: %2").arg(id, 3, 16, QChar('0')).arg(dlc);
    }
};

Q_DECLARE_METATYPE(CanFrame)
HERE

# 3. CanSniffer.h
cat > src/core/CanSniffer.h << 'HERE'
#pragma once
#include <QObject>
#include <QThread>
#include <QString>
#include <linux/can.h>
#include <linux/can/raw.h>
#include "CanFrame.h"

class CanSniffer : public QObject {
    Q_OBJECT
public:
    explicit CanSniffer(QObject *parent = nullptr);
    ~CanSniffer() override;

signals:
    void newFrame(const CanFrame &frame);
    void statusChanged(bool running);
    void errorOccurred(const QString &msg);

public slots:
    void start(const QString &interface);
    void stop();

private:
    void doWork();
    bool openSocket(const QString &ifname);
    void closeSocket();
    CanFrame parseFrame(const struct can_frame &rawFrame, uint64_t timestamp) const;
    uint64_t systemTimestamp() const;

    std::atomic<bool> m_running{false};
    int m_socket{-1};
    QString m_interface;
};
HERE

# 4. CanSniffer.cpp (z include QtConcurrent)
cat > src/core/CanSniffer.cpp << 'HERE'
#include "CanSniffer.h"
#include <QDebug>
#include <QtConcurrent>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <cstring>

CanSniffer::CanSniffer(QObject *parent) : QObject(parent) {}

CanSniffer::~CanSniffer() {
    if (m_running) stop();
}

void CanSniffer::start(const QString &interface) {
    if (m_running) return;
    if (!openSocket(interface)) {
        emit errorOccurred("Nie można otworzyć interfejsu: " + interface);
        return;
    }
    m_interface = interface;
    m_running = true;
    emit statusChanged(true);
    QtConcurrent::run([this] { doWork(); });
}

void CanSniffer::stop() {
    if (!m_running) return;
    m_running = false;
    closeSocket();
    emit statusChanged(false);
}

void CanSniffer::doWork() {
    while (m_running) {
        struct can_frame frame;
        ssize_t nbytes = read(m_socket, &frame, sizeof(struct can_frame));
        if (nbytes < 0) {
            if (m_running) {
                emit errorOccurred("Błąd odczytu z CAN: " + QString::fromLocal8Bit(strerror(errno)));
            }
            break;
        }
        if (nbytes == 0) continue;

        uint64_t ts = systemTimestamp();
        CanFrame canFrame = parseFrame(frame, ts);
        emit newFrame(canFrame);
    }
    closeSocket();
}

bool CanSniffer::openSocket(const QString &ifname) {
    m_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (m_socket < 0) {
        emit errorOccurred("socket() nie powiódł się");
        return false;
    }

    struct ifreq ifr;
    std::strncpy(ifr.ifr_name, ifname.toStdString().c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    if (ioctl(m_socket, SIOCGIFINDEX, &ifr) < 0) {
        emit errorOccurred("Nie znaleziono interfejsu: " + ifname);
        closeSocket();
        return false;
    }

    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(m_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        emit errorOccurred("bind() nie powiódł się");
        closeSocket();
        return false;
    }

    return true;
}

void CanSniffer::closeSocket() {
    if (m_socket >= 0) {
        ::close(m_socket);
        m_socket = -1;
    }
}

CanFrame CanSniffer::parseFrame(const struct can_frame &rawFrame, uint64_t timestamp) const {
    CanFrame frame;
    frame.id = rawFrame.can_id & CAN_EFF_MASK;
    frame.extended = rawFrame.can_id & CAN_EFF_FLAG;
    frame.rtr = rawFrame.can_id & CAN_RTR_FLAG;
    frame.error = rawFrame.can_id & CAN_ERR_FLAG;
    frame.dlc = rawFrame.len;
    for (int i = 0; i < rawFrame.len && i < 8; ++i) {
        frame.data[i] = rawFrame.data[i];
    }
    frame.timestamp = timestamp;
    return frame;
}

uint64_t CanSniffer::systemTimestamp() const {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000000ULL + tv.tv_usec;
}
HERE

# 5. CanFrameModel.h
cat > src/core/CanFrameModel.h << 'HERE'
#pragma once
#include <QAbstractTableModel>
#include <QVector>
#include <QMutex>
#include <QTimer>
#include "CanFrame.h"

class CanFrameModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ID, EXT, RTR, DLC, DATA, TIMESTAMP, _COUNT
    };

    explicit CanFrameModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

public slots:
    void appendFrames(const QVector<CanFrame> &newFrames);

private:
    mutable QMutex m_mutex;
    QVector<CanFrame> m_frames;
    QVector<CanFrame> m_pendingFrames;
};
HERE

# 6. CanFrameModel.cpp
cat > src/core/CanFrameModel.cpp << 'HERE'
#include "CanFrameModel.h"
#include <QColor>

CanFrameModel::CanFrameModel(QObject *parent) : QAbstractTableModel(parent) {
    m_pendingFrames.reserve(500);
}

int CanFrameModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    QMutexLocker lock(&m_mutex);
    return m_frames.size();
}

int CanFrameModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return Column::_COUNT;
}

QVariant CanFrameModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_frames.size())
        return {};

    if (role == Qt::DisplayRole) {
        QMutexLocker lock(&m_mutex);
        const CanFrame &frame = m_frames.at(index.row());
        switch (index.column()) {
        case Column::ID:        return QString::number(frame.id, 16).toUpper().rightJustified(3, '0');
        case Column::EXT:       return frame.extended ? "EXT" : "STD";
        case Column::RTR:       return frame.rtr ? "RTR" : "Data";
        case Column::DLC:       return frame.dlc;
        case Column::DATA: {
            QString dataHex;
            for (int i = 0; i < frame.dlc; ++i)
                dataHex += QString("%1 ").arg(frame.data[i], 2, 16, QChar('0')).toUpper();
            return dataHex.trimmed();
        }
        case Column::TIMESTAMP: return QString("%1 µs").arg(frame.timestamp);
        default: break;
        }
    } else if (role == Qt::TextAlignmentRole) {
        return Qt::AlignCenter;
    } else if (role == Qt::ForegroundRole) {
        switch (index.column()) {
        case Column::ID:        return QColor("#00ffaa");
        case Column::EXT:       return QColor("#ff66cc");
        case Column::RTR:       return QColor("#ffaa00");
        case Column::DLC:       return QColor("#66ccff");
        case Column::DATA:      return QColor("#aa44ff");
        case Column::TIMESTAMP: return QColor("#888888");
        }
    } else if (role == Qt::BackgroundRole) {
        return (index.row() % 2) ? QColor("#0d1117") : QColor("#161b22");
    }
    return {};
}

QVariant CanFrameModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case Column::ID:        return "CAN ID";
        case Column::EXT:       return "Typ";
        case Column::RTR:       return "RTR";
        case Column::DLC:       return "DLC";
        case Column::DATA:      return "Dane (hex)";
        case Column::TIMESTAMP: return "Czas [µs]";
        }
    }
    return {};
}

void CanFrameModel::appendFrames(const QVector<CanFrame> &newFrames) {
    if (newFrames.isEmpty()) return;

    QMutexLocker lock(&m_mutex);
    m_pendingFrames.append(newFrames);
    lock.unlock();

    if (!m_pendingFrames.isEmpty()) {
        int start = m_frames.size();
        int count = m_pendingFrames.size();
        beginInsertRows(QModelIndex(), start, start + count - 1);
        {
            QMutexLocker relock(&m_mutex);
            m_frames.append(m_pendingFrames);
            m_pendingFrames.clear();
        }
        endInsertRows();
    }
}
HERE

# 7. MainWindow.h
cat > src/gui/MainWindow.h << 'HERE'
#pragma once
#include <QMainWindow>
#include <QTableView>
#include <QPushButton>
#include <QTimer>
#include <QVector>
#include "core/CanSniffer.h"
#include "core/CanFrameModel.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void toggleSniffing();
    void onNewFrame(const CanFrame &frame);
    void updateTableBatch();

private:
    void setupStyle();
    void setupToolBar();
    void setupCentralWidget();

    CanSniffer m_sniffer;
    CanFrameModel *m_model;
    QTableView *m_tableView;
    QPushButton *m_btnStartStop;

    QTimer m_batchTimer;
    QVector<CanFrame> m_frameBuffer;
    bool m_sniffing = false;
};
HERE

# 8. MainWindow.cpp
cat > src/gui/MainWindow.cpp << 'HERE'
#include "MainWindow.h"
#include <QToolBar>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QMetaObject>
#include <QColor>
#include <QFont>
#include <QApplication>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("MagistralaCAN4 - Sniffer CAN");
    resize(1280, 800);

    m_model = new CanFrameModel(this);
    m_tableView = new QTableView;
    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->verticalHeader()->hide();
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->setShowGrid(false);
    m_tableView->setAlternatingRowColors(false);

    m_batchTimer.setInterval(33);
    connect(&m_batchTimer, &QTimer::timeout, this, &MainWindow::updateTableBatch);

    connect(&m_sniffer, &CanSniffer::newFrame, this, &MainWindow::onNewFrame, Qt::QueuedConnection);
    connect(&m_sniffer, &CanSniffer::errorOccurred, this, [this](const QString &msg) {
        QMessageBox::warning(this, "Błąd CAN", msg);
        m_sniffer.stop();
        m_sniffing = false;
        m_btnStartStop->setText("▶ Start");
        m_batchTimer.stop();
    });

    setupToolBar();
    setupCentralWidget();
    setupStyle();
}

MainWindow::~MainWindow() {
    if (m_sniffing) {
        m_sniffer.stop();
        m_batchTimer.stop();
    }
}

void MainWindow::toggleSniffing() {
    if (!m_sniffing) {
        m_sniffer.start("vcan0");
        m_sniffing = true;
        m_btnStartStop->setText("■ Stop");
        m_batchTimer.start();
    } else {
        m_sniffer.stop();
        m_sniffing = false;
        m_btnStartStop->setText("▶ Start");
        m_batchTimer.stop();
        m_frameBuffer.clear();
    }
}

void MainWindow::onNewFrame(const CanFrame &frame) {
    m_frameBuffer.append(frame);
}

void MainWindow::updateTableBatch() {
    if (m_frameBuffer.isEmpty()) return;
    m_model->appendFrames(m_frameBuffer);
    m_frameBuffer.clear();
    m_tableView->scrollToBottom();
}

void MainWindow::setupToolBar() {
    auto *toolbar = addToolBar("Główne");
    toolbar->setMovable(false);

    m_btnStartStop = new QPushButton("▶ Start");
    connect(m_btnStartStop, &QPushButton::clicked, this, &MainWindow::toggleSniffing);
    toolbar->addWidget(m_btnStartStop);

    toolbar->addSeparator();
    toolbar->addAction("🗙 Wyczyść", this, [this]() {
        m_model->removeRows(0, m_model->rowCount());
    });
}

void MainWindow::setupCentralWidget() {
    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tableView);
    setCentralWidget(central);
}

void MainWindow::setupStyle() {
    qApp->setStyleSheet(R"(
        QMainWindow {
            background-color: #0a0e17;
        }
        QToolBar {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #1a1a2e, stop:1 #16213e);
            border-bottom: 2px solid #e94560;
            spacing: 8px;
            padding: 4px;
        }
        QPushButton {
            background: #1a1a2e;
            color: #00ffaa;
            border: 1px solid #e94560;
            border-radius: 4px;
            padding: 5px 15px;
            font-weight: bold;
        }
        QPushButton:hover {
            background: #e94560;
            color: #0a0e17;
        }
        QTableView {
            background-color: #0a0e17;
            alternate-background-color: #161b22;
            color: #c0c0c0;
            gridline-color: #2a2a3c;
            selection-background-color: #e94560;
            selection-color: #ffffff;
            font-family: "Consolas", "Courier New", monospace;
            font-size: 12px;
        }
        QHeaderView::section {
            background-color: #1a1a2e;
            color: #ff66cc;
            font-weight: bold;
            padding: 4px;
            border: none;
            border-bottom: 2px solid #e94560;
        }
    )");
}
HERE

# 9. Pozostałe pliki – szkieletowe implementacje
for MODULE in FrameDetailWidget AssociativeLearner LuaScriptEngine CanExporter; do
    cat > "src/core/${MODULE}.h" << EOF
#pragma once
#include <QWidget>
class ${MODULE} : public QWidget { Q_OBJECT
public: explicit ${MODULE}(QWidget *p=nullptr) : QWidget(p) {} };
EOF
    cat > "src/core/${MODULE}.cpp" << EOF
#include "${MODULE}.h"
// ${MODULE}.cpp – placeholder
EOF
done

echo "Aktualizacja zakończona. Możesz budować projekt (build)."
