#!/usr/bin/env bash
# add_frame_detail.sh – widok szczegółów ramki z podświetlaniem bitów
set -e

echo "=== Wdrażanie widoku szczegółów ramki ==="

# 1. FrameDetailWidget.h
cat > src/core/FrameDetailWidget.h << 'EOF'
#pragma once
#include <QWidget>
#include <QLabel>
#include <QGridLayout>
#include <QScrollArea>
#include <QHash>
#include "CanFrame.h"

class FrameDetailWidget : public QWidget {
    Q_OBJECT
public:
    explicit FrameDetailWidget(QWidget *parent = nullptr);
    void loadFrame(const CanFrame &frame);

private:
    void buildGrid();
    QLabel* createByteLabel(int byteIndex);
    QString byteToBinary(uint8_t value) const;
    void highlightChangedBits(const CanFrame &frame);

    QGridLayout *m_grid;
    QLabel *m_idLabel;
    QLabel *m_dlcLabel;
    QLabel *m_timestampLabel;
    QVector<QLabel*> m_byteLabels;      // etykiety bajtów (hex)
    QVector<QVector<QLabel*>> m_bitLabels;  // etykiety bitów (8 bajtów × 8 bitów)
    QHash<uint32_t, CanFrame> m_lastFrameMap; // ostatnia ramka dla każdego ID
    uint32_t m_currentId = 0xFFFFFFFF;
};
EOF

# 2. FrameDetailWidget.cpp – pełna implementacja
cat > src/core/FrameDetailWidget.cpp << 'EOF'
#include "FrameDetailWidget.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QFont>
#include <QDebug>

FrameDetailWidget::FrameDetailWidget(QWidget *parent) : QWidget(parent) {
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0,0,0,0);

    auto *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; }");
    auto *scrollWidget = new QWidget;
    auto *layout = new QVBoxLayout(scrollWidget);

    // Sekcja identyfikatora
    m_idLabel = new QLabel("ID: —");
    m_idLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #00ffaa;");
    m_dlcLabel = new QLabel("DLC: —");
    m_dlcLabel->setStyleSheet("font-size: 14px; color: #ff66cc;");
    m_timestampLabel = new QLabel("Czas: —");
    m_timestampLabel->setStyleSheet("font-size: 14px; color: #c0c0c0;");

    layout->addWidget(m_idLabel);
    layout->addWidget(m_dlcLabel);
    layout->addWidget(m_timestampLabel);

    // Siatka bitów – przewijalna
    m_grid = new QGridLayout;
    m_grid->setSpacing(2);

    // Nagłówki kolumn (bity 7..0)
    for (int bit = 7; bit >= 0; --bit) {
        auto *header = new QLabel(QString("Bit %1").arg(bit));
        header->setAlignment(Qt::AlignCenter);
        header->setStyleSheet("font-weight: bold; color: #ffaa00; background-color: #1a1a2e;");
        m_grid->addWidget(header, 0, 8 - bit); // wiersz 0
    }

    buildGrid();

    layout->addLayout(m_grid);
    layout->addStretch();

    scrollArea->setWidget(scrollWidget);
    outerLayout->addWidget(scrollArea);

    setStyleSheet(R"(
        QLabel {
            font-family: "Consolas", "Courier New", monospace;
            padding: 2px 4px;
        }
    )");
}

void FrameDetailWidget::buildGrid() {
    // Czyścimy stare etykiety (jeśli istnieją)
    for (auto *lbl : m_byteLabels) delete lbl;
    for (auto &row : m_bitLabels) for (auto *lbl : row) delete lbl;
    m_byteLabels.clear();
    m_bitLabels.clear();

    // Tworzymy 8 wierszy dla bajtów 0..7 (klasyczny CAN)
    for (int byte = 0; byte < 8; ++byte) {
        int row = byte + 1; // wiersz 1..8

        // Etykieta bajtu
        auto *byteLabel = new QLabel("00");
        byteLabel->setAlignment(Qt::AlignCenter);
        byteLabel->setStyleSheet("background-color: #161b22; color: #00ffaa; font-weight: bold;");
        m_grid->addWidget(byteLabel, row, 0);
        m_byteLabels.append(byteLabel);

        // Etykiety bitów
        QVector<QLabel*> bitRow;
        for (int bit = 7; bit >= 0; --bit) {
            auto *bitLabel = new QLabel("0");
            bitLabel->setAlignment(Qt::AlignCenter);
            bitLabel->setStyleSheet("background-color: #0d1117; color: #c0c0c0;");
            m_grid->addWidget(bitLabel, row, 8 - bit); // kolumny 1..8
            bitRow.append(bitLabel);
        }
        m_bitLabels.append(bitRow);
    }
}

