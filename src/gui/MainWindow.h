#pragma once
#include <QMainWindow>
#include <QTableView>
#include <QPushButton>
#include <QToolButton>
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
#include "core/DbcParser.h"
#include "core/OfflineAnalyzer.h"
#include "core/CanDashboard.h"
#include "core/J1939Widget.h"
#include "core/DbcEditorWidget.h"
#include "core/CanNodeSimWidget.h"
#include "core/RemoteCanWidget.h"
#include "core/UdsWidget.h"
#include "core/LogComparatorWidget.h"
#include "core/ObdWidget.h"
#include "core/CanOpenWidget.h"
#include "core/CanRecorder.h"
#include "core/MqttBridge.h"
#include "core/Mdf4Writer.h"
#include "core/PluginLoader.h"
#include "core/HttpRestServer.h"
#include "core/McpServer.h"
#include "core/CanStatsPanel.h"
#include "core/CanFilterProxy.h"
#include "core/CanPlayer.h"
#include "core/SomeIpWidget.h"
#include "core/SignalPlotterWidget.h"
#include "core/DoIpWidget.h"
#include "core/DbcAutoWidget.h"
#include "core/CanBusLoadWidget.h"
#include "core/CanFrameSenderWidget.h"
#include "core/CanGatewayWidget.h"
#include "core/UdsSequenceWidget.h"
#include "core/LinWidget.h"
#include "core/CanIdStatsWidget.h"
#include "core/Kwp2000Widget.h"
#include "core/XcpWidget.h"
#include "core/CanReplayFilterWidget.h"
#include "core/CanSignalMonitorWidget.h"
#include "core/CanPeriodicSenderWidget.h"
#include "core/CanObservationDbWidget.h"
#include "core/CanAlertWidget.h"
#include "core/CanProtocolTimelineWidget.h"
#include "core/CanByteHeatmapWidget.h"
#include "core/CanSignalTrendWidget.h"
#include "core/ArxmlParser.h"
#include "core/CanModuleProfilerWidget.h"
#include "core/CanPrototypeExporterWidget.h"
#include "core/CanExporter.h"
#include "core/IcSimWidget.h"
#include "core/CanCustomDashboard.h"
#include "core/CanForensicsWidget.h"
#include "core/CanTriggerWidget.h"
#include "core/CanSignalStatisticsWidget.h"
#include "core/CanBusHealthWidget.h"
#include "core/CanReplayEditorWidget.h"
#include "core/CanScriptWidget.h"
#include "core/CanStatsDashboard.h"
#include "core/CanSignalMapperWidget.h"
#include "core/ICanDriver.h"
#include "core/Logger.h"
#include "gui/HeatmapBar.h"
#include "gui/LazyTabWidget.h"
#include <memory>
#include <QSystemTrayIcon>
#include <QShortcut>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void showTrayNotification(const QString &title, const QString &message);
    void trayActivated(QSystemTrayIcon::ActivationReason reason);
    void connectRemoteCan(const QString &url, const QString &token);

protected:
    void closeEvent(QCloseEvent *event) override;

signals:
    /// Ramka po przetworzeniu przez model (DirectConnection do krytycznych slotów: nagrywanie, uczenie, forwarding)
    void frameProcessed(const CanFrame &frame);

    /// Ramka throttlowana (co N-tą) dla ciężkich slotów wizualnych: dashboard, widgety, pluginy
    void frameProcessedThrottled(const CanFrame &frame);

private slots:
    void toggleSniffing();
    void onNewFrame(const CanFrame &frame);
    void updateTableBatch();
    void refreshInterfaces();
    void applyOverwriteMode(bool enabled);
    void onUserScroll(int value);
    void exportToCandump();
    void exportToCsv();
    void loadLuaScript();
    void loadDbcFile();
    void onFrameSelected(const QModelIndex &index);
    void applyIdFilter(const QString &text);
    void copySelectedToClipboard();
    void toggleRecording();
    void toggleMdf4Recording();
    void toggleRestApi();
    void toggleMcpServer();
    void toggleMqtt();
    void toggleTheme();
    void onTableContextMenu(const QPoint &pos);
    void onFilterPresetSelected(int index);
    void saveCurrentFilterPreset();
    void addMruFile(const QString &path, bool isDbc);
    void updateMruMenus();
    void loadRecording();
    void togglePlayback();
    void loadArxmlFile();

