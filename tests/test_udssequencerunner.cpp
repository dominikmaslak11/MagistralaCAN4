#include <gtest/gtest.h>
#include "core/UdsSequenceRunner.h"
#include "core/ICanDriver.h"
#include <QCoreApplication>

// ── Minimal sniffer stub ──────────────────────────────────────────────────────
// UdsSequenceRunner only calls sniffer->writeFrame(); we capture those calls.

#include "core/CanSniffer.h"

// ── Helpers ───────────────────────────────────────────────────────────────────

static CanFrame makeResponse(uint32_t id, std::initializer_list<uint8_t> bytes) {
    CanFrame f{};
    f.id  = id;
    f.dlc = static_cast<uint8_t>(bytes.size());
    int i = 0;
    for (uint8_t b : bytes) f.data[i++] = b;
    return f;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(UdsSequenceRunner, SingleStepPassOnPositiveResponse) {
    UdsSequenceRunner runner(nullptr); // null sniffer — no actual TX

    UdsStep step;
    step.name      = "DiagSession";
    step.txCanId   = 0x7DF;
    step.rxCanId   = 0x7E8;
    step.payload   = {0x10, 0x01};
    step.timeoutMs = 500;
    step.maxRetries = 0;
    runner.setSteps({step});

    bool finished = false;
    bool allPassed = false;
    QObject::connect(&runner, &UdsSequenceRunner::sequenceFinished,
                     [&](bool p) { finished = true; allPassed = p; });

    runner.start();

    // Inject positive response: SID|0x40 = 0x50
    runner.processFrame(makeResponse(0x7E8, {0x50, 0x01}));

    EXPECT_TRUE(finished);
    EXPECT_TRUE(allPassed);
    ASSERT_EQ(runner.results().size(), 1u);
    EXPECT_EQ(runner.results()[0].status, UdsStepResult::Pass);
}

TEST(UdsSequenceRunner, NegativeResponseAborts) {
    UdsSequenceRunner runner(nullptr);

    UdsStep s1; s1.txCanId = 0x7DF; s1.rxCanId = 0x7E8;
    s1.payload = {0x10, 0x03}; s1.timeoutMs = 500; s1.maxRetries = 0;
    s1.stopOnNegResponse = true; s1.name = "Step1";

    UdsStep s2 = s1; s2.name = "Step2";

    runner.setSteps({s1, s2});

    bool finished = false; bool allPassed = true;
    QObject::connect(&runner, &UdsSequenceRunner::sequenceFinished,
                     [&](bool p) { finished = true; allPassed = p; });

    runner.start();
    // Negative response: 0x7F SID NRC
    runner.processFrame(makeResponse(0x7E8, {0x7F, 0x10, 0x22}));

    EXPECT_TRUE(finished);
    EXPECT_FALSE(allPassed);
    EXPECT_EQ(runner.results()[0].status, UdsStepResult::NegResponse);
    // Second step should be Skipped
    EXPECT_EQ(runner.results()[1].status, UdsStepResult::Skipped);
}

TEST(UdsSequenceRunner, WrongRxIdIgnored) {
    UdsSequenceRunner runner(nullptr);

    UdsStep step; step.txCanId = 0x7DF; step.rxCanId = 0x7E8;
    step.payload = {0x22, 0xF1, 0x90}; step.timeoutMs = 500; step.maxRetries = 0;
    step.stopOnNegResponse = false; step.name = "ReadVIN";
    runner.setSteps({step});

    runner.start();
    EXPECT_TRUE(runner.isRunning());

    // Response from wrong ID — should be ignored
    runner.processFrame(makeResponse(0x7E9, {0x62, 0xF1, 0x90}));
    EXPECT_TRUE(runner.isRunning()); // still running, not answered

    // Correct RX ID
    runner.processFrame(makeResponse(0x7E8, {0x62, 0xF1, 0x90}));
    EXPECT_FALSE(runner.isRunning());
    EXPECT_EQ(runner.results()[0].status, UdsStepResult::Pass);
}

TEST(UdsSequenceRunner, AbortMarksRemaining) {
    UdsSequenceRunner runner(nullptr);

    UdsStep s; s.txCanId = 0x7DF; s.rxCanId = 0x7E8;
    s.payload = {0x10, 0x01}; s.timeoutMs = 5000; s.maxRetries = 0;
    s.stopOnNegResponse = false; s.name = "S";
    runner.setSteps({s, s, s}); // 3 steps

    bool finished = false; bool allPassed = true;
    QObject::connect(&runner, &UdsSequenceRunner::sequenceFinished,
                     [&](bool p) { finished = true; allPassed = p; });

    runner.start();
    runner.abort();

    EXPECT_TRUE(finished);
    EXPECT_FALSE(allPassed);
    // All steps (none responded) should be Skipped
    for (const auto &r : runner.results())
        EXPECT_EQ(r.status, UdsStepResult::Skipped);
}

TEST(UdsSequenceRunner, EmptyStepsDoNotStart) {
    UdsSequenceRunner runner(nullptr);
    runner.setSteps({});
    runner.start();
    EXPECT_FALSE(runner.isRunning());
}

TEST(UdsSequenceRunner, MultiStepAllPass) {
    UdsSequenceRunner runner(nullptr);

    auto makeStep = [](uint8_t sid) {
        UdsStep s; s.txCanId = 0x7DF; s.rxCanId = 0x7E8;
        s.payload = {sid}; s.timeoutMs = 500; s.maxRetries = 0;
        s.stopOnNegResponse = false; s.name = "S";
        return s;
    };
    runner.setSteps({makeStep(0x10), makeStep(0x22), makeStep(0x14)});

    bool finished = false; bool allPassed = false;
    QObject::connect(&runner, &UdsSequenceRunner::sequenceFinished,
                     [&](bool p) { finished = true; allPassed = p; });

    runner.start();
    runner.processFrame(makeResponse(0x7E8, {0x50})); // step 1 pass
    runner.processFrame(makeResponse(0x7E8, {0x62})); // step 2 pass
    runner.processFrame(makeResponse(0x7E8, {0x54})); // step 3 pass

    EXPECT_TRUE(finished);
    EXPECT_TRUE(allPassed);
    for (const auto &r : runner.results())
        EXPECT_EQ(r.status, UdsStepResult::Pass);
}