QLabel* FrameDetailWidget::createByteLabel(int byteIndex) {
    auto *lbl = new QLabel("00");
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setStyleSheet("background-color: #161b22; color: #00ffaa; font-weight: bold;");
    return lbl;
}

QString FrameDetailWidget::byteToBinary(uint8_t value) const {
    QString bin;
    for (int i = 7; i >= 0; --i) {
        bin += (value & (1 << i)) ? '1' : '0';
    }
    return bin;
}

void FrameDetailWidget::highlightChangedBits(const CanFrame &frame) {
    // Pobierz poprzednią ramkę dla tego ID
    auto it = m_lastFrameMap.find(frame.id);
    if (it != m_lastFrameMap.end()) {
        const CanFrame &prev = it.value();
        for (int byte = 0; byte < 8; ++byte) {
            uint8_t currVal = (byte < frame.dlc) ? frame.data[byte] : 0;
            uint8_t prevVal = (byte < prev.dlc) ? prev.data[byte] : 0;

            // Podświetl bajt, jeśli się zmienił
            if (currVal != prevVal) {
                m_byteLabels[byte]->setStyleSheet("background-color: #e94560; color: #ffffff; font-weight: bold;");
            }

            // Podświetl pojedyncze bity
            for (int bit = 0; bit < 8; ++bit) {
                bool currBit = (currVal >> (7 - bit)) & 1;
                bool prevBit = (prevVal >> (7 - bit)) & 1;
                if (currBit != prevBit) {
                    m_bitLabels[byte][bit]->setStyleSheet("background-color: #ff66cc; color: #ffffff; font-weight: bold;");
                }
            }
        }
    }

    // Zapamiętaj bieżącą ramkę
    m_lastFrameMap[frame.id] = frame;
}

void FrameDetailWidget::loadFrame(const CanFrame &frame) {
    // Aktualizuj nagłówek
    m_idLabel->setText(QString("ID: 0x%1").arg(frame.id, 3, 16, QChar('0')).toUpper());
    m_dlcLabel->setText(QString("DLC: %1").arg(frame.dlc));
    m_timestampLabel->setText(QString("Czas: %1 µs").arg(frame.timestamp));

    // Resetuj style przed aktualizacją
    for (int byte = 0; byte < 8; ++byte) {
        m_byteLabels[byte]->setStyleSheet("background-color: #161b22; color: #00ffaa; font-weight: bold;");
        for (int bit = 0; bit < 8; ++bit) {
            m_bitLabels[byte][bit]->setStyleSheet("background-color: #0d1117; color: #c0c0c0;");
        }
    }

    // Podświetl zmiany (robi to przed zapisaniem nowej ramki!)
    highlightChangedBits(frame);

    // Wypełnij bajty i bity
    for (int byte = 0; byte < 8; ++byte) {
        uint8_t value = (byte < frame.dlc) ? frame.data[byte] : 0;
        m_byteLabels[byte]->setText(QString("%1").arg(value, 2, 16, QChar('0')).toUpper());

        QString bin = byteToBinary(value);
        for (int bit = 7; bit >= 0; --bit) {
            m_bitLabels[byte][7 - bit]->setText(QString(bin[7 - bit]));
        }
    }
}
EOF

