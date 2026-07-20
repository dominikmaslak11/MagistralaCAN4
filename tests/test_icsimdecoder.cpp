#include <gtest/gtest.h>
#include "core/IcSimDecoder.h"
#include <cmath>

static CanFrame makeFrame(uint32_t id, std::initializer_list<uint8_t> bytes)
{
    CanFrame f;
    f.id  = id;
    f.dlc = static_cast<uint8_t>(bytes.size());
    int i = 0;
    for (uint8_t b : bytes) f.data[i++] = b;
    return f;
}

TEST(IcSimDecoder, DecodeSpeedZero) {
    CanFrame f = makeFrame(0x244, {0,0,0,0,0});
    IcSimState st;
    EXPECT_TRUE(IcSimDecoder::decode(f, st));
    EXPECT_NEAR(st.speedKph, 0.f, 0.1f);
    EXPECT_NEAR(st.speedMph, 0.f, 0.1f);
}

TEST(IcSimDecoder, DecodeSpeed50Kph) {
    CanFrame f = makeFrame(0x244, {0,0,0,0x13,0x88}); // 5000/100 = 50 kph
    IcSimState st;
    EXPECT_TRUE(IcSimDecoder::decode(f, st));
    EXPECT_NEAR(st.speedKph, 50.f, 0.1f);
    EXPECT_NEAR(st.speedMph, 50.f * 0.6213751f, 0.2f);
}

TEST(IcSimDecoder, DecodeSpeedMaxMph) {
    CanFrame f = makeFrame(0x244, {0,0,0,0x38,0x94}); // 0x3894 = 14484 → ~144.84 kph → ~90 mph
    IcSimState st;
    EXPECT_TRUE(IcSimDecoder::decode(f, st));
    EXPECT_NEAR(st.speedMph, 90.f, 1.0f);
}

TEST(IcSimDecoder, DecodeSpeedDlcTooShort) {
    CanFrame f = makeFrame(0x244, {0,0,0,0x13}); // dlc=4, need >=5
    IcSimState st;
    EXPECT_FALSE(IcSimDecoder::decode(f, st));
}

TEST(IcSimDecoder, DecodeDoorsAllLocked) {
    CanFrame f = makeFrame(0x19B, {0,0,0x0F});
    IcSimState st;
    EXPECT_TRUE(IcSimDecoder::decode(f, st));
    EXPECT_EQ(st.doorMask, 0x0F);
    for (int i = 0; i < 4; ++i) EXPECT_TRUE(st.isDoorLocked(i));
}

TEST(IcSimDecoder, DecodeDoorsAllUnlocked) {
    CanFrame f = makeFrame(0x19B, {0,0,0x00});
    IcSimState st;
    EXPECT_TRUE(IcSimDecoder::decode(f, st));
    EXPECT_EQ(st.doorMask, 0x00);
    for (int i = 0; i < 4; ++i) EXPECT_FALSE(st.isDoorLocked(i));
}

TEST(IcSimDecoder, DecodeDoor1OnlyUnlocked) {
    CanFrame f = makeFrame(0x19B, {0,0,0x0E}); // bit0=0 → door1 unlocked
    IcSimState st;
    EXPECT_TRUE(IcSimDecoder::decode(f, st));
    EXPECT_FALSE(st.isDoorLocked(0));
    EXPECT_TRUE(st.isDoorLocked(1));
    EXPECT_TRUE(st.isDoorLocked(2));
    EXPECT_TRUE(st.isDoorLocked(3));
}

TEST(IcSimDecoder, DecodeDoorDlcTooShort) {
    CanFrame f = makeFrame(0x19B, {0,0}); // dlc=2, need >=3
    IcSimState st;
    EXPECT_FALSE(IcSimDecoder::decode(f, st));
}

TEST(IcSimDecoder, DecodeSignalOff) {
    CanFrame f = makeFrame(0x188, {0x00});
    IcSimState st;
    EXPECT_TRUE(IcSimDecoder::decode(f, st));
    EXPECT_FALSE(st.leftSignal);
    EXPECT_FALSE(st.rightSignal);
}

TEST(IcSimDecoder, DecodeSignalLeft) {
    CanFrame f = makeFrame(0x188, {0x01});
    IcSimState st;
    EXPECT_TRUE(IcSimDecoder::decode(f, st));
    EXPECT_TRUE(st.leftSignal);
    EXPECT_FALSE(st.rightSignal);
}

TEST(IcSimDecoder, DecodeSignalRight) {
    CanFrame f = makeFrame(0x188, {0x02});
    IcSimState st;
    EXPECT_TRUE(IcSimDecoder::decode(f, st));
    EXPECT_FALSE(st.leftSignal);
    EXPECT_TRUE(st.rightSignal);
}

TEST(IcSimDecoder, DecodeSignalBoth) {
    CanFrame f = makeFrame(0x188, {0x03});
    IcSimState st;
    EXPECT_TRUE(IcSimDecoder::decode(f, st));
    EXPECT_TRUE(st.leftSignal);
    EXPECT_TRUE(st.rightSignal);
}

