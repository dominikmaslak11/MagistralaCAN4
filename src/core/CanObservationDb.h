#pragma once
#include "CanFrame.h"
#include <QString>
#include <QSqlDatabase>
#include <vector>
#include <utility>
#include <cstdint>

struct DbFrameRow {
    int64_t  rowid;
    int64_t  sessionId;
    uint64_t timestampUs;
    uint32_t canId;
    bool     extended;
    uint8_t  dlc;
    uint8_t  data[8];
};

class CanObservationDb {
public:
    explicit CanObservationDb(const QString &connectionName = "can_obs");
    ~CanObservationDb();

    bool open(const QString &path);
    void close();
    bool isOpen() const;

    int64_t beginSession(const QString &label = {});
    void endSession();
    int64_t currentSessionId() const { return m_sessionId; }

    void recordFrame(const CanFrame &frame, uint64_t timestampUs = 0);
    void flush();

    std::vector<DbFrameRow> queryByCanId(uint32_t id, int64_t sessionId = -1, int limit = 1000) const;
    std::vector<DbFrameRow> queryTimeRange(uint64_t fromUs, uint64_t toUs,
                                           int64_t sessionId = -1, int limit = 10000) const;
    int64_t totalFrameCount(int64_t sessionId = -1) const;
    std::vector<std::pair<int64_t, QString>> listSessions() const;

    // Analytics
    struct DlcAnomaly {
        int64_t  rowid;
        uint32_t canId;
        uint8_t  expectedDlc; // most common DLC for this ID
        uint8_t  actualDlc;
        uint64_t timestampUs;
    };
    std::vector<DlcAnomaly> findDlcAnomalies(int64_t sessionId = -1) const;

    struct IdFrequency {
        uint32_t canId;
        int64_t  frameCount;
        double   avgIntervalUs; // 0 if only one frame
    };
    std::vector<IdFrequency> computeIdFrequencies(int64_t sessionId = -1) const;

    // Export current query result (queryByCanId) to PCAP
    bool exportToPcap(const QString &path, uint32_t canId,
                      int64_t sessionId = -1, int limit = 100000) const;

    void reset();

    static constexpr int kBatchSize = 500;

private:
    void createSchema();
    void insertBatch();

    QString          m_connectionName;
    QSqlDatabase     m_db;
    int64_t          m_sessionId = -1;
    uint64_t         m_insertCount = 0;

    struct PendingRow {
        int64_t  sessionId;
        uint64_t timestampUs;
        uint32_t canId;
        bool     extended;
        uint8_t  dlc;
        uint8_t  data[8];
    };
    std::vector<PendingRow> m_pending;
};
