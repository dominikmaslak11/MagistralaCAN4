#include "MainWindow.h"
#include "core/PcapExporter.h"
#include <QOpenGLWidget>
#ifdef Q_OS_WIN
#include "core/PcanDriver.h"
#else
#include "core/SocketCanDriver.h"
#include "core/CanInterfaceEnumerator.h"
#endif
#include "core/SlCanDriver.h"
#include "core/EspMcpDriver.h"

#include <QToolBar>
#include <QToolButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QApplication>
#include <QScrollBar>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QTextStream>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QClipboard>
#include <QInputDialog>
#include <QDir>
#include <QSettings>
#include <QCloseEvent>
#include <QProgressDialog>
#include <QtConcurrent>
#include <QFutureWatcher>
#include "core/DataHighlightDelegate.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("MagistralaCAN4 - Sniffer CAN");
    Logger::log("Aplikacja MagistralaCAN4 uruchomiona");
    resize(1280, 800);

    m_frameBuffer.reserve(4096);  // pre-alloc, unikamy realokacji

    m_model = new CanFrameModel(this);
    m_filterProxy = new CanFilterProxy(this);
    m_filterProxy->setSourceModel(m_model);
    m_tableView = new QTableView;
    m_tableView->setModel(m_filterProxy);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->verticalHeader()->hide();
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->setShowGrid(false);
    m_tableView->setAlternatingRowColors(false);
    m_tableView->setSortingEnabled(true);
    m_tableView->horizontalHeader()->setSortIndicatorShown(true);
    m_tableView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel); // smooth scrolling

    // Podświetlanie zmienionych bajtów w kolumnie DATA
    m_tableView->setItemDelegateForColumn(CanFrameModel::Column::DATA,
                                          new DataHighlightDelegate(m_tableView));

    // Menu kontekstowe prawego przycisku
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tableView, &QTableView::customContextMenuRequested,
            this, &MainWindow::onTableContextMenu);

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
    m_customDashboard = new CanCustomDashboard;
    m_forensicsWidget  = new CanForensicsWidget;
    m_triggerWidget    = new CanTriggerWidget;
    m_signalStatsWidget = new CanSignalStatisticsWidget;
    m_busHealthWidget   = new CanBusHealthWidget;
    m_j1939Widget = new J1939Widget;
    m_learner->setJ1939Parser(m_j1939Widget->parser());
    m_model->setJ1939Parser(m_j1939Widget->parser());
    m_dbcEditor = new DbcEditorWidget;
    m_canSimWidget = new CanNodeSimWidget(&m_sniffer, m_luaEngine);
    m_remoteCanWidget = new RemoteCanWidget(&m_sniffer);
    // m_udsWidget, m_obdWidget, m_canOpenWidget, m_someIpWidget, m_doIpWidget — lazy (LazyTabWidget)
    // m_logComparator — lazy
    m_signalPlotter  = new SignalPlotterWidget;
    m_dbcAutoWidget  = new DbcAutoWidget;
    m_dbcAutoWidget->setDbcEditor(m_dbcEditor);
    m_busLoadWidget  = new CanBusLoadWidget;
    m_frameSender    = new CanFrameSenderWidget(&m_sniffer);
    m_gatewayWidget  = new CanGatewayWidget;
    if (m_canDriver)    m_gatewayWidget->addDriver("PCAN/SocketCAN", m_canDriver);
    if (m_slCanDriver)  m_gatewayWidget->addDriver("SLCAN", m_slCanDriver);
    if (m_espMcpDriver) m_gatewayWidget->addDriver("ESP-MCP2515", m_espMcpDriver);
    m_udsSequenceWidget = new UdsSequenceWidget(&m_sniffer);
    // m_linWidget, m_kwp2000Widget, m_xcpWidget, m_replayFilterWidget — lazy (LazyTabWidget)
    m_idStatsWidget  = new CanIdStatsWidget;
    m_signalMonitorWidget  = new CanSignalMonitorWidget;
    m_periodicSenderWidget = new CanPeriodicSenderWidget(&m_sniffer);
    m_observationDbWidget  = new CanObservationDbWidget;
    m_alertWidget          = new CanAlertWidget;
    m_alertWidget->setScorer([this]() { return m_learner->scoreLatestWindow(); });
    m_timelineWidget       = new CanProtocolTimelineWidget;
    m_heatmapWidget          = new CanByteHeatmapWidget;
    m_moduleProfilerWidget   = new CanModuleProfilerWidget(&m_sniffer, this);
    m_moduleProfilerWidget->setAlertEngine(m_alertWidget->engine());
    m_protoExporterWidget    = new CanPrototypeExporterWidget(this);
    m_protoExporterWidget->setFrameModel(m_model);
    m_icSimWidget            = new IcSimWidget(&m_sniffer, this);
    m_restServer.setModel(m_model);
    m_restServer.setAlertEngine(m_alertWidget->engine());
    connect(&m_restServer, &HttpRestServer::startRequested,    this, [this]() { if (!m_sniffing) toggleSniffing(); });
    connect(&m_restServer, &HttpRestServer::stopRequested,     this, [this]() { if (m_sniffing)  toggleSniffing(); });
    connect(&m_restServer, &HttpRestServer::sendFrameRequested,
            this, [this](const CanFrame &f) { m_sniffer.writeFrame(f); });
    m_pluginLoader.loadFromDirectory("./plugins");
    m_offlineAnalyzer = new OfflineAnalyzer(m_learner, m_luaEngine);
    m_offlineAnalyzer->setSniffer(&m_sniffer);

    // --- CAN Driver ---
#ifdef Q_OS_WIN
    m_canDriver = new PcanDriver();
#else
    m_canDriver = new SocketCanDriver();
