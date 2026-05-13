#include "CanObservationDb.h"
#include "PcapExporter.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDateTime>
#include <QDebug>
#include <chrono>
#include <unordered_map>

CanObservationDb::CanObservationDb(const QString &connectionName)
    : m_connectionName(connectionName)
{}

CanObservationDb::~CanObservationDb() {
    close();
}

bool CanObservationDb::open(const QString &path) {
    if (QSqlDatabase::contains(m_connectionName)) {
        m_db = QSqlDatabase(); // release reference before removing
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        qWarning() << "CanObservationDb: cannot open" << path << m_db.lastError().text();
        return false;
    }

    // WAL mode — faster concurrent writes, no journal file bloat
    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec("PRAGMA synchronous=NORMAL");

    createSchema();
    return true;
}

void CanObservationDb::close() {
    if (!m_db.isOpen()) return;
    flush();
    endSession();
    m_db.close();
    m_db = QSqlDatabase(); // release internal reference before removing
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool CanObservationDb::isOpen() const {
    return m_db.isOpen();
}

void CanObservationDb::createSchema() {
    QSqlQuery q(m_db);
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS sessions (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            label     TEXT,
            started   INTEGER NOT NULL,
            ended     INTEGER
        )
    )");
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS frames (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id  INTEGER NOT NULL REFERENCES sessions(id),
            ts_us       INTEGER NOT NULL,
            can_id      INTEGER NOT NULL,
            extended    INTEGER NOT NULL,
            dlc         INTEGER NOT NULL,
            data        BLOB    NOT NULL
        )
    )");
    q.exec("CREATE INDEX IF NOT EXISTS idx_frames_can_id    ON frames(can_id)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_frames_session   ON frames(session_id)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_frames_ts        ON frames(ts_us)");
}

int64_t CanObservationDb::beginSession(const QString &label) {
    flush();
    endSession();

    int64_t now = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO sessions (label, started) VALUES (?, ?)");
    q.addBindValue(label.isEmpty() ? QVariant{} : QVariant(label));
    q.addBindValue(static_cast<qlonglong>(now));
    q.exec();
    m_sessionId = q.lastInsertId().toLongLong();
    return m_sessionId;
}

void CanObservationDb::endSession() {
    if (m_sessionId < 0) return;
    int64_t now = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery q(m_db);
    q.prepare("UPDATE sessions SET ended=? WHERE id=?");
    q.addBindValue(static_cast<qlonglong>(now));
    q.addBindValue(static_cast<qlonglong>(m_sessionId));
    q.exec();
    m_sessionId = -1;
}

void CanObservationDb::recordFrame(const CanFrame &frame, uint64_t timestampUs) {
    if (!m_db.isOpen() || m_sessionId < 0) return;

    if (timestampUs == 0) {
        using namespace std::chrono;
        timestampUs = static_cast<uint64_t>(
            duration_cast<microseconds>(system_clock::now().time_since_epoch()).count());
    }

    PendingRow row;
    row.sessionId   = m_sessionId;
    row.timestampUs = timestampUs;
    row.canId       = frame.id;
    row.extended    = frame.extended;
    row.dlc         = frame.dlc;
    int toCopy = std::min(static_cast<int>(frame.dlc), 8);
    std::copy_n(std::begin(frame.data), toCopy, std::begin(row.data));
    m_pending.push_back(row);

    if (static_cast<int>(m_pending.size()) >= kBatchSize)
        insertBatch();
}

void CanObservationDb::flush() {
    if (!m_pending.empty())
        insertBatch();
}

void CanObservationDb::insertBatch() {
    if (m_pending.empty()) return;

    m_db.transaction();
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO frames (session_id, ts_us, can_id, extended, dlc, data) "
              "VALUES (?, ?, ?, ?, ?, ?)");

    for (const auto &row : m_pending) {
        q.addBindValue(static_cast<long long>(row.sessionId));
        q.addBindValue(static_cast<long long>(row.timestampUs));
        q.addBindValue(static_cast<long long>(row.canId));
        q.addBindValue(row.extended ? 1 : 0);
        q.addBindValue(row.dlc);
        q.addBindValue(QByteArray(reinterpret_cast<const char*>(row.data), row.dlc));
        if (!q.exec())
            qWarning() << "CanObservationDb: insert error" << q.lastError().text();
        else
            ++m_insertCount;
    }
    m_db.commit();
    m_pending.clear();
}

