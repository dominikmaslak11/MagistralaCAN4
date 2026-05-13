#include <gtest/gtest.h>
#include "core/CanAlertEngine.h"
#include "core/CanFrame.h"

static CanFrame makeFrame(uint32_t id, uint8_t dlc, uint8_t fill = 0x00) {
    CanFrame f{};
    f.id  = id;
    f.dlc = dlc;
    for (int i = 0; i < dlc && i < 8; ++i) f.data[i] = fill;
    return f;
}

// ── Helpers ──────────────────────────────────────────────────────────────────

static CanAlertRule makeRule(AlertType type, const QString &name = "R",
                             AlertAction action = AlertAction::Log) {
    CanAlertRule r;
    r.type   = type;
    r.name   = name;
    r.action = action;
    return r;
}

static CanAlertRule makeByteRule(uint32_t id, int byteIdx, ByteOp op, uint8_t thr) {
    CanAlertRule r;
    r.type      = AlertType::ByteValue;
    r.name      = "ByteR";
    r.action    = AlertAction::Log;
    r.targetId  = id;
    r.matchAny  = (id == 0);
    r.byteIdx   = byteIdx;
    r.op        = op;
    r.threshold = thr;
    return r;
}

// ── NewCanId ─────────────────────────────────────────────────────────────────

TEST(CanAlertEngine, NewCanIdTriggerOnFirst) {
    CanAlertEngine eng;
    eng.addRule(makeRule(AlertType::NewCanId));

    int count = 0;
    QObject::connect(&eng, &CanAlertEngine::alertTriggered,
                     [&](const CanAlert &) { ++count; });

    eng.evaluate(makeFrame(0x100, 8));
    EXPECT_EQ(count, 1);
}

TEST(CanAlertEngine, NewCanIdNoRepeat) {
    CanAlertEngine eng;
    eng.addRule(makeRule(AlertType::NewCanId));

    int count = 0;
    QObject::connect(&eng, &CanAlertEngine::alertTriggered,
                     [&](const CanAlert &) { ++count; });

    eng.evaluate(makeFrame(0x100, 8));
    eng.evaluate(makeFrame(0x100, 8));
    eng.evaluate(makeFrame(0x100, 8));
    EXPECT_EQ(count, 1);
}

TEST(CanAlertEngine, NewCanIdMultipleIds) {
    CanAlertEngine eng;
    eng.addRule(makeRule(AlertType::NewCanId));

    int count = 0;
    QObject::connect(&eng, &CanAlertEngine::alertTriggered,
                     [&](const CanAlert &) { ++count; });

    eng.evaluate(makeFrame(0x100, 8));
    eng.evaluate(makeFrame(0x200, 8));
    eng.evaluate(makeFrame(0x100, 8)); // no trigger
    eng.evaluate(makeFrame(0x300, 4)); // trigger
    EXPECT_EQ(count, 3);
}

// ── DlcChange ────────────────────────────────────────────────────────────────

TEST(CanAlertEngine, DlcChangeNoTriggerFirstFrame) {
    CanAlertEngine eng;
    eng.addRule(makeRule(AlertType::DlcChange));

    int count = 0;
    QObject::connect(&eng, &CanAlertEngine::alertTriggered,
                     [&](const CanAlert &) { ++count; });

    eng.evaluate(makeFrame(0x100, 8));
    EXPECT_EQ(count, 0);
}

TEST(CanAlertEngine, DlcChangeTrigger) {
    CanAlertEngine eng;
    eng.addRule(makeRule(AlertType::DlcChange));

    int count = 0;
    QObject::connect(&eng, &CanAlertEngine::alertTriggered,
                     [&](const CanAlert &) { ++count; });

    eng.evaluate(makeFrame(0x100, 8));
    eng.evaluate(makeFrame(0x100, 4)); // DLC changed
    EXPECT_EQ(count, 1);
}

TEST(CanAlertEngine, DlcChangeNoTriggerSameDlc) {
    CanAlertEngine eng;
    eng.addRule(makeRule(AlertType::DlcChange));

    int count = 0;
    QObject::connect(&eng, &CanAlertEngine::alertTriggered,
                     [&](const CanAlert &) { ++count; });

    eng.evaluate(makeFrame(0x100, 8));
    eng.evaluate(makeFrame(0x100, 8));
    eng.evaluate(makeFrame(0x100, 8));
    EXPECT_EQ(count, 0);
}

// ── ByteValue ────────────────────────────────────────────────────────────────

TEST(CanAlertEngine, ByteValueGT) {
    CanAlertEngine eng;
    eng.addRule(makeByteRule(0x100, 0, ByteOp::GT, 0x7F));

    int count = 0;
    QObject::connect(&eng, &CanAlertEngine::alertTriggered,
                     [&](const CanAlert &) { ++count; });

    eng.evaluate(makeFrame(0x100, 8, 0x7F)); // not >0x7F
    EXPECT_EQ(count, 0);
    eng.evaluate(makeFrame(0x100, 8, 0x80)); // >0x7F → trigger
    EXPECT_EQ(count, 1);
}

TEST(CanAlertEngine, ByteValueLT) {
    CanAlertEngine eng;
    eng.addRule(makeByteRule(0x200, 2, ByteOp::LT, 0x10));

    int count = 0;
    QObject::connect(&eng, &CanAlertEngine::alertTriggered,
                     [&](const CanAlert &) { ++count; });

    CanFrame f = makeFrame(0x200, 8, 0x00);
    f.data[2] = 0x05;
    eng.evaluate(f); // 0x05 < 0x10 → trigger
    EXPECT_EQ(count, 1);

    f.data[2] = 0x10;
    eng.evaluate(f); // 0x10 not < 0x10
    EXPECT_EQ(count, 1);
}