#endif
    // SLCAN – zawsze dostępny (porty szeregowe), cross-platform
    m_slCanDriver = new SlCanDriver();
    // ESP32+MCP2515 – ASCII serial sniffer, 115200 baud
    m_espMcpDriver = new EspMcpDriver();
    m_canDriver->setBaudRate("500K");
    m_slCanDriver->setBaudRate("500K");
    m_sniffer.setDriver(m_canDriver);

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

    connect(&m_sniffer, &CanSniffer::newFrame, this, &MainWindow::onNewFrame);  // direct — ring buffer decouples threads
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

    // Panel statystyk CAN (Faza 2.2 — musi być przed setupCentralWidget!)
    m_canStatsPanel = new CanStatsPanel(this);
    connect(m_canStatsPanel, &CanStatsPanel::filterChanged, this, &MainWindow::applyIdFilter);
    connect(m_canStatsPanel, &CanStatsPanel::pauseToggled, this, [this](bool paused) {
        // Bufor jest stopniowo opróżniany przez updateTableBatch() (timer 33ms, batch 500 ramek)
        // po odblokowaniu pauzy — nie czyścimy go tutaj.
        Q_UNUSED(paused);
    });
    connect(m_canStatsPanel, &CanStatsPanel::statsUpdated, this, [this](double fps, int uniqueIds) {
        m_restServer.fps = fps;
        m_restServer.uniqueIds = uniqueIds;
    });

    setupToolBar();
    if (m_offlineAnalyzer && m_interfaceCombo)
        m_offlineAnalyzer->setInterface(m_interfaceCombo->currentText());
    setupCentralWidget();
    setupStyle();

    // Ctrl+C – kopiuj zaznaczone ramki
    auto *copyShortcut = new QShortcut(QKeySequence("Ctrl+C"), m_tableView);
    connect(copyShortcut, &QShortcut::activated, this, &MainWindow::copySelectedToClipboard);

    // Ctrl+Z / Ctrl+Y – undo/redo modelu
    auto *undoShortcut = new QShortcut(QKeySequence("Ctrl+Z"), this);
    connect(undoShortcut, &QShortcut::activated, this, [this]() { m_model->undo(); });
    auto *redoShortcut = new QShortcut(QKeySequence("Ctrl+Y"), this);
    connect(redoShortcut, &QShortcut::activated, this, [this]() { m_model->redo(); });

    // Odtwarzanie nagrań
    m_player = new CanPlayer(this);
    connect(m_player, &CanPlayer::frameEmitted, this, &MainWindow::onNewFrame);
    connect(m_player, &CanPlayer::playbackFinished, this, [this]() {
        if (m_playPauseBtn) m_playPauseBtn->setText("▶ Odtwórz");
    });

    refreshInterfaces();
    if (m_interfaceCombo->count() > 0)
        m_interfaceCombo->setCurrentIndex(0);

    // Analiza ramek po przetworzeniu przez model (Direct, główny wątek)
    // Krytyczne sloty — każda ramka: nagrywanie, uczenie, forwarding
    connect(this, &MainWindow::frameProcessed, m_learner, &AssociativeLearner::processFrame);
    connect(this, &MainWindow::frameProcessed, m_luaEngine, &LuaScriptEngine::onNewFrame);
    connect(this, &MainWindow::frameProcessed, m_canSimWidget->simulator(), &CanNodeSimulator::onNewFrame);
    connect(this, &MainWindow::frameProcessed, &m_recorder, &CanRecorder::recordFrame);
    connect(this, &MainWindow::frameProcessed, &m_mdf4Writer, &Mdf4Writer::recordFrame);
    connect(this, &MainWindow::frameProcessed, &m_mqttBridge, &MqttBridge::onNewFrame);
    // Throttlowane sloty — co N-tą ramkę: dashboard, widgety diagnostyczne, pluginy
    connect(this, &MainWindow::frameProcessedThrottled, m_canDashboard,    &CanDashboard::updateSignal);
    connect(this, &MainWindow::frameProcessedThrottled, m_customDashboard,  &CanCustomDashboard::processFrame);
    connect(this, &MainWindow::frameProcessedThrottled, m_forensicsWidget,   &CanForensicsWidget::processFrame);
    connect(this, &MainWindow::frameProcessed,          m_triggerWidget,     &CanTriggerWidget::processFrame);
    connect(this, &MainWindow::frameProcessedThrottled, m_signalStatsWidget, &CanSignalStatisticsWidget::processFrame);
    connect(this, &MainWindow::frameProcessed,          m_busHealthWidget,   &CanBusHealthWidget::processFrame);
    connect(this, &MainWindow::frameProcessedThrottled, m_j1939Widget, &J1939Widget::processFrame);
    // m_udsWidget, m_obdWidget, m_canOpenWidget — lazy: connected in LazyTabWidget factory
    connect(this, &MainWindow::frameProcessedThrottled, m_signalPlotter,  &SignalPlotterWidget::processFrame);
    connect(this, &MainWindow::frameProcessed,          m_dbcAutoWidget,  &DbcAutoWidget::processFrame);
    connect(this, &MainWindow::frameProcessedThrottled, m_busLoadWidget,  &CanBusLoadWidget::processFrame);
    connect(this, &MainWindow::frameProcessed,          m_gatewayWidget,     &CanGatewayWidget::processFrame);
    connect(this, &MainWindow::frameProcessed,          m_udsSequenceWidget, &UdsSequenceWidget::processFrame);
    connect(this, &MainWindow::frameProcessed,          m_idStatsWidget,         &CanIdStatsWidget::processFrame);
    connect(this, &MainWindow::frameProcessedThrottled, m_signalMonitorWidget,   &CanSignalMonitorWidget::processFrame);
    connect(this, &MainWindow::frameProcessedThrottled, &m_pluginLoader, &PluginLoader::broadcastFrame);
    connect(this, &MainWindow::frameProcessed, m_observationDbWidget,
            [this](const CanFrame &f) { m_observationDbWidget->onFrame(f); });
    connect(this, &MainWindow::frameProcessed, m_alertWidget,
            [this](const CanFrame &f) { m_alertWidget->onFrame(f); });
    connect(this, &MainWindow::frameProcessedThrottled, m_timelineWidget,
            [this](const CanFrame &f) { m_timelineWidget->processFrame(f); });
    connect(this, &MainWindow::frameProcessedThrottled, m_heatmapWidget,
            [this](const CanFrame &f) { m_heatmapWidget->processFrame(f); });
    connect(this, &MainWindow::frameProcessed, m_moduleProfilerWidget,
            [this](const CanFrame &f) { m_moduleProfilerWidget->onFrame(f); });
    connect(this, &MainWindow::frameProcessed, m_protoExporterWidget,
            [this](const CanFrame &f) { m_protoExporterWidget->onFrame(f); });
    connect(this, &MainWindow::frameProcessedThrottled, m_icSimWidget,
            [this](const CanFrame &f) { m_icSimWidget->processFrame(f); });
    connect(m_alertWidget->engine(), &CanAlertEngine::alertTriggered,
            this, [this](const CanAlert &a) {
                if (a.frame.id != 0 || !a.description.isEmpty())
                    showTrayNotification("CAN Alert: " + a.ruleName, a.description);
            });
    connect(m_alertWidget->engine(), &CanAlertEngine::alertTriggered,
            m_luaEngine, &LuaScriptEngine::callOnAlert);
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
    connect(m_model, &CanFrameModel::frameBatchUpdated,
            m_remoteCanWidget->server(), &WebSocketServer::broadcastFrameBatch);

    // Zdalny CAN – klient: wstrzykiwanie odebranych ramek do pipeline'u
    connect(m_remoteCanWidget->client(), &RemoteCanClient::newFrame,
            this, &MainWindow::onNewFrame, Qt::QueuedConnection);
    connect(m_remoteCanWidget->client(), &RemoteCanClient::newFrame,
            m_learner, &AssociativeLearner::processFrame, Qt::QueuedConnection);
    connect(m_remoteCanWidget->client(), &RemoteCanClient::newFrame,
            m_luaEngine, &LuaScriptEngine::onNewFrame, Qt::QueuedConnection);

    // Zdalny CAN – serwer: ramki od klientów wstrzykowane do UI i na magistralę
    connect(m_remoteCanWidget->server(), &WebSocketServer::frameReceivedFromClient,
            this, [this](const CanFrame &f) {
                onNewFrame(f);                          // widok + analiza
                if (m_sniffing) m_sniffer.writeFrame(f); // fizyczna magistrala (jeśli aktywna)
            }, Qt::QueuedConnection);

    // Edytor DBC → podmiana parsera w dashboardzie i szczegółach
    connect(m_dbcEditor, &DbcEditorWidget::dbcApplied, this, [this]() {
        const DbcParser *parser = m_dbcEditor->parser();
        m_frameDetail->setDbcParser(const_cast<DbcParser*>(parser));
        m_canDashboard->setDbcParser(parser);
        m_learner->setDbcParser(parser);
        m_mqttBridge.setDbcParser(parser);
        m_signalPlotter->setDbcParser(parser);
        m_frameSender->setDbcParser(parser);
        m_signalMonitorWidget->setDbc(parser);
        m_timelineWidget->setDbcParser(parser);
        m_heatmapWidget->setDbcParser(parser);
        m_protoExporterWidget->setDbcParser(const_cast<DbcParser*>(parser));
        m_customDashboard->setDbcParser(parser);
        m_signalStatsWidget->setDbcParser(parser);
    });

    loadSettings();

    // Auto-odświeżanie listy interfejsów CAN (co 5s, tylko gdy nie sniffujemy)
    m_interfaceRefreshTimer = new QTimer(this);
    m_interfaceRefreshTimer->setInterval(5000);
    connect(m_interfaceRefreshTimer, &QTimer::timeout, this, [this]() {
        if (!m_sniffing) refreshInterfaces();
    });
    m_interfaceRefreshTimer->start();

    updateMruMenus();
}