# 3. MainWindow.h – dodanie FrameDetailWidget i slotu
cat > src/gui/MainWindow.h << 'EOF'
#pragma once
#include <QMainWindow>
#include <QTableView>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QTimer>
#include <QVector>
#include "core/CanSniffer.h"
#include "core/CanFrameModel.h"
#include "core/AssociativeLearner.h"
#include "core/LuaScriptEngine.h"
#include "core/FrameDetailWidget.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void toggleSniffing();
    void onNewFrame(const CanFrame &frame);
    void updateTableBatch();
    void refreshInterfaces();
    void applyOverwriteMode(bool enabled);
    void onUserScroll(int value);
    void exportToCandump();
    void loadLuaScript();
    void onFrameSelected(const QModelIndex &index);   // NOWE

private:
    void setupStyle();
    void setupToolBar();
    void setupCentralWidget();

    CanSniffer m_sniffer;
    CanFrameModel *m_model;
    AssociativeLearner *m_learner;
    LuaScriptEngine *m_luaEngine;
    FrameDetailWidget *m_frameDetail;  // NOWE
    QTableView *m_tableView;
    QPushButton *m_btnStartStop;
    QComboBox *m_interfaceCombo;
    QCheckBox *m_overwriteCheck;
    QLabel *m_statusLabel;

    QTimer m_batchTimer;
    QVector<CanFrame> m_frameBuffer;
    bool m_sniffing = false;
    bool m_autoScroll = true;
};
EOF

# 4. MainWindow.cpp – podłączenie widoku
cat > src/gui/MainWindow.cpp << 'EOF'
#include "MainWindow.h"
#include "core/CanInterfaceEnumerator.h"
#include <QToolBar>
#include <QToolButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QApplication>
#include <QScrollBar>
#include <QFileDialog>
#include <QTextStream>

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

    m_learner = new AssociativeLearner;
    m_luaEngine = new LuaScriptEngine(this);
    m_luaEngine->setSniffer(&m_sniffer);
    m_frameDetail = new FrameDetailWidget;

    m_batchTimer.setInterval(33);
    connect(&m_batchTimer, &QTimer::timeout, this, &MainWindow::updateTableBatch);

    connect(&m_sniffer, &CanSniffer::newFrame, this, &MainWindow::onNewFrame, Qt::QueuedConnection);
    connect(&m_sniffer, &CanSniffer::errorOccurred, this, [this](const QString &msg) {
        QMessageBox::warning(this, "Błąd CAN", msg);
        m_sniffer.stop();
        m_sniffing = false;
        m_btnStartStop->setText("▶ Start");
        m_batchTimer.stop();
        m_statusLabel->setText("Rozłączony");
        m_statusLabel->setStyleSheet("color: #ff4444;");
    });
    connect(&m_sniffer, &CanSniffer::statusChanged, this, [this](bool running) {
        m_statusLabel->setText(running ? "Nasłuchuje..." : "Rozłączony");
        m_statusLabel->setStyleSheet(running ? "color: #00ffaa;" : "color: #ff4444;");
    });

    setupToolBar();
    setupCentralWidget();
    setupStyle();

    refreshInterfaces();
    if (m_interfaceCombo->count() > 0)
        m_interfaceCombo->setCurrentIndex(0);

    connect(&m_sniffer, &CanSniffer::newFrame, m_learner, &AssociativeLearner::processFrame, Qt::QueuedConnection);
    connect(&m_sniffer, &CanSniffer::newFrame, m_luaEngine, &LuaScriptEngine::onNewFrame, Qt::QueuedConnection);
    connect(m_tableView->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &MainWindow::onUserScroll);

    connect(m_luaEngine, &LuaScriptEngine::logMessage, this, [](const QString &msg) {
        qDebug() << "[Lua]" << msg;
    });
    connect(m_luaEngine, &LuaScriptEngine::errorOccurred, this, [](const QString &err) {
        qWarning() << "[Lua ERROR]" << err;
    });

    // Podłączenie kliknięcia w tabeli -> widok szczegółów
    connect(m_tableView, &QTableView::clicked, this, &MainWindow::onFrameSelected);
}

MainWindow::~MainWindow() {
    if (m_sniffing) {
        m_sniffer.stop();
        m_batchTimer.stop();
    }
}

