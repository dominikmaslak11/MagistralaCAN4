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
#include <QStyle>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QClipboard>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("MagistralaCAN4 - Sniffer CAN");
    Logger::log("Aplikacja MagistralaCAN4 uruchomiona");
    resize(1280, 800);

    m_model = new CanFrameModel(this);
    m_tableView = new QTableView;
    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->verticalHeader()->hide();
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->setShowGrid(false);
    m_tableView->setAlternatingRowColors(false);
    m_tableView->setSortingEnabled(true);
    m_tableView->horizontalHeader()->setSortIndicatorShown(true);

    m_learner = new AssociativeLearner;
    // Lokalne skróty klawiszowe (zawsze działają przy aktywnym oknie)
    m_hotkeyMarkEvent = new QShortcut(QKeySequence("Ctrl+Shift+E"), this);
    connect(m_hotkeyMarkEvent, &QShortcut::activated, m_learner, &AssociativeLearner::markEvent);
    Logger::log("Lokalny skrót Ctrl+Shift+E utworzony");
    m_hotkeyNonEvent = new QShortcut(QKeySequence("Ctrl+Shift+D"), this);
    connect(m_hotkeyNonEvent, &QShortcut::activated, m_learner, &AssociativeLearner::markNonEvent);
    Logger::log("Lokalny skrót Ctrl+Shift+D utworzony");
    m_luaEngine = new LuaScriptEngine(this);
    m_luaEngine->setSniffer(&m_sniffer);
    m_frameDetail = new FrameDetailWidget;
    m_frameDetail->setSniffer(&m_sniffer);
    m_canDashboard = new CanDashboard;
    m_j1939Widget = new J1939Widget;
    m_learner->setJ1939Parser(m_j1939Widget->parser());
    m_model->setJ1939Parser(m_j1939Widget->parser());
    m_dbcEditor = new DbcEditorWidget;
    m_canSimWidget = new CanNodeSimWidget(&m_sniffer, m_luaEngine);
    m_remoteCanWidget = new RemoteCanWidget(&m_sniffer);
    m_udsWidget = new UdsWidget;
    m_logComparator = new LogComparatorWidget;
    m_obdWidget = new ObdWidget;
    m_canOpenWidget = new CanOpenWidget;
    m_restServer.setModel(m_model);
    connect(&m_restServer, &HttpRestServer::startRequested, this, [this]() { if (!m_sniffing) toggleSniffing(); });
    connect(&m_restServer, &HttpRestServer::stopRequested, this, [this]() { if (m_sniffing) toggleSniffing(); });
    m_pluginLoader.loadFromDirectory("./plugins");
    m_offlineAnalyzer = new OfflineAnalyzer(m_learner, m_luaEngine);
    setWindowIcon(QIcon(":/ico.png"));

    // --- System tray ---
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(QIcon(":/ico.png"));
    m_trayIcon->setToolTip("MagistralaCAN4");
    auto *trayMenu = new QMenu(this);
    trayMenu->addAction("Przywróć", this, &QMainWindow::show);
    trayMenu->addAction("Zamknij", qApp, &QApplication::quit);
    m_trayIcon->setContextMenu(trayMenu);
    m_trayIcon->show();
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::trayActivated);

    m_batchTimer.setInterval(33);
    connect(&m_batchTimer, &QTimer::timeout, this, &MainWindow::updateTableBatch);

    connect(&m_sniffer, &CanSniffer::newFrame, this, &MainWindow::onNewFrame, Qt::QueuedConnection);
    connect(&m_sniffer, &CanSniffer::errorOccurred, this, [this](const QString &msg) {
        QMessageBox::warning(this, "Błąd CAN", msg);
        Logger::log(QString("Błąd CAN: %1").arg(msg));
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

    // Ctrl+C – kopiuj zaznaczone ramki
    auto *copyShortcut = new QShortcut(QKeySequence("Ctrl+C"), m_tableView);
    connect(copyShortcut, &QShortcut::activated, this, &MainWindow::copySelectedToClipboard);

    // Statystyki CAN – timer co 500ms
    m_canStatsTimer.setInterval(500);
    connect(&m_canStatsTimer, &QTimer::timeout, this, &MainWindow::updateCanStats);
    m_canStatsTimer.start();

    refreshInterfaces();
    if (m_interfaceCombo->count() > 0)
        m_interfaceCombo->setCurrentIndex(0);

    // Analiza ramek po przetworzeniu przez model (Direct, główny wątek)
    connect(this, &MainWindow::frameProcessed, m_learner, &AssociativeLearner::processFrame);
    connect(this, &MainWindow::frameProcessed, m_luaEngine, &LuaScriptEngine::onNewFrame);
    connect(this, &MainWindow::frameProcessed, m_canDashboard, &CanDashboard::updateSignal);
    connect(this, &MainWindow::frameProcessed, m_j1939Widget, &J1939Widget::processFrame);
    connect(this, &MainWindow::frameProcessed, m_canSimWidget->simulator(), &CanNodeSimulator::onNewFrame);
    connect(this, &MainWindow::frameProcessed, m_udsWidget, &UdsWidget::processFrame);
    connect(this, &MainWindow::frameProcessed, m_obdWidget, &ObdWidget::processFrame);
    connect(this, &MainWindow::frameProcessed, m_canOpenWidget, &CanOpenWidget::processFrame);
    connect(this, &MainWindow::frameProcessed, &m_recorder, &CanRecorder::recordFrame);
    connect(this, &MainWindow::frameProcessed, &m_mdf4Writer, &Mdf4Writer::recordFrame);
    connect(this, &MainWindow::frameProcessed, &m_mqttBridge, &MqttBridge::onNewFrame);
    connect(this, &MainWindow::frameProcessed, &m_pluginLoader, &PluginLoader::broadcastFrame);
    connect(m_tableView->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::onUserScroll);
    connect(m_luaEngine, &LuaScriptEngine::logMessage, this, [](const QString &msg) { qDebug() << "[Lua]" << msg; });
    connect(m_luaEngine, &LuaScriptEngine::errorOccurred, this, [](const QString &err) { qWarning() << "[Lua ERROR]" << err; });
    connect(m_learner, &AssociativeLearner::eventMarked, this, [this](int iteration) {
        showTrayNotification("Zdarzenie", QString("Zarejestrowano zdarzenie #%1").arg(iteration));
        Logger::log(QString("Zarejestrowano zdarzenie #%1").arg(iteration));
    });
    connect(m_learner, &AssociativeLearner::anomalyDetected, this, [this]() {
        showTrayNotification("Anomalia", "Wykryto anomalię na magistrali!");
        Logger::log("Wykryto anomalię na magistrali!");
    });
    connect(m_tableView, &QTableView::clicked, this, &MainWindow::onFrameSelected);

    // Zdalny CAN – serwer: broadcast lokalnych ramek do klientów WSS
    connect(m_model, &CanFrameModel::frameUpdated,
            m_remoteCanWidget->server(), &WebSocketServer::broadcastFrame);

    // Zdalny CAN – klient: wstrzykiwanie odebranych ramek do pipeline'u
    connect(m_remoteCanWidget->client(), &RemoteCanClient::newFrame,
            this, &MainWindow::onNewFrame, Qt::QueuedConnection);
    connect(m_remoteCanWidget->client(), &RemoteCanClient::newFrame,
            m_learner, &AssociativeLearner::processFrame, Qt::QueuedConnection);
    connect(m_remoteCanWidget->client(), &RemoteCanClient::newFrame,
            m_luaEngine, &LuaScriptEngine::onNewFrame, Qt::QueuedConnection);

    // Edytor DBC → podmiana parsera w dashboardzie i szczegółach
    connect(m_dbcEditor, &DbcEditorWidget::dbcApplied, this, [this]() {
        const DbcParser *parser = m_dbcEditor->parser();
        m_frameDetail->setDbcParser(const_cast<DbcParser*>(parser));
        m_canDashboard->setDbcParser(parser);
        m_learner->setDbcParser(parser);
        m_mqttBridge.setDbcParser(parser);
    });
}

MainWindow::~MainWindow() {
    if (m_sniffing) { m_sniffer.stop(); m_batchTimer.stop(); }
}

void MainWindow::toggleSniffing() {
    if (!m_sniffing) {
        QString iface = m_interfaceCombo->currentText().trimmed();
        if (iface.isEmpty()) { QMessageBox::warning(this, "Brak interfejsu", "Wybierz interfejs CAN."); return; }
        m_sniffer.start(iface); m_sniffing = true;
        Logger::log(QString("Rozpoczęto sniffing na interfejsie %1").arg(iface));
        m_btnStartStop->setText("■ Stop"); m_interfaceCombo->setEnabled(false); m_batchTimer.start();
    } else {
        m_sniffer.stop(); m_sniffing = false;
        m_btnStartStop->setText("▶ Start"); m_interfaceCombo->setEnabled(true); m_batchTimer.stop();
        m_frameBuffer.clear();
    }
}

void MainWindow::onNewFrame(const CanFrame &frame) {
    m_frameBuffer.append(frame);
    m_totalFrameCount++;
    m_uniqueIdsSinceLastStats.insert(frame.id);
}

void MainWindow::updateTableBatch() {
    if (m_frameBuffer.isEmpty()) return;
    if (m_canPaused) return; // buforuj w tle, nie odświeżaj tabeli

    // Emituj ramki do slotów analizy (DirectConnection, zero nadmiarowych kopii)
    for (const CanFrame &frame : m_frameBuffer)
        emit frameProcessed(frame);

    m_model->processIncomingFrames(m_frameBuffer);
    m_frameBuffer.clear();
    if (m_autoScroll) m_tableView->scrollToBottom();
}

void MainWindow::refreshInterfaces() {
    QString current = m_interfaceCombo->currentText();
    m_interfaceCombo->clear();
    QStringList ifaces = CanInterfaceEnumerator::availableCanInterfaces();
    m_interfaceCombo->addItems(ifaces);
    int idx = m_interfaceCombo->findText(current);
    if (idx >= 0) m_interfaceCombo->setCurrentIndex(idx);
    else if (!current.isEmpty()) m_interfaceCombo->setCurrentText(current);
}

void MainWindow::applyOverwriteMode(bool enabled) { m_model->setOverwriteMode(enabled); }

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
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) { QMessageBox::warning(this, "Błąd", "Nie można zapisać pliku."); return; }
    QTextStream out(&file);
    QString iface = m_interfaceCombo->currentText().trimmed(); if (iface.isEmpty()) iface = "vcan0";
    for (const auto &frame : frames) {
        QString line = QString("(%1) %2 %3#%4").arg(frame.timestamp).arg(iface).arg(frame.id, frame.extended ? 8 : 3, 16, QChar('0')).arg(frame.dlc < 8 ? QString::number(frame.dlc) : "8");
        for (int i = 0; i < frame.dlc && i < 8; ++i) line += QString("%1").arg(frame.data[i], 2, 16, QChar('0')).toUpper();
        out << line << "\n";
    }
    file.close();
    QMessageBox::information(this, "Eksport", QString("Wyeksportowano %1 ramek.").arg(frames.size()));
        Logger::log(QString("Wyeksportowano %1 ramek do candump").arg(frames.size()));
}

