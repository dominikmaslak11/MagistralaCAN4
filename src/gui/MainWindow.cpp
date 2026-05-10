#include "MainWindow.h"
#include <QOpenGLWidget>
#ifdef Q_OS_WIN
#include "core/PcanDriver.h"
#else
#include "core/SocketCanDriver.h"
#include "core/CanInterfaceEnumerator.h"
#endif
#include "core/SlCanDriver.h"

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
#include <QTextStream>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QClipboard>

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

    // --- CAN Driver ---
#ifdef Q_OS_WIN
    m_canDriver = new PcanDriver();
#else
    m_canDriver = new SocketCanDriver();
#endif
    // SLCAN – zawsze dostępny (porty szeregowe), cross-platform
    m_slCanDriver = new SlCanDriver();
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
    setupCentralWidget();
    setupStyle();

    // Ctrl+C – kopiuj zaznaczone ramki
    auto *copyShortcut = new QShortcut(QKeySequence("Ctrl+C"), m_tableView);
    connect(copyShortcut, &QShortcut::activated, this, &MainWindow::copySelectedToClipboard);

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
    connect(this, &MainWindow::frameProcessedThrottled, m_canDashboard, &CanDashboard::updateSignal);
    connect(this, &MainWindow::frameProcessedThrottled, m_j1939Widget, &J1939Widget::processFrame);
    connect(this, &MainWindow::frameProcessedThrottled, m_udsWidget, &UdsWidget::processFrame);
    connect(this, &MainWindow::frameProcessedThrottled, m_obdWidget, &ObdWidget::processFrame);
    connect(this, &MainWindow::frameProcessedThrottled, m_canOpenWidget, &CanOpenWidget::processFrame);
    connect(this, &MainWindow::frameProcessedThrottled, &m_pluginLoader, &PluginLoader::broadcastFrame);
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

        // Inteligentny wybór drivera po nazwie urządzenia
        ICanDriver *active = m_canDriver;
        bool isSlCan = (iface.startsWith("COM", Qt::CaseInsensitive) ||
                         iface.startsWith("tty") || iface.startsWith("/dev/tty") ||
                         iface.contains("[SLCAN]") || iface.contains("[Canable]") ||
                         iface.contains("[candleLight]") || iface.contains("[USBtin]") ||
                         iface.contains("[CAN232]") || iface.contains("[Lawicel]"));
        if (m_slCanDriver && isSlCan) {
            active = m_slCanDriver;
            Logger::log(QString("SLCAN: wybrano sterownik szeregowy dla %1").arg(iface));
        }

        // Ustaw prędkość magistrali
        QString baudStr = m_baudCombo->currentText();
        active->setBaudRate(baudStr);

        m_sniffer.setDriver(active);
        m_sniffer.start(iface); m_sniffing = true;
        Logger::log(QString("Rozpoczęto sniffing na interfejsie %1").arg(iface));
        m_btnStartStop->setText("■ Stop"); m_interfaceCombo->setEnabled(false);
        m_baudCombo->setEnabled(false); m_batchTimer.start();
    } else {
        m_sniffer.stop(); m_sniffing = false;
        m_btnStartStop->setText("▶ Start"); m_interfaceCombo->setEnabled(true);
        m_baudCombo->setEnabled(true); m_batchTimer.stop();
        m_frameBuffer.resize(0);  // keep capacity
    }
}

void MainWindow::onNewFrame(const CanFrame &frame) {
    m_frameBuffer.append(frame);
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
        batch = m_frameBuffer.mid(0, MAX_BATCH);
        m_frameBuffer.erase(m_frameBuffer.begin(), m_frameBuffer.begin() + MAX_BATCH);
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
        bool isFd = frame.fd || frame.xl;
        // candump używa "#" dla klasycznego CAN, "##" dla CAN FD/XL
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
        int maxData = qMin((int)f.dlc, 64);
        for (int b = 0; b < maxData; ++b)
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
    canLayout->setSpacing(0);

    canLayout->addWidget(m_toolBarWidget);
    canLayout->addWidget(m_canStatsPanel);
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

void MainWindow::applyIdFilter(const QString &text) {
    if (m_filterProxy)
        m_filterProxy->setIdFilter(text);
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
    const QString qssPath = s_darkTheme
        ? QStringLiteral(":/resources/style_dark.qss")
        : QStringLiteral(":/resources/style_light.qss");
    QFile qssFile(qssPath);
    if (qssFile.open(QFile::ReadOnly | QFile::Text)) {
        qApp->setStyleSheet(qssFile.readAll());
        qssFile.close();
    }
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