MainWindow::~MainWindow() {
    saveSettings();
    if (m_sniffing) { m_sniffer.stop(); m_batchTimer.stop(); }
}

void MainWindow::toggleSniffing() {
    if (!m_sniffing) {
        QString iface = m_interfaceCombo->currentText().trimmed();
        if (iface.isEmpty()) { QMessageBox::warning(this, "Brak interfejsu", "Wybierz interfejs CAN."); return; }

        // Inteligentny wybór drivera po nazwie urządzenia
        ICanDriver *active = m_canDriver;
        bool isEspMcp = iface.contains("[ESP-MCP2515");
        bool isSlCan  = !isEspMcp &&
                        (iface.startsWith("COM", Qt::CaseInsensitive) ||
                         iface.startsWith("tty") || iface.startsWith("/dev/tty") ||
                         iface.contains("[SLCAN]") || iface.contains("[Canable]") ||
                         iface.contains("[candleLight]") || iface.contains("[USBtin]") ||
                         iface.contains("[CAN232]") || iface.contains("[Lawicel]"));
        if (m_espMcpDriver && isEspMcp) {
            active = m_espMcpDriver;
            Logger::log(QString("ESP-MCP2515: wybrano sterownik dla %1").arg(iface));
        } else if (m_slCanDriver && isSlCan) {
            active = m_slCanDriver;
            Logger::log(QString("SLCAN: wybrano sterownik szeregowy dla %1").arg(iface));
        }

        // Ustaw prędkość magistrali
        QString baudStr = m_baudCombo->currentText();
        active->setBaudRate(baudStr);

        m_sniffer.setDriver(active);
        m_sniffer.start(iface);
        if (!m_sniffer.isSocketValid()) {
            return;
        }
        m_sniffing = true;
        m_candumpIface = iface;
        Logger::log(QString("Rozpoczęto sniffing na interfejsie %1 (%2, %3)")
                    .arg(iface, active->backendName(), baudStr));
        m_totalFrames = 0;
        m_statusLabel->setText(QString("Nasłuchuje... (%1, %2)").arg(active->backendName(), baudStr));
        m_btnStartStop->setText("■ Stop"); m_interfaceCombo->setEnabled(false);
        m_baudCombo->setEnabled(false); m_batchTimer.start();

        if (m_autoCandumpCheck && m_autoCandumpCheck->isChecked())
            startCandumpRecording();

        // No-data timeout: warn if no frames after 5 seconds
        if (!m_noDataTimer) {
            m_noDataTimer = new QTimer(this);
            m_noDataTimer->setSingleShot(true);
            connect(m_noDataTimer, &QTimer::timeout, this, &MainWindow::checkNoData);
        }
        m_noDataTimer->start(5000);
    } else {
        m_sniffer.stop(); m_sniffing = false;
        stopCandumpRecording();
        m_btnStartStop->setText("▶ Start"); m_interfaceCombo->setEnabled(true);
        m_baudCombo->setEnabled(true); m_batchTimer.stop();
        m_statusLabel->setText("Rozłączony");
        m_statusLabel->setStyleSheet("color: #ff4444; font-weight: bold;");
        m_frameBuffer.resize(0);  // keep capacity
    }
}

void MainWindow::onNewFrame(const CanFrame &frame) {
    m_totalFrames++;
    m_frameBuffer.append(frame);
    if (m_candumpStream)
        writeFrameToCandump(frame);
    if (m_canStatsPanel)
        m_canStatsPanel->onNewFrame(frame.id, frame.timestamp);
}

void MainWindow::updateTableBatch() {
    // Drain the lock-free ring buffer (populates m_frameBuffer via onNewFrame)
    m_sniffer.drainAndEmit();

    if (m_frameBuffer.isEmpty()) return;
    if (m_canStatsPanel && m_canStatsPanel->isPaused()) return; // buforuj w tle

    // Batch processing: ogranicz liczbę ramek na tick, by uniknąć zamrożenia GUI
    // przy długiej pauzie (draining bufora stopniowo, max ~500 ramek / 33ms)
    static const int MAX_BATCH = 500;
    QVector<CanFrame> batch;
    if (m_frameBuffer.size() > MAX_BATCH) {
        batch.resize(MAX_BATCH);
        std::move(m_frameBuffer.begin(), m_frameBuffer.begin() + MAX_BATCH, batch.begin());
        m_frameBuffer.remove(0, MAX_BATCH);
    } else {
        batch.swap(m_frameBuffer);
        m_frameBuffer.reserve(4096);
    }

    // Emituj ramki do slotów analizy:
    // frameProcessed          → krytyczne sloty (każda ramka)
    // frameProcessedThrottled → co N-tą (widgety wizualne, pluginy)
    for (const CanFrame &frame : batch) {
        emit frameProcessed(frame);
        ++m_frameCounter;
        if (m_frameCounter % m_throttleInterval == 0)
            emit frameProcessedThrottled(frame);
    }

    m_model->processIncomingFrames(batch);
    if (m_autoScroll) m_tableView->scrollToBottom();
}