std::vector<DbFrameRow> CanObservationDb::queryByCanId(uint32_t id, int64_t sessionId, int limit) const {
    std::vector<DbFrameRow> result;
    if (!m_db.isOpen()) return result;

    QSqlQuery q(m_db);
    if (sessionId >= 0) {
        q.prepare("SELECT id,session_id,ts_us,can_id,extended,dlc,data "
                  "FROM frames WHERE can_id=? AND session_id=? ORDER BY ts_us LIMIT ?");
        q.addBindValue(static_cast<long long>(id));
        q.addBindValue(static_cast<long long>(sessionId));
        q.addBindValue(limit);
    } else {
        q.prepare("SELECT id,session_id,ts_us,can_id,extended,dlc,data "
                  "FROM frames WHERE can_id=? ORDER BY ts_us LIMIT ?");
        q.addBindValue(static_cast<long long>(id));
        q.addBindValue(limit);
    }
    q.exec();

    while (q.next()) {
        DbFrameRow row;
        row.rowid       = q.value(0).toLongLong();
        row.sessionId   = q.value(1).toLongLong();
        row.timestampUs = static_cast<uint64_t>(q.value(2).toLongLong());
        row.canId       = static_cast<uint32_t>(q.value(3).toLongLong());
        row.extended    = q.value(4).toInt() != 0;
        row.dlc         = static_cast<uint8_t>(q.value(5).toInt());
        QByteArray blob = q.value(6).toByteArray();
        std::fill(std::begin(row.data), std::end(row.data), 0);
        for (int i = 0; i < blob.size() && i < 8; ++i)
            row.data[i] = static_cast<uint8_t>(blob[i]);
        result.push_back(row);
    }
    return result;
}

int64_t CanObservationDb::totalFrameCount(int64_t sessionId) const {
    if (!m_db.isOpen()) return 0;
    QSqlQuery q(m_db);
    if (sessionId >= 0) {
        q.prepare("SELECT COUNT(*) FROM frames WHERE session_id=?");
        q.addBindValue(static_cast<long long>(sessionId));
    } else {
        q.prepare("SELECT COUNT(*) FROM frames");
    }
    q.exec();
    if (q.next()) return q.value(0).toLongLong();
    return 0;
}

std::vector<std::pair<int64_t, QString>> CanObservationDb::listSessions() const {
    std::vector<std::pair<int64_t, QString>> result;
    if (!m_db.isOpen()) return result;
    QSqlQuery q("SELECT id, COALESCE(label,'') FROM sessions ORDER BY started DESC", m_db);
    while (q.next())
        result.emplace_back(q.value(0).toLongLong(), q.value(1).toString());
    return result;
}

std::vector<DbFrameRow> CanObservationDb::queryTimeRange(uint64_t fromUs, uint64_t toUs,
                                                          int64_t sessionId, int limit) const {
    std::vector<DbFrameRow> result;
    if (!m_db.isOpen()) return result;

    QSqlQuery q(m_db);
    if (sessionId >= 0) {
        q.prepare("SELECT id,session_id,ts_us,can_id,extended,dlc,data "
                  "FROM frames WHERE ts_us>=? AND ts_us<=? AND session_id=? ORDER BY ts_us LIMIT ?");
        q.addBindValue(static_cast<long long>(fromUs));
        q.addBindValue(static_cast<long long>(toUs));
        q.addBindValue(static_cast<long long>(sessionId));
        q.addBindValue(limit);
    } else {
        q.prepare("SELECT id,session_id,ts_us,can_id,extended,dlc,data "
                  "FROM frames WHERE ts_us>=? AND ts_us<=? ORDER BY ts_us LIMIT ?");
        q.addBindValue(static_cast<long long>(fromUs));
        q.addBindValue(static_cast<long long>(toUs));
        q.addBindValue(limit);
    }
    q.exec();

    while (q.next()) {
        DbFrameRow row;
        row.rowid       = q.value(0).toLongLong();
        row.sessionId   = q.value(1).toLongLong();
        row.timestampUs = static_cast<uint64_t>(q.value(2).toLongLong());
        row.canId       = static_cast<uint32_t>(q.value(3).toLongLong());
        row.extended    = q.value(4).toInt() != 0;
        row.dlc         = static_cast<uint8_t>(q.value(5).toInt());
        QByteArray blob = q.value(6).toByteArray();
        std::fill(std::begin(row.data), std::end(row.data), 0);
        for (int i = 0; i < blob.size() && i < 8; ++i)
            row.data[i] = static_cast<uint8_t>(blob[i]);
        result.push_back(row);
    }
    return result;
}