void MainWindow::exportToCsv() {
    QString fileName = QFileDialog::getSaveFileName(this, "Eksportuj do CSV", "", "CSV (*.csv)");
    if (fileName.isEmpty()) return;
    QVector<CanFrame> frames = m_model->allFrames();
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&file);
    // Nagłówek
    out << "Index,Timestamp_us,ID(hex),Type,RTR,DLC,Data(hex),FD\n";
    for (int i = 0; i < frames.size(); ++i) {
        const auto &f = frames[i];
        QString data;
        for (int b = 0; b < f.dlc && b < 8; ++b)
            data += QString("%1").arg(f.data[b], 2, 16, QChar('0')).toUpper();
        out << i << "," << f.timestamp << ",0x" << QString::number(f.id, 16).toUpper()
            << "," << (f.extended ? "EXT" : "STD") << "," << (f.rtr ? "RTR" : "Data")
            << "," << f.dlc << "," << data << ","
            << (f.xl ? "XL" : f.fd ? "FD" : "CAN") << "\n";
    }
    file.close();
    Logger::log(QString("Wyeksportowano %1 ramek do CSV").arg(frames.size()));
    QMessageBox::information(this, "Eksport CSV", QString("Wyeksportowano %1 ramek.").arg(frames.size()));
}

void MainWindow::loadLuaScript() {
    QString fileName = QFileDialog::getOpenFileName(this, "Wczytaj skrypt Lua", "", "Skrypty Lua (*.lua);;Wszystkie pliki (*)");
    if (fileName.isEmpty()) return;
    m_luaEngine->loadScript(fileName);
        Logger::log(QString("Załadowano skrypt Lua: %1").arg(fileName));
}

