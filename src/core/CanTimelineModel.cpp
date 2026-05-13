#include "CanTimelineModel.h"
#include <algorithm>

CanTimelineModel::CanTimelineModel(int maxIds, int maxEvents)
    : m_maxIds(maxIds), m_maxEvents(maxEvents)
{}

void CanTimelineModel::addFrame(const CanFrame &frame, uint64_t timestampUs) {
    auto &state = m_idMap[frame.id];
    state.totalCount++;

    TimelineEvent ev;
    ev.timestampUs = timestampUs;
    ev.canId       = frame.id;
    ev.dlc         = frame.dlc;
    int toCopy = std::min(static_cast<int>(frame.dlc), 8);
    std::copy_n(frame.data.begin(), toCopy, std::begin(ev.data));

    state.events.push_back(ev);
    if (static_cast<int>(state.events.size()) > m_maxEvents)
        state.events.pop_front();

    if (m_totalEvents == 0 || timestampUs < m_minTs) m_minTs = timestampUs;
    if (timestampUs > m_maxTs) m_maxTs = timestampUs;
    ++m_totalEvents;
    m_dirty = true;
}

void CanTimelineModel::clear() {
    m_idMap.clear();
    m_trackedIds.clear();
    m_minTs = m_maxTs = 0;
    m_totalEvents = 0;
    m_dirty = false;
}

void CanTimelineModel::setMaxTrackedIds(int n) {
    m_maxIds = n;
    m_dirty  = true;
}

void CanTimelineModel::setMaxEventsPerid(int n) {
    m_maxEvents = n;
    // Trim existing deques
    for (auto &[id, state] : m_idMap)
        while (static_cast<int>(state.events.size()) > m_maxEvents)
            state.events.pop_front();
}

void CanTimelineModel::rebuildTracked() {
    // Collect all IDs sorted by total count descending
    std::vector<std::pair<uint64_t, uint32_t>> ranked; // (count, id)
    ranked.reserve(m_idMap.size());
    for (const auto &[id, state] : m_idMap)
        ranked.emplace_back(state.totalCount, id);
    std::sort(ranked.begin(), ranked.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });

    m_trackedIds.clear();
    int limit = std::min(m_maxIds, static_cast<int>(ranked.size()));
    for (int i = 0; i < limit; ++i)
        m_trackedIds.push_back(ranked[i].second);

    m_dirty = false;
}

std::vector<uint32_t> CanTimelineModel::trackedIds() const {
    if (m_dirty) const_cast<CanTimelineModel*>(this)->rebuildTracked();
    return m_trackedIds;
}

int CanTimelineModel::idIndex(uint32_t id) const {
    if (m_dirty) const_cast<CanTimelineModel*>(this)->rebuildTracked();
    for (int i = 0; i < static_cast<int>(m_trackedIds.size()); ++i)
        if (m_trackedIds[i] == id) return i;
    return -1;
}

std::vector<TimelineEvent> CanTimelineModel::eventsForId(uint32_t id) const {
    auto it = m_idMap.find(id);
    if (it == m_idMap.end()) return {};
    return {it->second.events.begin(), it->second.events.end()};
}

std::vector<TimelineEvent> CanTimelineModel::eventsInRange(uint32_t id,
                                                            uint64_t fromUs,
                                                            uint64_t toUs) const {
    auto it = m_idMap.find(id);
    if (it == m_idMap.end()) return {};

    std::vector<TimelineEvent> result;
    for (const auto &ev : it->second.events)
        if (ev.timestampUs >= fromUs && ev.timestampUs <= toUs)
            result.push_back(ev);
    return result;
}

std::vector<TimelineEvent> CanTimelineModel::allEventsInRange(uint64_t fromUs,
                                                               uint64_t toUs) const {
    if (m_dirty) const_cast<CanTimelineModel*>(this)->rebuildTracked();
    std::vector<TimelineEvent> result;
    for (const auto &id : m_trackedIds) {
        auto it = m_idMap.find(id);
        if (it == m_idMap.end()) continue;
        for (const auto &ev : it->second.events)
            if (ev.timestampUs >= fromUs && ev.timestampUs <= toUs)
                result.push_back(ev);
    }
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b) { return a.timestampUs < b.timestampUs; });
    return result;
}

std::pair<uint64_t, uint64_t> CanTimelineModel::timeRange() const {
    if (m_totalEvents == 0) return {0, 0};
    return {m_minTs, m_maxTs};
}