std::vector<CanObservationDb::DlcAnomaly> CanObservationDb::findDlcAnomalies(int64_t sessionId) const {
    std::vector<DlcAnomaly> result;
    if (!m_db.isOpen()) return result;

    // Step 1: find most common DLC per can_id (mode)
    QSqlQuery q1(m_db);
    if (sessionId >= 0) {
        q1.prepare("SELECT can_id, dlc, COUNT(*) as cnt "
                   "FROM frames WHERE session_id=? "
                   "GROUP BY can_id, dlc ORDER BY can_id, cnt DESC");
        q1.addBindValue(static_cast<long long>(sessionId));
    } else {
        q1.prepare("SELECT can_id, dlc, COUNT(*) as cnt "
                   "FROM frames GROUP BY can_id, dlc ORDER BY can_id, cnt DESC");
    }
    q1.exec();

    std::unordered_map<uint32_t, uint8_t> modeDlc;
    while (q1.next()) {
        uint32_t cid = static_cast<uint32_t>(q1.value(0).toLongLong());
        if (modeDlc.find(cid) == modeDlc.end()) // first row = highest count = mode
            modeDlc[cid] = static_cast<uint8_t>(q1.value(1).toInt());
    }

    // Step 2: find frames whose DLC != mode
    QSqlQuery q2(m_db);
    const QString baseQuery =
        "SELECT f.id, f.can_id, f.dlc, f.ts_us FROM frames f "
        "%1 ORDER BY f.can_id, f.ts_us LIMIT 10000";
    if (sessionId >= 0) {
        q2.prepare(baseQuery.arg("WHERE f.session_id=?"));
        q2.addBindValue(static_cast<long long>(sessionId));
    } else {
        q2.prepare(baseQuery.arg(""));
    }
    q2.exec();

    while (q2.next()) {
        uint32_t cid = static_cast<uint32_t>(q2.value(1).toLongLong());
        uint8_t  dlc = static_cast<uint8_t>(q2.value(2).toInt());
        auto it = modeDlc.find(cid);
        if (it != modeDlc.end() && dlc != it->second) {
            DlcAnomaly a;
            a.rowid       = q2.value(0).toLongLong();
            a.canId       = cid;
            a.expectedDlc = it->second;
            a.actualDlc   = dlc;
            a.timestampUs = static_cast<uint64_t>(q2.value(3).toLongLong());
            result.push_back(a);
        }
    }
    return result;
}

std::vector<CanObservationDb::IdFrequency> CanObservationDb::computeIdFrequencies(int64_t sessionId) const {
    std::vector<IdFrequency> result;
    if (!m_db.isOpen()) return result;

    QSqlQuery q(m_db);
    if (sessionId >= 0) {
        q.prepare("SELECT can_id, COUNT(*) as cnt, MIN(ts_us), MAX(ts_us) "
                  "FROM frames WHERE session_id=? GROUP BY can_id ORDER BY cnt DESC");
        q.addBindValue(static_cast<long long>(sessionId));
    } else {
        q.prepare("SELECT can_id, COUNT(*) as cnt, MIN(ts_us), MAX(ts_us) "
                  "FROM frames GROUP BY can_id ORDER BY cnt DESC");
    }
    q.exec();

    while (q.next()) {
        IdFrequency f;
        f.canId      = static_cast<uint32_t>(q.value(0).toLongLong());
        f.frameCount = q.value(1).toLongLong();
        uint64_t minTs = static_cast<uint64_t>(q.value(2).toLongLong());
        uint64_t maxTs = static_cast<uint64_t>(q.value(3).toLongLong());
        f.avgIntervalUs = (f.frameCount > 1 && maxTs > minTs)
                          ? static_cast<double>(maxTs - minTs) / (f.frameCount - 1)
                          : 0.0;
        result.push_back(f);
    }
    return result;
}

bool CanObservationDb::exportToPcap(const QString &path, uint32_t canId,
                                    int64_t sessionId, int limit) const {
    auto rows = queryByCanId(canId, sessionId, limit);
    std::vector<CanFrame> frames;
    std::vector<uint64_t> timestamps;
    frames.reserve(rows.size());
    timestamps.reserve(rows.size());
    for (const auto &row : rows) {
        CanFrame f{};
        f.id       = row.canId;
        f.extended = row.extended;
        f.dlc      = row.dlc;
        std::copy(std::begin(row.data), std::end(row.data), std::begin(f.data));
        frames.push_back(f);
        timestamps.push_back(row.timestampUs);
    }
    return PcapExporter::exportFrames(path, frames, timestamps);
}

void CanObservationDb::reset() {
    flush();
    endSession();
    if (!m_db.isOpen()) return;
    QSqlQuery q(m_db);
    q.exec("DELETE FROM frames");
    q.exec("DELETE FROM sessions");
    q.exec("DELETE FROM sqlite_sequence WHERE name IN ('frames','sessions')");
    m_insertCount = 0;
}