void MainWindow::toggleSniffing() {
    if (!m_sniffing) {
        QString iface = m_interfaceCombo->currentText().trimmed();
        if (iface.isEmpty()) {
            QMessageBox::warning(this, "Brak interfejsu", "Wybierz interfejs CAN.");
            return;
        }
        m_sniffer.start(iface);
        m_sniffing = true;
        m_btnStartStop->setText("■ Stop");
        m_interfaceCombo->setEnabled(false);
        m_batchTimer.start();
    } else {
        m_sniffer.stop();
        m_sniffing = false;
        m_btnStartStop->setText("▶ Start");
        m_interfaceCombo->setEnabled(true);
        m_batchTimer.stop();
        m_frameBuffer.clear();
    }
}

void MainWindow::onNewFrame(const CanFrame &frame) {
    m_frameBuffer.append(frame);
}

void MainWindow::updateTableBatch() {
    if (m_frameBuffer.isEmpty()) return;
    m_model->processIncomingFrames(m_frameBuffer);
    m_frameBuffer.clear();
    if (m_autoScroll)
        m_tableView->scrollToBottom();
}

void MainWindow::refreshInterfaces() {
    QString current = m_interfaceCombo->currentText();
    m_interfaceCombo->clear();
    QStringList ifaces = CanInterfaceEnumerator::availableCanInterfaces();
    m_interfaceCombo->addItems(ifaces);
    int idx = m_interfaceCombo->findText(current);
    if (idx >= 0)
        m_interfaceCombo->setCurrentIndex(idx);
    else if (!current.isEmpty())
        m_interfaceCombo->setCurrentText(current);
}

void MainWindow::applyOverwriteMode(bool enabled) {
    m_model->setOverwriteMode(enabled);
}

void MainWindow::onUserScroll(int value) {
    QScrollBar *vbar = m_tableView->verticalScrollBar();
    if (!vbar) return;
    m_autoScroll = (value >= vbar->maximum() - 1);
}

void MainWindow::exportToCandump() {
    QString fileName = QFileDialog::getSaveFileName(this, "Eksportuj do candump", "", "Pliki candump (*.log *.txt);;Wszystkie pliki (*)");
    if (fileName.isEmpty()) return;

    QVector<CanFrame> frames = m_model->allFrames();
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Błąd", "Nie można zapisać pliku.");
        return;
    }

    QTextStream out(&file);
    QString iface = m_interfaceCombo->currentText().trimmed();
    if (iface.isEmpty()) iface = "vcan0";

    for (const auto &frame : frames) {
        QString line = QString("(%1) %2 %3#%4")
                .arg(frame.timestamp)
                .arg(iface)
                .arg(frame.id, frame.extended ? 8 : 3, 16, QChar('0'))
                .arg(frame.dlc < 8 ? QString::number(frame.dlc) : "8");
        for (int i = 0; i < frame.dlc && i < 8; ++i) {
            line += QString("%1").arg(frame.data[i], 2, 16, QChar('0')).toUpper();
        }
        out << line << "\n";
    }

    file.close();
    QMessageBox::information(this, "Eksport", QString("Wyeksportowano %1 ramek.").arg(frames.size()));
}

void MainWindow::loadLuaScript() {
    QString fileName = QFileDialog::getOpenFileName(this, "Wczytaj skrypt Lua", "", "Skrypty Lua (*.lua);;Wszystkie pliki (*)");
    if (fileName.isEmpty()) return;
    m_luaEngine->loadScript(fileName);
}

void MainWindow::onFrameSelected(const QModelIndex &index) {
    if (!index.isValid()) return;
    CanFrame frame = m_model->frameAt(index.row()); // wymaga metody frameAt w modelu
    m_frameDetail->loadFrame(frame);
}

