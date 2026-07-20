#pragma once
#include <QMainWindow>
#include <QTableView>
#include <QPushButton>
#include <QTimer>
#include <QVector>
#include "core/CanSniffer.h"
#include "core/CanFrameModel.h"
class WebSocketServer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void toggleSniffing();
    void onNewFrame(const CanFrame &frame);
    void updateTableBatch();
    void toggleWebSocket();

private:
    void setupStyle();
    void setupToolBar();
    void setupCentralWidget();

    CanSniffer m_sniffer;
    CanFrameModel *m_model;
    QTableView *m_tableView;
    QPushButton *m_btnStartStop;
    QPushButton *m_btnWsServer;
    WebSocketServer *m_wsServer;

    QTimer m_batchTimer;
    QVector<CanFrame> m_frameBuffer;  // bufor zbierający ramki z wątku sniffera
    bool m_sniffing = false;
    bool m_wsRunning = false;
};
