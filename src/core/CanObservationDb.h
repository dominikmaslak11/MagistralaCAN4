#pragma once
#include "CanFrame.h"
#include <QString>
#include <QSqlDatabase>
#include <vector>
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
    int64_t totalFrameCount(int64_t sessionId = -1) const;
    std::vector<std::pair<int64_t, QString>> listSessions() const;

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