void MainWindow::setupToolBar() {
    auto *toolbar = addToolBar("Główne");
    toolbar->setMovable(false);

    m_interfaceCombo = new QComboBox;
    m_interfaceCombo->setEditable(true);
    m_interfaceCombo->setMinimumWidth(120);
    m_interfaceCombo->setToolTip("Wybierz interfejs CAN (np. vcan0, can0)");
    toolbar->addWidget(m_interfaceCombo);

    auto *refreshBtn = new QPushButton("↻");
    refreshBtn->setToolTip("Odśwież listę interfejsów");
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshInterfaces);
    toolbar->addWidget(refreshBtn);

    toolbar->addSeparator();

    m_btnStartStop = new QPushButton("▶ Start");
    connect(m_btnStartStop, &QPushButton::clicked, this, &MainWindow::toggleSniffing);
    toolbar->addWidget(m_btnStartStop);

    m_overwriteCheck = new QCheckBox("Nadpisywanie");
    m_overwriteCheck->setChecked(true);
    connect(m_overwriteCheck, &QCheckBox::toggled, this, &MainWindow::applyOverwriteMode);
    toolbar->addWidget(m_overwriteCheck);

    toolbar->addSeparator();

    m_statusLabel = new QLabel("Rozłączony");
    m_statusLabel->setStyleSheet("color: #ff4444; font-weight: bold;");
    toolbar->addWidget(m_statusLabel);

    toolbar->addSeparator();

    QAction *clearAction = toolbar->addAction("🗙 Wyczyść");
    if (clearAction) {
        connect(clearAction, &QAction::triggered, this, [this]() {
            m_model->clear();
        });
        QToolButton *clearBtn = qobject_cast<QToolButton*>(toolbar->widgetForAction(clearAction));
        if (clearBtn) clearBtn->setObjectName("clearButton");
    }

    QAction *exportAction = toolbar->addAction("📥 Eksportuj candump");
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportToCandump);

    QAction *luaAction = toolbar->addAction("📜 Wczytaj skrypt Lua");
    connect(luaAction, &QAction::triggered, this, &MainWindow::loadLuaScript);
}

void MainWindow::setupCentralWidget() {
    auto *tabs = new QTabWidget;
    tabs->addTab(m_tableView, "Ruch CAN");
    tabs->addTab(m_learner, "Uczenie asocjacyjne");
    tabs->addTab(m_frameDetail, "Szczegóły ramki");   // NOWA ZAKŁADKA
    setCentralWidget(tabs);
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
        QComboBox {
            background: #1a1a2e;
            color: #00ffaa;
            border: 1px solid #e94560;
            border-radius: 4px;
            padding: 3px 8px;
            min-width: 100px;
        }
        QComboBox::drop-down { border: none; }
        QComboBox QAbstractItemView {
            background: #1a1a2e;
            color: #00ffaa;
            selection-background-color: #e94560;
        }
        QCheckBox {
            color: #ff66cc; font-weight: bold;
        }
        QCheckBox::indicator { width: 16px; height: 16px; }
        QLabel { color: #c0c0c0; }
        QTableView, QTableWidget {
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
        QToolButton#clearButton {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #1a1a2e, stop:0.5 #2c0735, stop:1 #e94560);
            color: #00ffaa;
            border: 1px solid #e94560;
            border-radius: 6px;
            padding: 6px 14px;
            font-weight: bold;
        }
        QToolButton#clearButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #e94560, stop:0.5 #2c0735, stop:1 #1a1a2e);
            color: #0a0e17;
            border: 1px solid #ff66cc;
        }
        QToolButton#clearButton:pressed {
            background: #2c0735;
            border: 1px solid #ff00ff;
        }
    )");
}
EOF

# 5. CanFrameModel – potrzebujemy metody frameAt()
sed -i '/void clear();/a\    CanFrame frameAt(int row) const;' src/core/CanFrameModel.h
sed -i '/^void CanFrameModel::clear()/i\
CanFrame CanFrameModel::frameAt(int row) const {\
    QMutexLocker lock(&m_mutex);\
    if (row >= 0 && row < m_frames.size())\
        return m_frames.at(row);\
    return CanFrame();\
}\n' src/core/CanFrameModel.cpp

echo "=== Widok szczegółów ramki dodany. Kompiluj: cd build && make -j\$(nproc) ==="
