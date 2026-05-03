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