void MainWindow::refreshInterfaces() {
    QString current = m_interfaceCombo->currentText();
    m_interfaceCombo->clear();

#ifdef Q_OS_WIN
    QStringList ifaces = m_canDriver ? m_canDriver->availableDevices() : QStringList();
#else
    QStringList ifaces = m_canDriver ? m_canDriver->availableDevices()
                                     : CanInterfaceEnumerator::availableCanInterfaces();
#endif

    // Merge SLCAN devices (auto-detekcja portów szeregowych)
    if (m_slCanDriver) {
        QStringList slIfaces = m_slCanDriver->availableDevices();
        ifaces.append(slIfaces);
    }

    // Merge ESP32+MCP2515 devices
    if (m_espMcpDriver) {
        QStringList espIfaces = m_espMcpDriver->availableDevices();
        ifaces.append(espIfaces);
    }

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
    QString iface = m_interfaceCombo->currentText().trimmed();
    if (iface.isEmpty()) iface = "vcan0";

    auto *progress = new QProgressDialog("Eksport candump...", QString(), 0, 0, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setCancelButton(nullptr);
    progress->show();

    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this,
            [this, fileName, progress, watcher, n = frames.size()]() {
        progress->close();
        progress->deleteLater();
        watcher->deleteLater();
        QMessageBox::information(this, "Eksport", QString("Wyeksportowano %1 ramek.").arg(n));
        Logger::log(QString("Wyeksportowano %1 ramek do candump").arg(n));
    });

    watcher->setFuture(QtConcurrent::run([fileName, frames = std::move(frames), iface]() {
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
        QTextStream out(&file);
        for (const auto &frame : frames) {
            bool isFd = frame.fd || frame.xl;
            QString sep = isFd ? "##" : "#";
            QString line = QString("(%1) %2 %3%4%5")
                .arg(frame.timestamp).arg(iface)
                .arg(frame.id, frame.extended ? 8 : 3, 16, QChar('0'))
                .arg(sep)
                .arg(frame.dlc);
            int maxData = qMin((int)frame.dlc, 64);
            for (int i = 0; i < maxData; ++i)
                line += QString("%1").arg(frame.data[i], 2, 16, QChar('0')).toUpper();
            out << line << "\n";
        }
        file.close();
    }));
}

void MainWindow::exportToCsv() {
    QString fileName = QFileDialog::getSaveFileName(this, "Eksportuj do CSV", "", "CSV (*.csv)");
    if (fileName.isEmpty()) return;

    QVector<CanFrame> frames = m_model->allFrames();

    auto *progress = new QProgressDialog("Eksport CSV...", QString(), 0, 0, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setCancelButton(nullptr);
    progress->show();

    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this,
            [this, fileName, progress, watcher, n = frames.size()]() {
        progress->close();
        progress->deleteLater();
        watcher->deleteLater();
        QMessageBox::information(this, "Eksport CSV", QString("Wyeksportowano %1 ramek.").arg(n));
        Logger::log(QString("Wyeksportowano %1 ramek do CSV").arg(n));
    });

    watcher->setFuture(QtConcurrent::run([fileName, frames = std::move(frames)]() {
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
        QTextStream out(&file);
        out << "Index,Timestamp_us,ID(hex),Type,RTR,DLC,Data(hex),FD\n";
        for (int i = 0; i < frames.size(); ++i) {
            const auto &f = frames[i];
            QString data;
            int maxData = qMin((int)f.dlc, 64);
            for (int b = 0; b < maxData; ++b)
                data += QString("%1").arg(f.data[b], 2, 16, QChar('0')).toUpper();
            out << i << "," << f.timestamp << ",0x" << QString::number(f.id, 16).toUpper()
                << "," << (f.extended ? "EXT" : "STD") << "," << (f.rtr ? "RTR" : "Data")
                << "," << f.dlc << "," << data << ","
                << (f.xl ? "XL" : f.fd ? "FD" : "CAN") << "\n";
        }
        file.close();
    }));
}

void MainWindow::loadLuaScript() {
    QString fileName = QFileDialog::getOpenFileName(this, "Wczytaj skrypt Lua", "", "Skrypty Lua (*.lua);;Wszystkie pliki (*)");
    if (fileName.isEmpty()) return;
    m_luaEngine->loadScript(fileName);
    addMruFile(fileName, false);
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
        m_signalPlotter->setDbcParser(&m_dbcParser);
        m_frameSender->setDbcParser(&m_dbcParser);
        m_signalMonitorWidget->setDbc(&m_dbcParser);
        m_timelineWidget->setDbcParser(&m_dbcParser);
        m_heatmapWidget->setDbcParser(&m_dbcParser);
        m_protoExporterWidget->setDbcParser(&m_dbcParser);
        m_customDashboard->setDbcParser(&m_dbcParser);
        m_signalStatsWidget->setDbcParser(&m_dbcParser);
        addMruFile(fileName, true);
        Logger::log(QString("Załadowano plik DBC: %1").arg(fileName));
        QMessageBox::information(this, "DBC", "Plik DBC załadowany pomyślnie.");
    } else {
        QMessageBox::warning(this, "DBC", "Nie udało się wczytać pliku DBC.");
    }
}

void MainWindow::loadArxmlFile() {
    QString fileName = QFileDialog::getOpenFileName(this, "Importuj AUTOSAR ARXML", "",
                                                    "ARXML files (*.arxml);;XML files (*.xml);;All files (*)");
    if (fileName.isEmpty()) return;

    ArxmlParser arxml;
    QVector<DbcMessage> msgs = arxml.load(fileName);
    if (!arxml.lastError().isEmpty()) {
        QMessageBox::warning(this, "ARXML", "Błąd parsowania ARXML:\n" + arxml.lastError());
        return;
    }
    if (msgs.isEmpty()) {
        QMessageBox::warning(this, "ARXML", "Plik ARXML nie zawiera żadnych wiadomości CAN.");
        return;
    }

    // Merge with existing DBC messages (ARXML adds on top, DBC wins on ID conflict)
    QVector<DbcMessage> merged = m_dbcParser.messages();
    QSet<uint32_t> existingIds;
    for (auto &m : merged) existingIds.insert(m.id);
    for (auto &m : msgs) {
        if (!existingIds.contains(m.id))
            merged.append(m);
    }
    m_dbcParser.setMessages(merged);

    m_frameDetail->setDbcParser(&m_dbcParser);
    m_canDashboard->setDbcParser(&m_dbcParser);
    m_learner->setDbcParser(&m_dbcParser);
    m_model->setDbcParser(&m_dbcParser);
    m_mqttBridge.setDbcParser(&m_dbcParser);
    m_signalPlotter->setDbcParser(&m_dbcParser);
    m_frameSender->setDbcParser(&m_dbcParser);
    m_signalMonitorWidget->setDbc(&m_dbcParser);
    m_timelineWidget->setDbcParser(&m_dbcParser);
    m_heatmapWidget->setDbcParser(&m_dbcParser);
    m_protoExporterWidget->setDbcParser(&m_dbcParser);
    m_customDashboard->setDbcParser(&m_dbcParser);
    m_signalStatsWidget->setDbcParser(&m_dbcParser);

    Logger::log(QString("ARXML: zaimportowano %1 wiadomości z %2").arg(msgs.size()).arg(fileName));
    QMessageBox::information(this, "ARXML",
        QString("Zaimportowano %1 wiadomości CAN z pliku ARXML.\n"
                "Łącznie w bazie: %2 wiadomości.").arg(msgs.size()).arg(merged.size()));
}

void MainWindow::onFrameSelected(const QModelIndex &index) {
    if (!index.isValid()) return;
    QModelIndex srcIdx = m_filterProxy ? m_filterProxy->mapToSource(index) : index;
    CanFrame frame = m_model->frameAt(srcIdx.row());
    m_frameDetail->loadFrame(frame);
}