private:
    void setupStyle();
    void setupToolBar();
    void setupCentralWidget();
    void loadFilterPresets();
    void saveSettings();
    void loadSettings();

    CanSniffer m_sniffer;
    ICanDriver *m_canDriver    = nullptr;
    ICanDriver *m_slCanDriver  = nullptr;  // SLCAN backend (serial port)
    ICanDriver *m_espMcpDriver = nullptr;  // ESP32+MCP2515 backend
    ICanDriver *m_gvretDriver  = nullptr;  // GVRET binary backend (SavvyCAN-compatible)
    QComboBox  *m_driverCombo      = nullptr;  // explicit protocol selector
    QComboBox  *m_serialBaudCombo  = nullptr;  // UART baud rate for serial drivers
    CanFrameModel *m_model;
    AssociativeLearner *m_learner;
    LuaScriptEngine *m_luaEngine;
    FrameDetailWidget *m_frameDetail;
    DbcParser m_dbcParser;
    OfflineAnalyzer *m_offlineAnalyzer;
    CanDashboard *m_canDashboard;
    J1939Widget *m_j1939Widget;
    DbcEditorWidget *m_dbcEditor;
    CanNodeSimWidget *m_canSimWidget;
    RemoteCanWidget  *m_remoteCanWidget;
    UdsWidget        *m_udsWidget        = nullptr;
    LogComparatorWidget *m_logComparator = nullptr;
    ObdWidget           *m_obdWidget     = nullptr;
    CanOpenWidget       *m_canOpenWidget = nullptr;
    SomeIpWidget        *m_someIpWidget  = nullptr;
    SignalPlotterWidget *m_signalPlotter;
    DoIpWidget          *m_doIpWidget    = nullptr;
    DbcAutoWidget       *m_dbcAutoWidget;
    CanBusLoadWidget      *m_busLoadWidget;
    CanFrameSenderWidget  *m_frameSender;
    CanGatewayWidget      *m_gatewayWidget;
    UdsSequenceWidget     *m_udsSequenceWidget;
    LinWidget             *m_linWidget          = nullptr;
    CanIdStatsWidget      *m_idStatsWidget;
    Kwp2000Widget         *m_kwp2000Widget      = nullptr;
    XcpWidget             *m_xcpWidget          = nullptr;
    CanReplayFilterWidget  *m_replayFilterWidget = nullptr;
    CanSignalMonitorWidget  *m_signalMonitorWidget;
    CanPeriodicSenderWidget *m_periodicSenderWidget;
    CanObservationDbWidget  *m_observationDbWidget;
    CanAlertWidget               *m_alertWidget;
    CanProtocolTimelineWidget    *m_timelineWidget;
    CanByteHeatmapWidget         *m_heatmapWidget;
    CanSignalTrendWidget         *m_signalTrendWidget;
    CanModuleProfilerWidget      *m_moduleProfilerWidget  = nullptr;
    CanPrototypeExporterWidget   *m_protoExporterWidget   = nullptr;
    CanExporter                  *m_canExporter           = nullptr;
    IcSimWidget                  *m_icSimWidget           = nullptr;
    CanCustomDashboard           *m_customDashboard       = nullptr;
    CanForensicsWidget           *m_forensicsWidget       = nullptr;
    CanTriggerWidget             *m_triggerWidget         = nullptr;
    CanSignalStatisticsWidget    *m_signalStatsWidget     = nullptr;
    CanBusHealthWidget           *m_busHealthWidget       = nullptr;
    CanReplayEditorWidget        *m_replayEditorWidget    = nullptr;
    CanScriptWidget              *m_scriptWidget          = nullptr;
    CanStatsDashboard            *m_statsDashboard        = nullptr;
    CanSignalMapperWidget        *m_signalMapperWidget    = nullptr;
    CanRecorder          m_recorder;
    Mdf4Writer           m_mdf4Writer;
    HttpRestServer       m_restServer;
    McpServer            m_mcpServer;
    MqttBridge           m_mqttBridge;
    PluginLoader         m_pluginLoader;
    QTableView *m_tableView;
    QPushButton *m_btnStartStop;
    QComboBox *m_interfaceCombo;
    QComboBox *m_baudCombo;
    QCheckBox *m_overwriteCheck;
    QLabel *m_statusLabel;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QShortcut *m_hotkeyMarkEvent = nullptr;
    QShortcut *m_hotkeyNonEvent = nullptr;

    // Panel statystyk CAN (Faza 2.2 — wydzielony widget)
    CanStatsPanel *m_canStatsPanel;

    // Proxy filtrująco-sortujący między CanFrameModel a QTableView
    CanFilterProxy *m_filterProxy = nullptr;

    // Pasek szybkiego skoku (heatmap scrollbar)
    HeatmapBar *m_heatmapBar = nullptr;

    // Presety filtrów ID
    QComboBox *m_filterPresetCombo = nullptr;
    QPushButton *m_savePresetBtn = nullptr;

    // MRU buttons
    QToolButton *m_dbcToolBtn = nullptr;
    QToolButton *m_luaToolBtn = nullptr;

    // Toolbar przeniesiony do zakładki "Ruch CAN"
    QWidget *m_toolBarWidget = nullptr;

    QTimer m_batchTimer;
    QVector<CanFrame> m_frameBuffer;
    bool m_sniffing = false;
    bool m_autoScroll = true;

    // Throttling slotów analizy (co N-tą ramkę do ciężkich widgetów)
    uint64_t m_frameCounter = 0;
    int m_throttleInterval = 16;  // ~30 fps wizualnych przy 500 fps wejściu

    // Stan do persystencji
    bool m_darkTheme = true;

    // MRU: ostatnio otwierane pliki
    QStringList m_mruDbcFiles;
    QStringList m_mruLuaFiles;

    // Auto-odświeżanie interfejsów
    std::unique_ptr<QTimer> m_interfaceRefreshTimer;

    // Diagnostyka CAN
    std::unique_ptr<QTimer> m_noDataTimer;
    int m_totalFrames = 0;
    void checkNoData();

    // Ciągłe nagrywanie candump na dysk
    QCheckBox *m_autoCandumpCheck = nullptr;
    QFile *m_candumpFile = nullptr;
    QTextStream *m_candumpStream = nullptr;
    QString m_candumpIface;
    void startCandumpRecording();
    void stopCandumpRecording();
    void writeFrameToCandump(const CanFrame &frame);

    // Odtwarzanie nagrań
    CanPlayer *m_player = nullptr;
    QPushButton *m_playPauseBtn = nullptr;
    QComboBox *m_speedCombo = nullptr;
};