TEST(CanAlertEngine, ByteValueEQ) {
    CanAlertEngine eng;
    eng.addRule(makeByteRule(0x300, 0, ByteOp::EQ, 0xAB));

    int count = 0;
    QObject::connect(&eng, &CanAlertEngine::alertTriggered,
                     [&](const CanAlert &) { ++count; });

    eng.evaluate(makeFrame(0x300, 8, 0xAB)); // trigger
    eng.evaluate(makeFrame(0x300, 8, 0xAC)); // no
    EXPECT_EQ(count, 1);
}

TEST(CanAlertEngine, ByteValueIdFilterNoMatch) {
    CanAlertEngine eng;
    eng.addRule(makeByteRule(0x100, 0, ByteOp::GT, 0x00));

    int count = 0;
    QObject::connect(&eng, &CanAlertEngine::alertTriggered,
                     [&](const CanAlert &) { ++count; });

    eng.evaluate(makeFrame(0x200, 8, 0xFF)); // wrong ID
    EXPECT_EQ(count, 0);
}

// ── Disabled rule ────────────────────────────────────────────────────────────

TEST(CanAlertEngine, DisabledRuleDoesNotTrigger) {
    CanAlertEngine eng;
    CanAlertRule r = makeRule(AlertType::NewCanId);
    r.enabled = false;
    eng.addRule(r);

    int count = 0;
    QObject::connect(&eng, &CanAlertEngine::alertTriggered,
                     [&](const CanAlert &) { ++count; });

    eng.evaluate(makeFrame(0x100, 8));
    EXPECT_EQ(count, 0);
}

// ── Multiple rules ───────────────────────────────────────────────────────────

TEST(CanAlertEngine, MultipleRulesAllEvaluated) {
    CanAlertEngine eng;
    eng.addRule(makeRule(AlertType::NewCanId, "R1"));
    eng.addRule(makeByteRule(0x100, 0, ByteOp::GT, 0x00));

    int count = 0;
    QObject::connect(&eng, &CanAlertEngine::alertTriggered,
                     [&](const CanAlert &) { ++count; });

    eng.evaluate(makeFrame(0x100, 8, 0xFF)); // NewCanId + ByteValue → 2 alerts
    EXPECT_EQ(count, 2);
}

// ── Reset ────────────────────────────────────────────────────────────────────

TEST(CanAlertEngine, ResetClearsState) {
    CanAlertEngine eng;
    eng.addRule(makeRule(AlertType::NewCanId));

    int count = 0;
    QObject::connect(&eng, &CanAlertEngine::alertTriggered,
                     [&](const CanAlert &) { ++count; });

    eng.evaluate(makeFrame(0x100, 8)); // trigger
    eng.reset();
    eng.evaluate(makeFrame(0x100, 8)); // trigger again after reset
    EXPECT_EQ(count, 2);
}

TEST(CanAlertEngine, TotalAlertCount) {
    CanAlertEngine eng;
    eng.addRule(makeRule(AlertType::NewCanId));
    eng.evaluate(makeFrame(0x100, 8));
    eng.evaluate(makeFrame(0x200, 8));
    EXPECT_EQ(eng.totalAlerts(), 2);
    eng.reset();
    EXPECT_EQ(eng.totalAlerts(), 0);
}

TEST(CanAlertEngine, AlertDescriptionContainsId) {
    CanAlertEngine eng;
    eng.addRule(makeRule(AlertType::NewCanId));

    QString desc;
    QObject::connect(&eng, &CanAlertEngine::alertTriggered,
                     [&](const CanAlert &a) { desc = a.description; });

    eng.evaluate(makeFrame(0x1AB, 8));
    EXPECT_TRUE(desc.contains("1ab", Qt::CaseInsensitive));
}

// ── IsolationForestScore ──────────────────────────────────────────────────────

TEST(CanAlertEngine, IsoForestScoreAboveThresholdTriggers) {
    CanAlertEngine eng;
    CanAlertRule r;
    r.type        = AlertType::IsolationForestScore;
    r.name        = "IsoTest";
    r.action      = AlertAction::Log;
    r.isThreshold = 0.5;
    r.scorer      = []() { return 0.8; };  // always returns high score
    eng.addRule(r);

    int count = 0;
    QObject::connect(&eng, &CanAlertEngine::alertTriggered,
                     [&](const CanAlert &) { ++count; });

    eng.evaluate(makeFrame(0x100, 8));
    EXPECT_EQ(count, 1);
}

TEST(CanAlertEngine, IsoForestScoreBelowThresholdNoTrigger) {
    CanAlertEngine eng;
    CanAlertRule r;
    r.type        = AlertType::IsolationForestScore;
    r.name        = "IsoTest";
    r.action      = AlertAction::Log;
    r.isThreshold = 0.9;
    r.scorer      = []() { return 0.3; };  // low score
    eng.addRule(r);

    int count = 0;
    QObject::connect(&eng, &CanAlertEngine::alertTriggered,
                     [&](const CanAlert &) { ++count; });

    eng.evaluate(makeFrame(0x100, 8));
    EXPECT_EQ(count, 0);
}

TEST(CanAlertEngine, IsoForestNoScorerNoTrigger) {
    CanAlertEngine eng;
    CanAlertRule r;
    r.type        = AlertType::IsolationForestScore;
    r.name        = "IsoNoScorer";
    r.action      = AlertAction::Log;
    r.isThreshold = 0.0;
    // r.scorer not set — should not crash or trigger
    eng.addRule(r);

    int count = 0;
    QObject::connect(&eng, &CanAlertEngine::alertTriggered,
                     [&](const CanAlert &) { ++count; });

    eng.evaluate(makeFrame(0x200, 8));
    EXPECT_EQ(count, 0);
}