TEST(IcSimDecoder, DecodeUnknownId) {
    CanFrame f = makeFrame(0x100, {0xFF,0xFF,0xFF,0xFF,0xFF});
    IcSimState st;
    EXPECT_FALSE(IcSimDecoder::decode(f, st));
}

TEST(IcSimDecoder, DecodeCustomSpeedId) {
    CanFrame f = makeFrame(0x300, {0,0,0,0x13,0x88});
    IcSimState st;
    EXPECT_FALSE(IcSimDecoder::decode(f, st));
    EXPECT_TRUE(IcSimDecoder::decode(f, st, 0x300, 0x19B, 0x188));
    EXPECT_NEAR(st.speedKph, 50.f, 0.1f);
}

TEST(IcSimDecoder, BuildSpeedFrameStructure) {
    CanFrame f = IcSimDecoder::buildSpeedFrame(60.f);
    EXPECT_EQ(f.id, IcSimDecoder::SPEED_ID);
    EXPECT_EQ(f.dlc, 5);
    int raw = (f.data[3] << 8) | f.data[4];
    float mph = (raw / 100.f) * 0.6213751f;
    EXPECT_NEAR(mph, 60.f, 1.0f);
}

TEST(IcSimDecoder, BuildSpeedFrameNegativeClampedToZero) {
    CanFrame f = IcSimDecoder::buildSpeedFrame(-10.f);
    int raw = (f.data[3] << 8) | f.data[4];
    EXPECT_EQ(raw, 0);
}

TEST(IcSimDecoder, BuildDoorFrameAllLocked) {
    CanFrame f = IcSimDecoder::buildDoorFrame(0x0F);
    EXPECT_EQ(f.id, IcSimDecoder::DOOR_ID);
    EXPECT_EQ(f.dlc, 3);
    EXPECT_EQ(f.data[2], 0x0F);
}

TEST(IcSimDecoder, BuildDoorFrameAllUnlocked) {
    CanFrame f = IcSimDecoder::buildDoorFrame(0x00);
    EXPECT_EQ(f.data[2], 0x00);
}

TEST(IcSimDecoder, BuildDoorFrameHighBitsIgnored) {
    CanFrame f = IcSimDecoder::buildDoorFrame(0xFF);
    EXPECT_EQ(f.data[2], 0x0F);
}

TEST(IcSimDecoder, BuildSignalFrameOff) {
    CanFrame f = IcSimDecoder::buildSignalFrame(false, false);
    EXPECT_EQ(f.id, IcSimDecoder::SIGNAL_ID);
    EXPECT_EQ(f.dlc, 1);
    EXPECT_EQ(f.data[0], 0x00);
}

TEST(IcSimDecoder, BuildSignalFrameLeft) {
    CanFrame f = IcSimDecoder::buildSignalFrame(true, false);
    EXPECT_EQ(f.data[0], 0x01);
}

TEST(IcSimDecoder, BuildSignalFrameRight) {
    CanFrame f = IcSimDecoder::buildSignalFrame(false, true);
    EXPECT_EQ(f.data[0], 0x02);
}

TEST(IcSimDecoder, BuildSignalFrameBoth) {
    CanFrame f = IcSimDecoder::buildSignalFrame(true, true);
    EXPECT_EQ(f.data[0], 0x03);
}

TEST(IcSimDecoder, RoundTripSpeed) {
    for (float mph : {0.f, 30.f, 60.f, 90.f}) {
        CanFrame f = IcSimDecoder::buildSpeedFrame(mph);
        IcSimState st;
        ASSERT_TRUE(IcSimDecoder::decode(f, st));
        EXPECT_NEAR(st.speedMph, mph, 1.0f) << "mph=" << mph;
    }
}

TEST(IcSimDecoder, RoundTripDoor) {
    for (uint8_t mask : {0x00, 0x0F, 0x05, 0x0A, 0x03}) {
        CanFrame f = IcSimDecoder::buildDoorFrame(mask);
        IcSimState st;
        ASSERT_TRUE(IcSimDecoder::decode(f, st));
        EXPECT_EQ(st.doorMask, mask) << "mask=" << (int)mask;
    }
}

TEST(IcSimDecoder, RoundTripSignal) {
    for (auto [l, r] : std::initializer_list<std::pair<bool,bool>>{{false,false},{true,false},{false,true},{true,true}}) {
        CanFrame f = IcSimDecoder::buildSignalFrame(l, r);
        IcSimState st;
        ASSERT_TRUE(IcSimDecoder::decode(f, st));
        EXPECT_EQ(st.leftSignal,  l);
        EXPECT_EQ(st.rightSignal, r);
    }
}

TEST(IcSimDecoder, IcSimStateDefaultAllLocked) {
    IcSimState st;
    for (int i = 0; i < 4; ++i) EXPECT_TRUE(st.isDoorLocked(i));
}

TEST(IcSimDecoder, IcSimStateIsDoorLockedBits) {
    IcSimState st;
    st.doorMask = 0b0101;
    EXPECT_TRUE(st.isDoorLocked(0));
    EXPECT_FALSE(st.isDoorLocked(1));
    EXPECT_TRUE(st.isDoorLocked(2));
    EXPECT_FALSE(st.isDoorLocked(3));
}