void MainWindow::loadDbcFile() {
    QString fileName = QFileDialog::getOpenFileName(this, "Wczytaj plik DBC", "", "Pliki DBC (*.dbc);;Wszystkie pliki (*)");
    if (fileName.isEmpty()) return;
    if (m_dbcParser.load(fileName)) {
        m_frameDetail->setDbcParser(&m_dbcParser);
        m_canDashboard->setDbcParser(&m_dbcParser);
        m_learner->setDbcParser(&m_dbcParser);
        m_model->setDbcParser(&m_dbcParser);
        m_mqttBridge.setDbcParser(&m_dbcParser);
        Logger::log(QString("Załadowano plik DBC: %1").arg(fileName));
        QMessageBox::information(this, "DBC", "Plik DBC załadowany pomyślnie.");
    } else {
        QMessageBox::warning(this, "DBC", "Nie udało się wczytać pliku DBC.");
    }
}

void MainWindow::onFrameSelected(const QModelIndex &index) {
    if (!index.isValid()) return;
    CanFrame frame = m_model->frameAt(index.row());
    m_frameDetail->loadFrame(frame);
}

void MainWindow::setupToolBar() {
    auto *toolbar = addToolBar("Główne"); toolbar->setMovable(false);
    m_interfaceCombo = new QComboBox; m_interfaceCombo->setEditable(true); m_interfaceCombo->setMinimumWidth(120);
    toolbar->addWidget(m_interfaceCombo);
    auto *refreshBtn = new QPushButton("↻"); connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshInterfaces);
    toolbar->addWidget(refreshBtn);
    toolbar->addSeparator();
    m_btnStartStop = new QPushButton("▶ Start"); connect(m_btnStartStop, &QPushButton::clicked, this, &MainWindow::toggleSniffing);
    toolbar->addWidget(m_btnStartStop);
    m_overwriteCheck = new QCheckBox("Nadpisywanie"); m_overwriteCheck->setChecked(true); connect(m_overwriteCheck, &QCheckBox::toggled, this, &MainWindow::applyOverwriteMode);
    toolbar->addWidget(m_overwriteCheck);
    toolbar->addSeparator();
    m_statusLabel = new QLabel("Rozłączony"); m_statusLabel->setStyleSheet("color: #ff4444; font-weight: bold;");
    toolbar->addWidget(m_statusLabel);
    toolbar->addSeparator();
    QAction *clearAction = toolbar->addAction("🗙 Wyczyść"); if (clearAction) { connect(clearAction, &QAction::triggered, [this]() { m_model->clear(); }); QToolButton *clearBtn = qobject_cast<QToolButton*>(toolbar->widgetForAction(clearAction)); if (clearBtn) clearBtn->setObjectName("clearButton"); }
    QAction *exportAction = toolbar->addAction("📥 Eksportuj candump"); connect(exportAction, &QAction::triggered, this, &MainWindow::exportToCandump);
    QAction *csvAction = toolbar->addAction("📊 Eksportuj CSV"); connect(csvAction, &QAction::triggered, this, &MainWindow::exportToCsv);
    QAction *recAction = toolbar->addAction("⏺ Nagraj"); connect(recAction, &QAction::triggered, this, &MainWindow::toggleRecording);
    QAction *mdf4Action = toolbar->addAction("📦 Nagraj MDF4"); connect(mdf4Action, &QAction::triggered, this, &MainWindow::toggleMdf4Recording);
    QAction *restAction = toolbar->addAction("🌐 REST API"); connect(restAction, &QAction::triggered, this, &MainWindow::toggleRestApi);
    QAction *mqttAction = toolbar->addAction("📡 MQTT"); connect(mqttAction, &QAction::triggered, this, &MainWindow::toggleMqtt);
    QAction *themeAction = toolbar->addAction("☀️ Jasny motyw"); connect(themeAction, &QAction::triggered, this, &MainWindow::toggleTheme);
    QAction *luaAction = toolbar->addAction("📜 Wczytaj skrypt Lua"); connect(luaAction, &QAction::triggered, this, &MainWindow::loadLuaScript);
    QAction *dbcAction = toolbar->addAction("🗄️ Wczytaj DBC"); connect(dbcAction, &QAction::triggered, this, &MainWindow::loadDbcFile);
}

