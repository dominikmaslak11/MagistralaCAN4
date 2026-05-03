#include "MainWindow.h"
#include "core/CanInterfaceEnumerator.h"
#include <QToolBar>
#include <QToolButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
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

    m_learner = new AssociativeLearner;

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

    // Przekazuj ramki do uczenia asocjacyjnego
    connect(&m_sniffer, &CanSniffer::newFrame, m_learner, &AssociativeLearner::processFrame, Qt::QueuedConnection);
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
        if (clearBtn) {
            clearBtn->setObjectName("clearButton");
        }
    }
}

void MainWindow::setupCentralWidget() {
    auto *tabs = new QTabWidget;
    tabs->addTab(m_tableView, "Ruch CAN");
    tabs->addTab(m_learner, "Uczenie asocjacyjne");
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