void MainWindow::setupToolBar() {
    auto *toolbar = new QToolBar("Główne", this); toolbar->setMovable(false);
    m_toolBarWidget = toolbar;
    m_interfaceCombo = new QComboBox; m_interfaceCombo->setEditable(true); m_interfaceCombo->setMinimumWidth(120);
    toolbar->addWidget(m_interfaceCombo);
    auto *refreshBtn = new QPushButton("↻"); connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshInterfaces);
    toolbar->addWidget(refreshBtn);
    m_baudCombo = new QComboBox;
    m_baudCombo->addItems({"1M", "800K", "500K", "250K", "125K", "100K", "50K", "20K", "10K"});
    m_baudCombo->setCurrentText("500K");
    m_baudCombo->setToolTip("Prędkość magistrali CAN");
    m_baudCombo->setMinimumWidth(70);
    toolbar->addWidget(m_baudCombo);
    toolbar->addSeparator();
    m_btnStartStop = new QPushButton("▶ Start"); connect(m_btnStartStop, &QPushButton::clicked, this, &MainWindow::toggleSniffing);
    toolbar->addWidget(m_btnStartStop);
    m_overwriteCheck = new QCheckBox("Nadpisywanie"); m_overwriteCheck->setChecked(true); connect(m_overwriteCheck, &QCheckBox::toggled, this, &MainWindow::applyOverwriteMode);
    toolbar->addWidget(m_overwriteCheck);

    m_autoCandumpCheck = new QCheckBox("Nagrywaj candump"); m_autoCandumpCheck->setChecked(true);
    m_autoCandumpCheck->setToolTip("Ciągłe nagrywanie ramek do C:\\candump");
    m_autoCandumpCheck->setStyleSheet("color: #00ffaa; font-weight: bold;");
    toolbar->addWidget(m_autoCandumpCheck);
    toolbar->addSeparator();
    m_statusLabel = new QLabel("Rozłączony"); m_statusLabel->setStyleSheet("color: #ff4444; font-weight: bold;");
    toolbar->addWidget(m_statusLabel);
    toolbar->addSeparator();
    QAction *clearAction = toolbar->addAction("🗙 Wyczyść"); if (clearAction) { connect(clearAction, &QAction::triggered, [this]() { m_model->clear(); }); QToolButton *clearBtn = qobject_cast<QToolButton*>(toolbar->widgetForAction(clearAction)); if (clearBtn) clearBtn->setObjectName("clearButton"); }
    QAction *exportAction = toolbar->addAction("📥 Eksportuj candump"); connect(exportAction, &QAction::triggered, this, &MainWindow::exportToCandump);
    QAction *csvAction = toolbar->addAction("📊 Eksportuj CSV"); connect(csvAction, &QAction::triggered, this, &MainWindow::exportToCsv);
    QAction *pcapAction = toolbar->addAction("🦈 Eksportuj PCAP");
    connect(pcapAction, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getSaveFileName(this, "Eksport PCAP", "", "PCAP (*.pcap);;Wszystkie (*)");
        if (path.isEmpty()) return;
        QVector<CanFrame> frames = m_model->allFrames();
        std::vector<CanFrame> vec(frames.begin(), frames.end());
        if (PcapExporter::exportFrames(path, vec))
            QMessageBox::information(this, "PCAP", QString("Wyeksportowano %1 ramek.").arg(vec.size()));
        else
            QMessageBox::warning(this, "PCAP", "Nie udało się zapisać pliku.");
    });
    QAction *recAction = toolbar->addAction("⏺ Nagraj"); connect(recAction, &QAction::triggered, this, &MainWindow::toggleRecording);
    QAction *mdf4Action = toolbar->addAction("📦 Nagraj MDF4"); connect(mdf4Action, &QAction::triggered, this, &MainWindow::toggleMdf4Recording);
    QAction *restAction = toolbar->addAction("🌐 REST API"); connect(restAction, &QAction::triggered, this, &MainWindow::toggleRestApi);
    QAction *mqttAction = toolbar->addAction("📡 MQTT"); connect(mqttAction, &QAction::triggered, this, &MainWindow::toggleMqtt);
    QAction *themeAction = toolbar->addAction("☀️ Jasny motyw"); connect(themeAction, &QAction::triggered, this, &MainWindow::toggleTheme);

    // Lua button z menu MRU
    m_luaToolBtn = new QToolButton;
    m_luaToolBtn->setText("📜 Wczytaj skrypt Lua");
    m_luaToolBtn->setPopupMode(QToolButton::MenuButtonPopup);
    m_luaToolBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    connect(m_luaToolBtn, &QToolButton::clicked, this, &MainWindow::loadLuaScript);
    toolbar->addWidget(m_luaToolBtn);

    // DBC button z menu MRU
    m_dbcToolBtn = new QToolButton;
    m_dbcToolBtn->setText("🗄️ Wczytaj DBC");
    m_dbcToolBtn->setPopupMode(QToolButton::MenuButtonPopup);
    m_dbcToolBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    connect(m_dbcToolBtn, &QToolButton::clicked, this, &MainWindow::loadDbcFile);
    toolbar->addWidget(m_dbcToolBtn);

    // ARXML import button
    auto *arxmlBtn = new QPushButton("ARXML");
    arxmlBtn->setToolTip("Importuj sygnały z pliku AUTOSAR ARXML (.arxml)");
    connect(arxmlBtn, &QPushButton::clicked, this, &MainWindow::loadArxmlFile);
    toolbar->addWidget(arxmlBtn);

    // Presety filtrów ID
    toolbar->addSeparator();
    m_filterPresetCombo = new QComboBox;
    m_filterPresetCombo->setToolTip("Presety filtrów CAN ID");
    m_filterPresetCombo->setMinimumWidth(100);
    m_filterPresetCombo->addItem("— filtry —");
    connect(m_filterPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onFilterPresetSelected);
    toolbar->addWidget(m_filterPresetCombo);

    m_savePresetBtn = new QPushButton("+");
    m_savePresetBtn->setToolTip("Zapisz bieżący filtr jako preset");
    m_savePresetBtn->setFixedWidth(28);
    connect(m_savePresetBtn, &QPushButton::clicked, this, &MainWindow::saveCurrentFilterPreset);
    toolbar->addWidget(m_savePresetBtn);

    loadFilterPresets();

    // Odtwarzanie nagrań
    toolbar->addSeparator();
    QAction *loadRecAction = toolbar->addAction("📂 Wczytaj nagranie");
    connect(loadRecAction, &QAction::triggered, this, &MainWindow::loadRecording);

    m_playPauseBtn = new QPushButton("▶ Odtwórz");
    m_playPauseBtn->setFixedWidth(90);
    connect(m_playPauseBtn, &QPushButton::clicked, this, &MainWindow::togglePlayback);
    toolbar->addWidget(m_playPauseBtn);

    m_speedCombo = new QComboBox;
    m_speedCombo->addItems({"0.5x", "1x", "2x", "5x", "10x", "Max"});
    m_speedCombo->setCurrentIndex(1); // 1x
    m_speedCombo->setToolTip("Prędkość odtwarzania");
    m_speedCombo->setFixedWidth(70);
    connect(m_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (!m_player) return;
        switch (idx) {
        case 0: m_player->setSpeed(0.5f); break;
        case 1: m_player->setSpeed(1.0f); break;
        case 2: m_player->setSpeed(2.0f); break;
        case 3: m_player->setSpeed(5.0f); break;
        case 4: m_player->setSpeed(10.0f); break;
        case 5: m_player->setSpeed(0.0f); break; // max
        }
    });
    toolbar->addWidget(m_speedCombo);
}