void MainWindow::setupCentralWidget() {
    auto *tabs = new QTabWidget;

    // Zakładka "Ruch CAN" z paskiem statystyk i filtrem ID
    auto *canTab = new QWidget;
    auto *canLayout = new QVBoxLayout(canTab);
    canLayout->setContentsMargins(0, 0, 0, 0);

    auto *canHeader = new QHBoxLayout;
    m_canFilterEdit = new QLineEdit;
    m_canFilterEdit->setPlaceholderText("Filtruj po CAN ID (hex, np. 123 lub 0x123)...");
    m_canFilterEdit->setStyleSheet("QLineEdit { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560; "
                                    "border-radius: 4px; padding: 5px 10px; font-size: 12px; }");
    connect(m_canFilterEdit, &QLineEdit::textChanged, this, &MainWindow::applyIdFilter);
    canHeader->addWidget(m_canFilterEdit, 1);

    m_canStatsLabel = new QLabel("Ramki: 0 | FPS: 0 | Unikalne ID: 0 | Obciążenie: 0%");
    m_canStatsLabel->setStyleSheet("color: #ffaa00; font-weight: bold; font-size: 11px; "
                                    "background: #1a1a2e; padding: 4px 10px; border-radius: 4px;");
    canHeader->addWidget(m_canStatsLabel);

    m_canPauseBtn = new QPushButton("⏸ Pauza");
    m_canPauseBtn->setFixedWidth(90);
    m_canPauseBtn->setStyleSheet("QPushButton { background: #1a1a2e; color: #ffaa00; border: 1px solid #e94560; "
                                  "border-radius: 4px; padding: 4px 10px; font-weight: bold; font-size: 11px; } "
                                  "QPushButton:hover { background: #e94560; color: #0a0e17; }");
    connect(m_canPauseBtn, &QPushButton::clicked, this, &MainWindow::toggleCanPause);
    canHeader->addWidget(m_canPauseBtn);

    // Mini-wykres FPS
    m_fpsChart = new QChart();
    m_fpsChart->setMargins(QMargins(0,0,0,0));
    m_fpsChart->setBackgroundRoundness(0);
    m_fpsChart->legend()->hide();
    m_fpsSeries = new QLineSeries(); m_fpsSeries->setColor(QColor("#00ffaa"));
    m_fpsChart->addSeries(m_fpsSeries);
    m_fpsChart->createDefaultAxes();
    m_fpsChart->axes(Qt::Horizontal).first()->setVisible(false);
    m_fpsChart->axes(Qt::Vertical).first()->setVisible(false);
    m_fpsChartView = new QChartView(m_fpsChart);
    m_fpsChartView->setRenderHint(QPainter::Antialiasing);
    m_fpsChartView->setFixedSize(120, 30);
    m_fpsChartView->setStyleSheet("background: transparent;");
    canHeader->addWidget(m_fpsChartView);

    canLayout->addLayout(canHeader);
    canLayout->addWidget(m_tableView);

    tabs->addTab(canTab, "Ruch CAN");
    tabs->addTab(m_learner, "Uczenie asocjacyjne");
    tabs->addTab(m_frameDetail, "Szczegóły ramki");
    tabs->addTab(m_offlineAnalyzer, "Analiza offline");
    tabs->addTab(m_canDashboard, "Dashboard CAN");
    tabs->addTab(m_j1939Widget, "Diagnostyka J1939");
    tabs->addTab(m_dbcEditor, "Edytor DBC");
    tabs->addTab(m_canSimWidget, "Symulacja CAN");
    tabs->addTab(m_remoteCanWidget, "Zdalny CAN");
    tabs->addTab(m_udsWidget, "Diagnostyka UDS");
    tabs->addTab(m_logComparator, "Porównywarka logów");
    tabs->addTab(m_obdWidget, "Diagnostyka OBD-II");
    tabs->addTab(m_canOpenWidget, "CANopen");
    // Wtyczki z plugins/
    for (auto *plugin : m_pluginLoader.plugins())
        if (auto *w = plugin->widget())
            tabs->addTab(w, plugin->name());
    setCentralWidget(tabs);
}

