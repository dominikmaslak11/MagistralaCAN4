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
#include "core/CanStatsPanel.h"
#include "core/CanFilterProxy.h"
#include "core/CanPlayer.h"
#include "core/SomeIpWidget.h"
#include "core/SignalPlotterWidget.h"
#include "core/DoIpWidget.h"
#include "core/DbcAutoWidget.h"
#include "core/CanBusLoadWidget.h"
#include "core/ICanDriver.h"
#include "core/Logger.h"
#include "gui/HeatmapBar.h"
#include <QSystemTrayIcon>
#include <QShortcut>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void showTrayNotification(const QString &title, const QString &message);
    void trayActivated(QSystemTrayIcon::ActivationReason reason);

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
    void toggleMqtt();
    void toggleTheme();
    void onTableContextMenu(const QPoint &pos);
    void onFilterPresetSelected(int index);
    void saveCurrentFilterPreset();
    void addMruFile(const QString &path, bool isDbc);
    void updateMruMenus();
    void loadRecording();
    void togglePlayback();

private:
    void setupStyle();
    void setupToolBar();
    void setupCentralWidget();
    void loadFilterPresets();
    void saveSettings();
    void loadSettings();

    CanSniffer m_sniffer;
    ICanDriver *m_canDriver = nullptr;
    ICanDriver *m_slCanDriver = nullptr;  // SLCAN backend (serial port)
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
    UdsWidget        *m_udsWidget;
    LogComparatorWidget *m_logComparator;
    ObdWidget           *m_obdWidget;
    CanOpenWidget       *m_canOpenWidget;
    SomeIpWidget        *m_someIpWidget;
    SignalPlotterWidget *m_signalPlotter;
    DoIpWidget          *m_doIpWidget;
    DbcAutoWidget       *m_dbcAutoWidget;
    CanBusLoadWidget    *m_busLoadWidget;
    CanRecorder          m_recorder;
    Mdf4Writer           m_mdf4Writer;
    HttpRestServer       m_restServer;
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
    QTimer *m_interfaceRefreshTimer = nullptr;

    // Diagnostyka CAN
    QTimer *m_noDataTimer = nullptr;
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
