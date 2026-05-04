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
#include <QSystemTrayIcon>

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
    void loadLuaScript();
    void loadDbcFile();
    void onFrameSelected(const QModelIndex &index);

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
    QTableView *m_tableView;
    QPushButton *m_btnStartStop;
    QComboBox *m_interfaceCombo;
    QCheckBox *m_overwriteCheck;
    QLabel *m_statusLabel;
    QSystemTrayIcon *m_trayIcon = nullptr;

    QTimer m_batchTimer;
    QVector<CanFrame> m_frameBuffer;
    bool m_sniffing = false;
    bool m_autoScroll = true;
};