void MainWindow::showTrayNotification(const QString &title, const QString &message) {
    if (m_trayIcon) m_trayIcon->showMessage(title, message, QSystemTrayIcon::Information, 5000);
}

void MainWindow::trayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger) {
        show();
        activateWindow();
    }
}

void MainWindow::updateCanStats() {
    uint64_t delta = m_totalFrameCount - m_lastStatsFrameCount;
    double fps = delta / 0.5; // 500ms timer
    int uniqueIds = m_uniqueIdsSinceLastStats.size();
    // Szacunkowe obciążenie: 8B * fps / 500kbps (dla CAN 2.0)
    double busLoad = (delta * 8 * 8.0) / (500'000 * 0.5) * 100.0;
    if (busLoad > 100) busLoad = 100;

    m_canStatsLabel->setText(QString("Ramki: %1 | FPS: %2 | Unikalne ID: %3 | Obc.: %4%")
                             .arg(m_totalFrameCount).arg(fps, 0, 'f', 0).arg(uniqueIds)
                             .arg(busLoad, 0, 'f', 1));
    m_lastStatsFrameCount = m_totalFrameCount;
    m_uniqueIdsSinceLastStats.clear();

    m_restServer.fps = fps;
    m_restServer.uniqueIds = uniqueIds;

    // Aktualizuj mini-wykres FPS (ostatnie 60 próbek = 30s)
    m_fpsSeries->append(m_fpsHistoryCount, fps);
    m_fpsHistoryCount++;
    if (m_fpsSeries->count() > 60)
        m_fpsSeries->removePoints(0, m_fpsSeries->count() - 60);
    m_fpsChart->axes(Qt::Vertical).first()->setRange(0, std::max(100.0, fps * 1.5));
}

void MainWindow::applyIdFilter(const QString &text) {
    QString filter = text.trimmed();
    if (filter.startsWith("0x", Qt::CaseInsensitive))
        filter = filter.mid(2);
    bool filterActive = !filter.isEmpty();
    uint32_t filterId = filterActive ? filter.toUInt(nullptr, 16) : 0;

    for (int i = 0; i < m_model->rowCount(); ++i) {
        CanFrame f = m_model->frameAt(i);
        bool show = !filterActive || (f.id == filterId);
        m_tableView->setRowHidden(i, !show);
    }
}

void MainWindow::toggleCanPause() {
    m_canPaused = !m_canPaused;
    if (m_canPaused) {
        m_canPauseBtn->setText("▶ Wznów");
        m_canPauseBtn->setStyleSheet("QPushButton { background: #e94560; color: #0a0e17; border: 1px solid #e94560; "
                                      "border-radius: 4px; padding: 4px 10px; font-weight: bold; font-size: 11px; }");
        m_canStatsLabel->setStyleSheet("color: #ff4444; font-weight: bold; font-size: 11px; "
                                        "background: #1a1a2e; padding: 4px 10px; border-radius: 4px;");
    } else {
        m_canPauseBtn->setText("⏸ Pauza");
        m_canPauseBtn->setStyleSheet("QPushButton { background: #1a1a2e; color: #ffaa00; border: 1px solid #e94560; "
                                      "border-radius: 4px; padding: 4px 10px; font-weight: bold; font-size: 11px; } "
                                      "QPushButton:hover { background: #e94560; color: #0a0e17; }");
        m_canStatsLabel->setStyleSheet("color: #ffaa00; font-weight: bold; font-size: 11px; "
                                        "background: #1a1a2e; padding: 4px 10px; border-radius: 4px;");
        // Wznów – przetwórz zbuforowane ramki
        if (!m_frameBuffer.isEmpty()) {
            m_model->processIncomingFrames(m_frameBuffer);
            m_frameBuffer.clear();
        }
    }
}

void MainWindow::copySelectedToClipboard() {
    auto sel = m_tableView->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;

    QStringList lines;
    // Nagłówek
    QStringList hdr;
    for (int c = 0; c < m_model->columnCount(); ++c)
        hdr << m_model->headerData(c, Qt::Horizontal).toString();
    lines << hdr.join('\t');

    for (const auto &idx : sel) {
        QStringList cols;
        for (int c = 0; c < m_model->columnCount(); ++c)
            cols << m_model->data(m_model->index(idx.row(), c)).toString();
        lines << cols.join('\t');
    }
    QApplication::clipboard()->setText(lines.join('\n'));
}

void MainWindow::toggleRecording() {
    if (m_recorder.isRecording()) {
        m_recorder.stopRecording();
    } else {
        QString path = QFileDialog::getSaveFileName(this, "Nagraj sesję CAN", "sesja.mcan", "MCAN (*.mcan)");
        if (path.isEmpty()) return;
        m_recorder.startRecording(path);
    }
}

void MainWindow::toggleRestApi() {
    if (m_restServer.isRunning()) {
        m_restServer.stop();
    } else {
        m_restServer.start(8080);
    }
}

static bool s_darkTheme = true;

void MainWindow::toggleMdf4Recording() {
    if (m_mdf4Writer.isRecording()) {
        m_mdf4Writer.stop();
    } else {
        QString path = QFileDialog::getSaveFileName(this, "Nagraj sesję MDF4", "sesja.mf4", "MDF4 (*.mf4)");
        if (path.isEmpty()) return;
        m_mdf4Writer.start(path);
    }
}

void MainWindow::toggleMqtt() {
    m_mqttBridge.setEnabled(!m_mqttBridge.isEnabled());
}

void MainWindow::toggleTheme() {
    s_darkTheme = !s_darkTheme;
    if (s_darkTheme) {
        setupStyle();
    } else {
        qApp->setStyleSheet(R"(
            QMainWindow { background-color: #f0f0f0; }
            QToolBar { background: #e8e8e8; border-bottom: 2px solid #ccc; spacing: 8px; padding: 4px; }
            QPushButton, QToolButton { background: #ddd; color: #333; border: 1px solid #999; border-radius: 4px; padding: 5px 15px; font-weight: bold; }
            QPushButton:hover, QToolButton:hover { background: #ccc; }
            QComboBox { background: #fff; color: #333; border: 1px solid #999; border-radius: 4px; padding: 3px 8px; }
            QCheckBox { color: #333; }
            QLabel { color: #333; }
            QTableView, QTableWidget { background-color: #fff; color: #333; gridline-color: #ddd; font-family: Consolas, monospace; font-size: 12px; }
            QHeaderView::section { background-color: #e0e0e0; color: #333; font-weight: bold; padding: 4px; border: none; border-bottom: 2px solid #999; }
        )");
    }
}

void MainWindow::setupStyle() {
    qApp->setStyleSheet(R"(
        QMainWindow { background-color: #0a0e17; }
        QToolBar { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #1a1a2e, stop:1 #16213e); border-bottom: 2px solid #e94560; spacing: 8px; padding: 4px; }
        QPushButton, QToolButton { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560; border-radius: 4px; padding: 5px 15px; font-weight: bold; }
        QPushButton:hover, QToolButton:hover { background: #e94560; color: #0a0e17; }
        QComboBox { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560; border-radius: 4px; padding: 3px 8px; min-width: 100px; }
        QComboBox::drop-down { border: none; }
        QComboBox QAbstractItemView { background: #1a1a2e; color: #00ffaa; selection-background-color: #e94560; }
        QCheckBox { color: #ff66cc; font-weight: bold; }
        QCheckBox::indicator { width: 16px; height: 16px; }
        QLabel { color: #c0c0c0; }
        QTableView, QTableWidget { background-color: #0a0e17; alternate-background-color: #161b22; color: #c0c0c0; gridline-color: #2a2a3c; selection-background-color: #e94560; selection-color: #ffffff; font-family: "Consolas", "Courier New", monospace; font-size: 12px; }
        QHeaderView::section { background-color: #1a1a2e; color: #ff66cc; font-weight: bold; padding: 4px; border: none; border-bottom: 2px solid #e94560; }
    )");
}
