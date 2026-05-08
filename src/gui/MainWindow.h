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
#include "core/Logger.h"
#include <QSystemTrayIcon>
#include <QShortcut>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QChart>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void showTrayNotification(const QString &title, const QString &message);
    void trayActivated(QSystemTrayIcon::ActivationReason reason);

signals:
    /// Ramka po przetworzeniu przez model (DirectConnection do slotów analizy)
    void frameProcessed(const CanFrame &frame);

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
    void updateCanStats();
    void applyIdFilter(const QString &text);
    void toggleCanPause();
    void copySelectedToClipboard();
    void toggleRecording();
    void toggleMdf4Recording();
    void toggleRestApi();
    void toggleMqtt();
    void toggleTheme();

private:
    void setupStyle();
    void setupToolBar();
    void setupCentralWidget();

    CanSniffer m_sniffer;
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
    CanRecorder          m_recorder;
    Mdf4Writer           m_mdf4Writer;
    HttpRestServer       m_restServer;
    MqttBridge           m_mqttBridge;
    PluginLoader         m_pluginLoader;
    QTableView *m_tableView;
    QPushButton *m_btnStartStop;
    QComboBox *m_interfaceCombo;
    QCheckBox *m_overwriteCheck;
    QLabel *m_statusLabel;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QShortcut *m_hotkeyMarkEvent = nullptr;
    QShortcut *m_hotkeyNonEvent = nullptr;

    // Statystyki CAN
    QLineEdit    *m_canFilterEdit;
    QLabel       *m_canStatsLabel;
    QPushButton  *m_canPauseBtn;
    QTimer        m_canStatsTimer;
    uint64_t      m_totalFrameCount = 0;
    uint64_t      m_lastStatsFrameCount = 0;
    QSet<uint32_t> m_uniqueIdsSinceLastStats;
    bool           m_canPaused = false;
    QChartView    *m_fpsChartView;
    QChart        *m_fpsChart;
    QLineSeries   *m_fpsSeries;
    int            m_fpsHistoryCount = 0;

    QTimer m_batchTimer;
    QVector<CanFrame> m_frameBuffer;
    bool m_sniffing = false;
    bool m_autoScroll = true;
};
