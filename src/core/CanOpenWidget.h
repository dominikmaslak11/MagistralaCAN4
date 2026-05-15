#pragma once
#include <QWidget>
#include <QTableView>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTimer>
#include <QAbstractTableModel>
#include <QVector>
#include <QHash>
#include "CanOpenFrame.h"

// ── Frame table model ─────────────────────────────────────────────────────────

class CanOpenTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col { TIME, CAN_ID, NODE, TYPE, TYPE_NAME, DETAIL, DATA, _COUNT };
    explicit CanOpenTableModel(QObject *p = nullptr);
    int rowCount(const QModelIndex & = QModelIndex()) const override { return m_frames.size(); }
    int columnCount(const QModelIndex & = QModelIndex()) const override { return _COUNT; }
    QVariant data(const QModelIndex &idx, int role) const override;
    QVariant headerData(int s, Qt::Orientation o, int role) const override;
    void addFrame(const CanOpenFrame &f);
    void clear();
    void setParser(const CanOpenParser *p) { m_parser = p; }
    void setNodeFilter(int nodeId) { m_filterNode = nodeId; } // -1 = all
private:
    QVector<CanOpenFrame> m_frames;
    const CanOpenParser  *m_parser     = nullptr;
    int                   m_filterNode = -1;
    static constexpr int  MAX          = 2000;
};

// ── Heartbeat node state ──────────────────────────────────────────────────────

struct CanOpenNodeState {
    uint8_t  nodeId        = 0;
    uint8_t  state         = 0xFF;  // last heartbeat state byte
    uint64_t lastSeenUs    = 0;
    int64_t  lastSeenWallMs= 0;     // QDateTime::currentMSecsSinceEpoch()
    uint32_t hbCount       = 0;
    bool     timedOut      = false;
};

// ── Main widget ───────────────────────────────────────────────────────────────

class CanOpenWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanOpenWidget(QWidget *parent = nullptr);

public slots:
    void processFrame(const CanFrame &frame);

private slots:
    void onNodeFilterChanged(const QString &text);
    void checkHeartbeatTimeouts();

private:
    void setupUi();
    void updateNodeState(uint8_t nodeId, uint8_t stateCode, uint64_t ts);
    void refreshNodeTable();

    CanOpenParser       m_parser;
    CanOpenTableModel  *m_model        = nullptr;
    QTableView         *m_table        = nullptr;
    QLabel             *m_statusLabel  = nullptr;
    QPushButton        *m_clearBtn     = nullptr;
    QLineEdit          *m_nodeFilter   = nullptr;

    // Heartbeat monitor
    QTableWidget       *m_nodeTable    = nullptr;
    QHash<uint8_t, CanOpenNodeState> m_nodes;
    QTimer              m_hbTimer;
    static constexpr uint64_t kHeartbeatTimeoutUs = 5'000'000; // 5s

    int m_totalFrames = 0;
};
