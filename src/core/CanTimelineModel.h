#pragma once
#include "CanFrame.h"
#include <vector>
#include <deque>
#include <unordered_map>
#include <cstdint>
#include <utility>

struct TimelineEvent {
    uint64_t timestampUs = 0;
    uint32_t canId       = 0;
    uint8_t  dlc         = 0;
    uint8_t  data[8]     = {};
};

// Pure-C++ (Qt-free) data model — accumulates CAN frame events, tracks the most
// frequent IDs, and exposes time-indexed data for the protocol timeline chart.
class CanTimelineModel {
public:
    static constexpr int kDefaultMaxIds    = 16;
    static constexpr int kDefaultMaxEvents = 500; // per-ID ring

    explicit CanTimelineModel(int maxIds    = kDefaultMaxIds,
                              int maxEvents = kDefaultMaxEvents);

    void addFrame(const CanFrame &frame, uint64_t timestampUs);
    void clear();

    void setMaxTrackedIds(int n);
    void setMaxEventsPerid(int n);

    // IDs currently tracked, sorted by total frame count (descending)
    std::vector<uint32_t> trackedIds() const;

    // Index of id in trackedIds() (-1 if not tracked)
    int idIndex(uint32_t id) const;

    // All stored events for a specific id (chronological)
    std::vector<TimelineEvent> eventsForId(uint32_t id) const;

    // Events for a specific id within [fromUs, toUs]
    std::vector<TimelineEvent> eventsInRange(uint32_t id,
                                             uint64_t fromUs,
                                             uint64_t toUs) const;

    // All tracked events in [fromUs, toUs], any ID
    std::vector<TimelineEvent> allEventsInRange(uint64_t fromUs, uint64_t toUs) const;

    std::pair<uint64_t, uint64_t> timeRange() const; // (minUs, maxUs), (0,0) if empty
    int totalEventCount() const { return m_totalEvents; }

private:
    void rebuildTracked();

    int m_maxIds;
    int m_maxEvents;

    struct IdState {
        uint64_t              totalCount = 0;
        std::deque<TimelineEvent> events;
    };

    std::unordered_map<uint32_t, IdState> m_idMap;
    std::vector<uint32_t>                 m_trackedIds; // cached, sorted
    uint64_t m_minTs = 0;
    uint64_t m_maxTs = 0;
    int      m_totalEvents = 0;
    bool     m_dirty = false; // trackedIds needs rebuild
};
