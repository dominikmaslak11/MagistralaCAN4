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
#include "core/Logger.h"
#include <QSystemTrayIcon>
#include <QShortcut>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void showTrayNotification(const QString &title, const QString &message);
    void trayActivated(QSystemTrayIcon::ActivationReason reason);
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

    QTimer m_batchTimer;
    QVector<CanFrame> m_frameBuffer;
    bool m_sniffing = false;
    bool m_autoScroll = true;
};
