#pragma once
#include "CanFrame.h"
#include <cstdint>
#include <deque>
#include <vector>
#include <functional>

// Triggered recording: accumulates frames in a pre-trigger ring buffer,
// then records post-trigger frames until a configurable post-trigger count.
// Useful for capturing the context around an anomaly (e.g. a DTC set).
//
// Workflow:
//   1. setPreCount(N)  – keep last N frames before trigger
//   2. setPostCount(M) – capture M frames after trigger fires
//   3. setTrigger(fn)  – predicate called for each frame; returning true fires
//   4. Feed frames via addFrame()
//   5. Check triggered() to know if armed
//   6. Read capture() to get [pre + trigger + post] frames
//   7. reset() to arm again
class CanTriggerRecorder {
public:
    using TriggerFn = std::function<bool(const CanFrame &)>;

    void setPreCount(int n);   // pre-trigger ring size (default 0)
    void setPostCount(int n);  // post-trigger capture count (default 0)
    void setTrigger(TriggerFn fn);

    // Feed one frame. Returns true if this frame fired the trigger.
    bool addFrame(const CanFrame &frame);

    // True once the trigger has fired (and post-capture is complete or ongoing).
    bool triggered() const { return m_triggered; }

    // True once post-capture is complete (or post count == 0 and triggered).
    bool captureComplete() const;

    // The captured frames: [pre-trigger ring] + [trigger frame] + [post frames].
    const std::vector<CanFrame> &capture() const { return m_capture; }

    // Number of post-trigger frames still needed.
    int remainingPost() const { return m_remainingPost; }

    // Reset: disarms trigger, clears all buffers.
    void reset();

private:
    int       m_preCount  = 0;
    int       m_postCount = 0;
    TriggerFn m_trigger;

    std::deque<CanFrame>  m_pre;    // rolling pre-trigger ring
    std::vector<CanFrame> m_capture;
    bool                  m_triggered    = false;
    int                   m_remainingPost = 0;
};
