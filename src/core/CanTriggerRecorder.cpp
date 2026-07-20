#include "CanTriggerRecorder.h"

void CanTriggerRecorder::setPreCount(int n) {
    m_preCount = (n < 0) ? 0 : n;
}

void CanTriggerRecorder::setPostCount(int n) {
    m_postCount = (n < 0) ? 0 : n;
}

void CanTriggerRecorder::setTrigger(TriggerFn fn) {
    m_trigger = std::move(fn);
}

bool CanTriggerRecorder::addFrame(const CanFrame &frame) {
    if (!m_triggered) {
        // Evaluate trigger BEFORE modifying the pre ring so the pre ring
        // holds frames that arrived strictly before this one.
        bool fire = m_trigger && m_trigger(frame);
        if (fire) {
            m_triggered     = true;
            m_remainingPost = m_postCount;
            // Capture = pre-ring frames + trigger frame
            m_capture.reserve(m_pre.size() + 1);
            for (const auto &f : m_pre) m_capture.push_back(f);
            m_capture.push_back(frame);
        } else {
            // Maintain rolling pre ring
            m_pre.push_back(frame);
            if (static_cast<int>(m_pre.size()) > m_preCount)
                m_pre.pop_front();
        }
        return fire;
    }

    // Post-trigger: collect post frames
    if (m_remainingPost > 0) {
        m_capture.push_back(frame);
        --m_remainingPost;
    }
    return false;
}

bool CanTriggerRecorder::captureComplete() const {
    return m_triggered && (m_remainingPost == 0);
}

void CanTriggerRecorder::reset() {
    m_pre.clear();
    m_capture.clear();
    m_triggered     = false;
    m_remainingPost = 0;
}
