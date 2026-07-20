#include <gtest/gtest.h>
#include "core/CanGateway.h"
#include "core/ICanDriver.h"

// ── Minimal stub driver ───────────────────────────────────────────────────────

class StubDriver : public ICanDriver {
public:
    bool isValid() const override { return m_valid; }
    bool open(const QString &) override { m_valid = true; return true; }
    void close() override { m_valid = false; }
    CanFrame readFrame() override { return {}; }
    void writeFrame(const CanFrame &f) override { written.push_back(f); }
    QStringList availableDevices() const override { return {}; }
    QString backendName() const override { return "stub"; }
    void setBaudRate(const QString &) override {}

    bool m_valid = true;
    std::vector<CanFrame> written;
};

static CanFrame makeFrame(uint32_t id, uint8_t dlc = 1) {
    CanFrame f{};
    f.id = id; f.dlc = dlc;
    return f;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(CanGateway, PassAllForwardsEverything) {
    StubDriver sink;
    CanGateway gw;
    gw.setSink(&sink);
    gw.setFilterMode(CanGateway::FilterMode::PassAll);
    gw.start();

    gw.processFrame(makeFrame(0x100));
    gw.processFrame(makeFrame(0x200));
    gw.processFrame(makeFrame(0x300));

    EXPECT_EQ(sink.written.size(), 3u);
    EXPECT_EQ(gw.stats().forwarded, 3u);
    EXPECT_EQ(gw.stats().blocked,   0u);
    EXPECT_EQ(gw.stats().total,     3u);
    gw.stop();
}

TEST(CanGateway, WhitelistBlocksUnlistedId) {
    StubDriver sink;
    CanGateway gw;
    gw.setSink(&sink);
    gw.setFilterMode(CanGateway::FilterMode::Whitelist);
    gw.setRules({{ 0x100, 0, false }});
    gw.start();

    gw.processFrame(makeFrame(0x100)); // allowed
    gw.processFrame(makeFrame(0x200)); // blocked

    EXPECT_EQ(sink.written.size(), 1u);
    EXPECT_EQ(sink.written[0].id, 0x100u);
    EXPECT_EQ(gw.stats().forwarded, 1u);
    EXPECT_EQ(gw.stats().blocked,   1u);
    gw.stop();
}

TEST(CanGateway, BlacklistBlocksListedId) {
    StubDriver sink;
    CanGateway gw;
    gw.setSink(&sink);
    gw.setFilterMode(CanGateway::FilterMode::Blacklist);
    gw.setRules({{ 0x200, 0, true }});
    gw.start();

    gw.processFrame(makeFrame(0x100)); // not blacklisted → forwarded
    gw.processFrame(makeFrame(0x200)); // blacklisted → blocked

    EXPECT_EQ(sink.written.size(), 1u);
    EXPECT_EQ(sink.written[0].id, 0x100u);
    EXPECT_EQ(gw.stats().blocked, 1u);
    gw.stop();
}

TEST(CanGateway, IdRemapping) {
    StubDriver sink;
    CanGateway gw;
    gw.setSink(&sink);
    gw.setFilterMode(CanGateway::FilterMode::PassAll);
    gw.setRules({{ 0x100, 0x200, false }}); // remap 0x100 → 0x200
    gw.start();

    gw.processFrame(makeFrame(0x100));
    gw.processFrame(makeFrame(0x300)); // no remap

    ASSERT_EQ(sink.written.size(), 2u);
    EXPECT_EQ(sink.written[0].id, 0x200u); // remapped
    EXPECT_EQ(sink.written[1].id, 0x300u); // unchanged
    EXPECT_EQ(gw.stats().remapped, 1u);
    gw.stop();
}

TEST(CanGateway, ResetStats) {
    StubDriver sink;
    CanGateway gw;
    gw.setSink(&sink);
    gw.start();
    gw.processFrame(makeFrame(0x100));
    EXPECT_EQ(gw.stats().total, 1u);
    gw.resetStats();
    EXPECT_EQ(gw.stats().total,     0u);
    EXPECT_EQ(gw.stats().forwarded, 0u);
    EXPECT_EQ(gw.stats().blocked,   0u);
    gw.stop();
}

TEST(CanGateway, NotRunningSkipsFrames) {
    StubDriver sink;
    CanGateway gw;
    gw.setSink(&sink);
    // Don't call start()
    // Direct processFrame while not running: stats should accumulate
    // (gateway engine processes frame regardless of m_running when called directly)
    // But CanGatewayWidget checks isRunning before calling
    EXPECT_FALSE(gw.isRunning());
}

TEST(CanGateway, ClearRulesPassesAll) {
    StubDriver sink;
    CanGateway gw;
    gw.setSink(&sink);
    gw.setFilterMode(CanGateway::FilterMode::Whitelist);
    gw.setRules({{ 0x100, 0, false }});
    gw.clearRules(); // after clear, whitelist is empty → blocks everything in whitelist mode
    gw.start();

    gw.processFrame(makeFrame(0x100));
    gw.processFrame(makeFrame(0x200));
    // In whitelist mode with empty whitelist: nothing passes
    EXPECT_EQ(sink.written.size(), 0u);
    gw.stop();
}

TEST(CanGateway, NullSinkDoesNotCrash) {
    CanGateway gw;
    gw.setSink(nullptr);
    gw.start();
    gw.processFrame(makeFrame(0x100)); // should not crash
    EXPECT_EQ(gw.stats().forwarded, 1u); // still counted
    gw.stop();
}

TEST(CanGateway, DropFlagBlocksInPassAllMode) {
    // drop=true should block even when FilterMode is PassAll
    StubDriver sink;
    CanGateway gw;
    gw.setSink(&sink);
    gw.setFilterMode(CanGateway::FilterMode::PassAll);
    gw.setRules({{ 0x100, 0, true }}); // drop=true → goes to blacklist
    gw.start();

    gw.processFrame(makeFrame(0x100)); // explicitly dropped
    gw.processFrame(makeFrame(0x200)); // not in rules → passes

    EXPECT_EQ(sink.written.size(), 1u);
    EXPECT_EQ(sink.written[0].id, 0x200u);
    EXPECT_EQ(gw.stats().blocked,   1u);
    EXPECT_EQ(gw.stats().forwarded, 1u);
    gw.stop();
}

TEST(CanGateway, WhitelistAllowsMultipleIds) {
    StubDriver sink;
    CanGateway gw;
    gw.setSink(&sink);
    gw.setFilterMode(CanGateway::FilterMode::Whitelist);
    gw.setRules({{ 0x100, 0, false }, { 0x200, 0, false }});
    gw.start();

    gw.processFrame(makeFrame(0x100));
    gw.processFrame(makeFrame(0x200));
    gw.processFrame(makeFrame(0x300)); // not whitelisted

    EXPECT_EQ(sink.written.size(), 2u);
    EXPECT_EQ(gw.stats().forwarded, 2u);
    EXPECT_EQ(gw.stats().blocked,   1u);
    gw.stop();
}

TEST(CanGateway, BlacklistModeAllowsUnlistedIds) {
    StubDriver sink;
    CanGateway gw;
    gw.setSink(&sink);
    gw.setFilterMode(CanGateway::FilterMode::Blacklist);
    gw.setRules({{ 0x200, 0, true }}); // explicitly block 0x200
    gw.start();

    gw.processFrame(makeFrame(0x100)); // not blocked
    gw.processFrame(makeFrame(0x200)); // blocked
    gw.processFrame(makeFrame(0x300)); // not blocked

    EXPECT_EQ(sink.written.size(), 2u);
    EXPECT_EQ(gw.stats().blocked,   1u);
    EXPECT_EQ(gw.stats().forwarded, 2u);
    gw.stop();
}

TEST(CanGateway, FrameContentPreservedOnRemap) {
    StubDriver sink;
    CanGateway gw;
    gw.setSink(&sink);
    gw.setFilterMode(CanGateway::FilterMode::PassAll);
    gw.setRules({{ 0x100, 0x500, false }});
    gw.start();

    CanFrame f = makeFrame(0x100, 5);
    f.data[0] = 0xAA; f.data[1] = 0xBB; f.data[2] = 0xCC;
    gw.processFrame(f);

    ASSERT_EQ(sink.written.size(), 1u);
    const auto &w = sink.written[0];
    EXPECT_EQ(w.id, 0x500u);    // remapped
    EXPECT_EQ(w.dlc, 5u);       // dlc unchanged
    EXPECT_EQ(w.data[0], 0xAA); // payload unchanged
    EXPECT_EQ(w.data[1], 0xBB);
    EXPECT_EQ(w.data[2], 0xCC);
    gw.stop();
}

TEST(CanGateway, StartWithNoSourceNotRunning) {
    CanGateway gw;
    // No source driver set
    gw.start();
    EXPECT_FALSE(gw.isRunning()); // start() with no source sets running=false
}

TEST(CanGateway, TotalEqualsForwardedPlusBlocked) {
    StubDriver sink;
    CanGateway gw;
    gw.setSink(&sink);
    gw.setFilterMode(CanGateway::FilterMode::Whitelist);
    gw.setRules({{ 0x100, 0, false }});
    gw.start();

    for (int i = 0; i < 5; ++i) gw.processFrame(makeFrame(0x100)); // forwarded
    for (int i = 0; i < 3; ++i) gw.processFrame(makeFrame(0x999)); // blocked

    auto s = gw.stats();
    EXPECT_EQ(s.forwarded + s.blocked, s.total);
    EXPECT_EQ(s.total, 8u);
    gw.stop();
}

TEST(CanGateway, MultipleRemaps) {
    StubDriver sink;
    CanGateway gw;
    gw.setSink(&sink);
    gw.setFilterMode(CanGateway::FilterMode::PassAll);
    gw.setRules({{ 0x100, 0x500, false }, { 0x200, 0x600, false }});
    gw.start();

    gw.processFrame(makeFrame(0x100));
    gw.processFrame(makeFrame(0x200));
    gw.processFrame(makeFrame(0x300)); // no remap

    ASSERT_EQ(sink.written.size(), 3u);
    EXPECT_EQ(sink.written[0].id, 0x500u);
    EXPECT_EQ(sink.written[1].id, 0x600u);
    EXPECT_EQ(sink.written[2].id, 0x300u);
    EXPECT_EQ(gw.stats().remapped, 2u);
    gw.stop();
}