void MainWindow::setupCentralWidget() {
    // ── Zakładka "Ruch CAN" (canTab) ─────────────────────────────────────────
    auto *canTab = new QWidget;
    auto *canLayout = new QVBoxLayout(canTab);
    canLayout->setContentsMargins(0, 0, 0, 0);
    canLayout->setSpacing(0);
    canLayout->addWidget(m_toolBarWidget);
    canLayout->addWidget(m_canStatsPanel);

    auto *tableRow = new QHBoxLayout;
    tableRow->setContentsMargins(0, 0, 0, 0);
    tableRow->setSpacing(0);
    tableRow->addWidget(m_tableView);
    m_heatmapBar = new HeatmapBar(m_filterProxy, canTab);
    m_heatmapBar->setTableView(m_tableView);
    tableRow->addWidget(m_heatmapBar);
    canLayout->addLayout(tableRow);

    // ── Grupa: Przechwytywanie ────────────────────────────────────────────────
    auto *captureTabs = new QTabWidget;
    captureTabs->addTab(canTab,                 "Ruch CAN");
    captureTabs->addTab(m_frameDetail,          "Szczegóły ramki");
    captureTabs->addTab(m_canDashboard,         "Dashboard CAN");
    captureTabs->addTab(m_customDashboard,      "Konfigurowalny Dashboard");
    captureTabs->addTab(m_busLoadWidget,        "Obciążenie magistrali");
    captureTabs->addTab(m_idStatsWidget,        "ID Statistics");
    captureTabs->addTab(m_observationDbWidget,  "Frame DB");
    captureTabs->addTab(m_heatmapWidget,        "Byte Heatmap");
    captureTabs->addTab(m_timelineWidget,       "Timeline");

    // ── Grupa: Protokoły (lazy — parsery tworzone przy pierwszym otwarciu zakładki) ──
    auto *protocolTabs = new LazyTabWidget;
    protocolTabs->addTab(m_j1939Widget,       "J1939");          // eager: setJ1939Parser deps
    protocolTabs->addLazyTab("UDS", [this]() -> QWidget* {
        m_udsWidget = new UdsWidget;
        connect(this, &MainWindow::frameProcessedThrottled, m_udsWidget, &UdsWidget::processFrame);
        return m_udsWidget;
    });
    protocolTabs->addTab(m_udsSequenceWidget, "Sekwencje UDS");  // eager: Direct frame connect
    protocolTabs->addLazyTab("OBD-II", [this]() -> QWidget* {
        m_obdWidget = new ObdWidget;
        connect(this, &MainWindow::frameProcessedThrottled, m_obdWidget, &ObdWidget::processFrame);
        return m_obdWidget;
    });
    protocolTabs->addLazyTab("KWP2000", [this]() -> QWidget* {
        m_kwp2000Widget = new Kwp2000Widget;
        return m_kwp2000Widget;
    });
    protocolTabs->addLazyTab("XCP", [this]() -> QWidget* {
        m_xcpWidget = new XcpWidget;
        return m_xcpWidget;
    });
    protocolTabs->addLazyTab("CANopen", [this]() -> QWidget* {
        m_canOpenWidget = new CanOpenWidget;
        connect(this, &MainWindow::frameProcessedThrottled, m_canOpenWidget, &CanOpenWidget::processFrame);
        return m_canOpenWidget;
    });
    protocolTabs->addLazyTab("SOME/IP", [this]() -> QWidget* {
        m_someIpWidget = new SomeIpWidget;
        return m_someIpWidget;
    });
    protocolTabs->addLazyTab("DoIP", [this]() -> QWidget* {
        m_doIpWidget = new DoIpWidget;
        return m_doIpWidget;
    });
    protocolTabs->addLazyTab("LIN Bus", [this]() -> QWidget* {
        m_linWidget = new LinWidget;
        return m_linWidget;
    });

    // ── Grupa: Analiza ────────────────────────────────────────────────────────
    auto *analysisTabs = new LazyTabWidget;
    analysisTabs->addTab(m_learner,              "Uczenie asocjacyjne");
    analysisTabs->addTab(m_signalPlotter,        "Przebiegi sygnałów");
    analysisTabs->addTab(m_offlineAnalyzer,      "Analiza offline");
    analysisTabs->addLazyTab("Porównywarka logów", [this]() -> QWidget* {
        m_logComparator = new LogComparatorWidget;
        return m_logComparator;
    });
    analysisTabs->addTab(m_moduleProfilerWidget, "Moduły CAN");
    analysisTabs->addTab(m_alertWidget,          "Alerts");
    analysisTabs->addTab(m_signalMonitorWidget,  "Live Signals");
    analysisTabs->addTab(m_forensicsWidget,      "Forensics");
    analysisTabs->addTab(m_signalStatsWidget,   "Statystyki sygnałów");
    analysisTabs->addTab(m_busHealthWidget,     "Zdrowie magistrali");

    // ── Grupa: Narzędzia ──────────────────────────────────────────────────────
    auto *toolsTabs = new LazyTabWidget;
    toolsTabs->addTab(m_frameSender,          "Nadajnik ramek");
    toolsTabs->addTab(m_periodicSenderWidget, "Periodic Sender");
    toolsTabs->addTab(m_gatewayWidget,        "CAN Gateway");
    toolsTabs->addLazyTab("Replay Filter", [this]() -> QWidget* {
        m_replayFilterWidget = new CanReplayFilterWidget(&m_sniffer);
        return m_replayFilterWidget;
    });
    toolsTabs->addTab(m_protoExporterWidget, "Eksport kodu");
    m_canExporter = new CanExporter(m_model);
    toolsTabs->addTab(m_canExporter,         "Eksport danych");
    toolsTabs->addTab(m_icSimWidget,         "ICSim");
    toolsTabs->addTab(m_triggerWidget,       "Wyzwalacz");
    toolsTabs->addTab(m_canSimWidget,        "Symulacja CAN");
    toolsTabs->addTab(m_remoteCanWidget,     "Zdalny CAN");
    toolsTabs->addTab(m_dbcEditor,           "Edytor DBC");
    toolsTabs->addTab(m_dbcAutoWidget,       "Auto-DBC");
    for (auto *plugin : m_pluginLoader.plugins())
        if (auto *w = plugin->widget())
            toolsTabs->addTab(w, plugin->name());

    // ── Główny QTabWidget (4 grupy) ───────────────────────────────────────────
    auto *tabs = new QTabWidget;
    tabs->addTab(captureTabs,  "Przechwytywanie");
    tabs->addTab(protocolTabs, "Protokoły");
    tabs->addTab(analysisTabs, "Analiza");
    tabs->addTab(toolsTabs,    "Narzędzia");

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

void MainWindow::checkNoData() {
    if (!m_sniffing) return;
    if (m_totalFrames == 0) {
        m_statusLabel->setText(m_statusLabel->text() + " ⚠ brak ramek — sprawdź magistralę/baud");
        m_statusLabel->setStyleSheet("color: #ffaa00; font-weight: bold;");
        Logger::log("⚠ Brak ramek CAN po 5s — sprawdź czy magistrala jest aktywna i baud rate poprawny");
    }
}

void MainWindow::applyIdFilter(const QString &text) {
    if (!m_filterProxy) return;
    // If text looks like an expression (contains 'can.' or logical ops), use expr filter
    QString t = text.trimmed();
    if (t.contains("can.") || t.contains("&&") || t.contains("||") || t.startsWith("!")) {
        QString err = m_filterProxy->setExpression(t);
        if (!err.isEmpty())
            m_statusLabel->setText("Filtr: " + err);
    } else {
        m_filterProxy->setExpression(QString()); // clear expression
        m_filterProxy->setIdFilter(text);
    }
}

void MainWindow::loadFilterPresets() {
    if (!m_filterPresetCombo) return;

    const QString presetsPath = QDir::homePath() + "/.magistrala_can4/filter_presets.txt";
    QFile file(presetsPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        int sep = line.indexOf('|');
        if (sep > 0) {
            QString hexId = line.left(sep).trimmed();
            QString name  = line.mid(sep + 1).trimmed();
            m_filterPresetCombo->addItem(
                QString("0x%1 — %2").arg(hexId.toUpper(), name),
                hexId); // store raw hex as user data
        }
    }
    file.close();
}

void MainWindow::saveCurrentFilterPreset() {
    if (!m_filterProxy || !m_filterProxy->isFilterActive()) return;

    // Pobierz bieżący filtr z proxy
    QString currentId = m_filterProxy->currentFilterId();  // hex, uppercase
    if (currentId.isEmpty()) return;

    bool ok = false;
    QString name = QInputDialog::getText(this, "Zapisz preset filtru",
        QString("Nazwa dla filtru 0x%1:").arg(currentId),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    name = name.trimmed();

    // Zapisz do pliku
    const QString presetsPath = QDir::homePath() + "/.magistrala_can4/filter_presets.txt";
    QDir().mkpath(QDir::homePath() + "/.magistrala_can4");
    QFile file(presetsPath);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << currentId << "|" << name << "\n";
        file.close();
    }

    // Dodaj do combo
    m_filterPresetCombo->addItem(
        QString("0x%1 — %2").arg(currentId, name),
        currentId);
}

void MainWindow::onFilterPresetSelected(int index) {
    if (!m_filterPresetCombo || index <= 0) return; // index 0 = placeholder
    QString hexId = m_filterPresetCombo->itemData(index).toString();
    if (!hexId.isEmpty())
        applyIdFilter(hexId);
}

void MainWindow::onTableContextMenu(const QPoint &pos) {
    QModelIndex proxyIndex = m_tableView->indexAt(pos);
    QModelIndex srcIndex = proxyIndex.isValid() && m_filterProxy
        ? m_filterProxy->mapToSource(proxyIndex) : QModelIndex();
    CanFrame frame = srcIndex.isValid() ? m_model->frameAt(srcIndex.row()) : CanFrame();
    bool hasFrame = srcIndex.isValid() && (frame.dlc > 0 || frame.id != 0);

    QMenu menu(m_tableView);

    if (hasFrame) {
        QString hexId = QString::number(frame.id, 16).toUpper();

        QAction *copyId = menu.addAction(QString("Kopiuj CAN ID  (0x%1)").arg(hexId));
        connect(copyId, &QAction::triggered, this, [hexId]() {
            QApplication::clipboard()->setText(hexId);
        });

        QAction *copyData = menu.addAction("Kopiuj dane (hex)");
        connect(copyData, &QAction::triggered, this, [frame]() {
            QString data;
            for (int i = 0; i < frame.dlc && i < 64; ++i)
                data += QString("%1 ").arg(frame.data[i], 2, 16, QChar('0')).toUpper();
            QApplication::clipboard()->setText(data.trimmed());
        });

        menu.addSeparator();

        QAction *filterAction = menu.addAction(QString("Filtruj po ID 0x%1").arg(hexId));
        connect(filterAction, &QAction::triggered, this, [this, hexId]() {
            applyIdFilter(hexId);
        });
    }

    if (m_filterProxy && m_filterProxy->isFilterActive()) {
        QAction *clearAction = menu.addAction("Wyczyść filtr");
        connect(clearAction, &QAction::triggered, this, [this]() {
            applyIdFilter(QString());
        });
    }

    menu.addSeparator();

    QAction *copyTsAction = menu.addAction("Kopiuj zaznaczone (TSV)");
    connect(copyTsAction, &QAction::triggered, this, &MainWindow::copySelectedToClipboard);

    menu.exec(m_tableView->viewport()->mapToGlobal(pos));
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

    for (const auto &proxyIdx : sel) {
        QModelIndex srcIdx = m_filterProxy ? m_filterProxy->mapToSource(proxyIdx) : proxyIdx;
        QStringList cols;
        for (int c = 0; c < m_model->columnCount(); ++c)
            cols << m_model->data(m_model->index(srcIdx.row(), c)).toString();
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

void MainWindow::loadRecording() {
    QString path = QFileDialog::getOpenFileName(this, "Wczytaj nagranie CAN", "",
        "Nagrania (*.mcan *.mcan.zst);;Wszystkie pliki (*)");
    if (path.isEmpty()) return;

    int count = m_player->loadFile(path);
    if (count > 0) {
        if (m_sniffing) { toggleSniffing(); } // zatrzymaj live sniffing
        if (m_playPauseBtn) m_playPauseBtn->setText("▶ Odtwórz");
        Logger::log(QString("Wczytano nagranie: %1 (%2 ramek)").arg(path).arg(count));
    } else {
        QMessageBox::warning(this, "Błąd", "Nie udało się wczytać nagrania.");
    }
}

// ── Ciągłe nagrywanie candump ──────────────────────────────

void MainWindow::startCandumpRecording() {
    QString dirPath = "C:/candump";
    QDir dir(dirPath);
    if (!dir.exists())
        dir.mkpath(".");

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString filePath = dirPath + "/candump_" + timestamp + ".log";

    m_candumpFile = new QFile(filePath);
    if (!m_candumpFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
        delete m_candumpFile;
        m_candumpFile = nullptr;
        Logger::log("Blad: nie mozna utworzyc pliku candump: " + filePath);
        return;
    }
    m_candumpStream = new QTextStream(m_candumpFile);
    Logger::log("Nagrywanie candump: " + filePath);
}

void MainWindow::stopCandumpRecording() {
    if (m_candumpStream) {
        m_candumpStream->flush();
        delete m_candumpStream;
        m_candumpStream = nullptr;
    }
    if (m_candumpFile) {
        m_candumpFile->close();
        Logger::log("Zakonczono nagrywanie candump: " + m_candumpFile->fileName());
        delete m_candumpFile;
        m_candumpFile = nullptr;
    }
}

void MainWindow::writeFrameToCandump(const CanFrame &frame) {
    if (!m_candumpStream) return;
    bool isFd = frame.fd || frame.xl;
    QString sep = isFd ? "##" : "#";
    (*m_candumpStream) << "(" << frame.timestamp << ") "
                       << m_candumpIface << " "
                       << QString::number(frame.id, 16).toUpper()
                       << sep
                       << QString::number(frame.dlc);
    int maxData = qMin((int)frame.dlc, 64);
    for (int i = 0; i < maxData; ++i)
        (*m_candumpStream) << QString("%1").arg(frame.data[i], 2, 16, QChar('0')).toUpper();
    (*m_candumpStream) << "\n";
}

void MainWindow::togglePlayback() {
    if (!m_player) return;

    if (m_player->isPlaying()) {
        m_player->pause();
        if (m_playPauseBtn) m_playPauseBtn->setText("▶ Odtwórz");
    } else {
        m_player->play();
        if (m_playPauseBtn) m_playPauseBtn->setText("⏸ Pauza");
    }
}

void MainWindow::toggleTheme() {
    m_darkTheme = !m_darkTheme;
    const QString qssPath = m_darkTheme
        ? QStringLiteral(":/resources/style_dark.qss")
        : QStringLiteral(":/resources/style_light.qss");
    QFile qssFile(qssPath);
    if (qssFile.open(QFile::ReadOnly | QFile::Text)) {
        qApp->setStyleSheet(qssFile.readAll());
        qssFile.close();
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::saveSettings() {
    QSettings s("MagistralaCAN4", "MagistralaCAN4");
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/state", saveState());
    s.setValue("interface/last", m_interfaceCombo ? m_interfaceCombo->currentText() : QString());
    s.setValue("interface/baud", m_baudCombo ? m_baudCombo->currentText() : QString());
    s.setValue("options/overwrite", m_overwriteCheck ? m_overwriteCheck->isChecked() : true);
    s.setValue("options/autoscroll", m_autoScroll);
    s.setValue("options/darkTheme", m_darkTheme);
    s.setValue("options/throttleInterval", m_throttleInterval);
    s.setValue("mru/dbc", m_mruDbcFiles);
    s.setValue("mru/lua", m_mruLuaFiles);
}

void MainWindow::loadSettings() {
    QSettings s("MagistralaCAN4", "MagistralaCAN4");

    // Geometria i stan okna
    if (s.contains("window/geometry"))
        restoreGeometry(s.value("window/geometry").toByteArray());
    if (s.contains("window/state"))
        restoreState(s.value("window/state").toByteArray());

    // Ostatni interfejs
    QString lastIface = s.value("interface/last").toString();
    if (!lastIface.isEmpty() && m_interfaceCombo) {
        int idx = m_interfaceCombo->findText(lastIface);
        if (idx >= 0) m_interfaceCombo->setCurrentIndex(idx);
        else m_interfaceCombo->setCurrentText(lastIface);
    }

    // Baud rate
    QString lastBaud = s.value("interface/baud").toString();
    if (!lastBaud.isEmpty() && m_baudCombo) {
        int idx = m_baudCombo->findText(lastBaud);
        if (idx >= 0) m_baudCombo->setCurrentIndex(idx);
    }

    // Opcje
    if (m_overwriteCheck)
        m_overwriteCheck->setChecked(s.value("options/overwrite", true).toBool());
    m_autoScroll = s.value("options/autoscroll", true).toBool();
    m_darkTheme = s.value("options/darkTheme", true).toBool();
    m_throttleInterval = s.value("options/throttleInterval", 16).toInt();
    m_mruDbcFiles = s.value("mru/dbc").toStringList();
    m_mruLuaFiles = s.value("mru/lua").toStringList();

    // Zastosuj motyw
    const QString qssPath = m_darkTheme
        ? QStringLiteral(":/resources/style_dark.qss")
        : QStringLiteral(":/resources/style_light.qss");
    QFile qssFile(qssPath);
    if (qssFile.open(QFile::ReadOnly | QFile::Text)) {
        qApp->setStyleSheet(qssFile.readAll());
        qssFile.close();
    }
}

void MainWindow::addMruFile(const QString &path, bool isDbc) {
    QStringList &list = isDbc ? m_mruDbcFiles : m_mruLuaFiles;
    list.removeAll(path);          // usuń duplikat
    list.prepend(path);            // wstaw na początek
    if (list.size() > 5) list.resize(5); // max 5
    updateMruMenus();
}

void MainWindow::updateMruMenus() {
    auto buildMenu = [this](QToolButton *btn, const QStringList &list, bool isDbc) {
        if (!btn || list.isEmpty()) {
            if (btn) btn->setMenu(nullptr);
            return;
        }
        auto *menu = new QMenu(btn);
        for (const QString &path : list) {
            QAction *act = menu->addAction(QFileInfo(path).fileName());
            connect(act, &QAction::triggered, this, [this, path, isDbc]() {
                if (isDbc) {
                    if (m_dbcParser.load(path)) {
                        m_frameDetail->setDbcParser(&m_dbcParser);
                        m_canDashboard->setDbcParser(&m_dbcParser);
                        m_learner->setDbcParser(&m_dbcParser);
                        m_model->setDbcParser(&m_dbcParser);
                        m_mqttBridge.setDbcParser(&m_dbcParser);
                        m_signalPlotter->setDbcParser(&m_dbcParser);
                        m_frameSender->setDbcParser(&m_dbcParser);
                        m_signalMonitorWidget->setDbc(&m_dbcParser);
                        m_timelineWidget->setDbcParser(&m_dbcParser);
                        m_heatmapWidget->setDbcParser(&m_dbcParser);
                        m_protoExporterWidget->setDbcParser(&m_dbcParser);
                        m_customDashboard->setDbcParser(&m_dbcParser);
                        m_signalStatsWidget->setDbcParser(&m_dbcParser);
                        addMruFile(path, true);
                    }
                } else {
                    m_luaEngine->loadScript(path);
                    addMruFile(path, false);
                }
            });
        }
        menu->addSeparator();
        menu->addAction("Wyczyść listę")->connect(menu->addAction("Wyczyść listę"), &QAction::triggered, this, [this, isDbc]() {
            if (isDbc) m_mruDbcFiles.clear(); else m_mruLuaFiles.clear();
            updateMruMenus();
        });
        btn->setMenu(menu);
    };

    buildMenu(m_dbcToolBtn, m_mruDbcFiles, true);
    buildMenu(m_luaToolBtn, m_mruLuaFiles, false);
}

void MainWindow::setupStyle() {
    // Global stylesheet is loaded in main.cpp from resources/style_dark.qss
    // This method is kept for explicit re-application when needed
    QFile qssFile(":/resources/style_dark.qss");
    if (qssFile.open(QFile::ReadOnly | QFile::Text)) {
        qApp->setStyleSheet(qssFile.readAll());
        qssFile.close();
    }
}