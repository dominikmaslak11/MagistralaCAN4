#include "LinParser.h"
#include <numeric>
#include <algorithm>

// ── LinFrame static methods ───────────────────────────────────────────────────

uint8_t LinFrame::computePid(uint8_t frameId) {
    uint8_t id = frameId & 0x3F;
    // P0 = ID0 ⊕ ID1 ⊕ ID2 ⊕ ID4
    uint8_t p0 = ((id >> 0) ^ (id >> 1) ^ (id >> 2) ^ (id >> 4)) & 1u;
    // P1 = ¬(ID1 ⊕ ID3 ⊕ ID4 ⊕ ID5)
    uint8_t p1 = (~((id >> 1) ^ (id >> 3) ^ (id >> 4) ^ (id >> 5))) & 1u;
    return id | (p0 << 6) | (p1 << 7);
}

bool LinFrame::pidValid(uint8_t pid) {
    uint8_t frameId = pid & 0x3F;
    return computePid(frameId) == pid;
}

uint8_t LinFrame::computeChecksum(uint8_t pid, const uint8_t *data, int len, ChecksumType type) {
    uint32_t sum = (type == ChecksumType::Enhanced) ? pid : 0;
    for (int i = 0; i < len; ++i) sum += data[i];
    // Carry bit addition to keep within 8 bits
    while (sum > 0xFF) sum = (sum & 0xFF) + (sum >> 8);
    return static_cast<uint8_t>(~sum & 0xFF);
}

// ── LinParser ─────────────────────────────────────────────────────────────────

LinFrame LinParser::parse(const uint8_t *bytes, int len, LinFrame::ChecksumType csType) {
    LinFrame f;
    if (len < kMinFrame) return f;

    // Optional: allow leading sync byte or start directly from PID
    int offset = 0;
    if (bytes[0] == kSyncByte) offset = 1;

    if (len - offset < 3) return f; // pid + at least 1 byte + checksum

    f.pid = bytes[offset];
    f.frameId = f.pid & 0x3F;
    f.csType = csType;

    // DLC = frame length - sync - pid - checksum
    int dlcGuess = guessDataLength(f.frameId);
    // Try to use the remaining bytes
    int available = len - offset - 1 - 1; // -pid -checksum
    f.dlc = static_cast<uint8_t>(std::min({available, dlcGuess, 8}));
    if (f.dlc == 0) f.dlc = static_cast<uint8_t>(std::min(available, 8));

    for (int i = 0; i < f.dlc; ++i)
        f.data[i] = bytes[offset + 1 + i];

    f.checksum = bytes[offset + 1 + f.dlc];

    uint8_t expected = LinFrame::computeChecksum(f.pid, f.data.data(), f.dlc, csType);
    f.checksumValid = (expected == f.checksum);

    // If enhanced didn't match, try classic
    if (!f.checksumValid && csType == LinFrame::ChecksumType::Enhanced) {
        uint8_t classic = LinFrame::computeChecksum(f.pid, f.data.data(), f.dlc,
                                                     LinFrame::ChecksumType::Classic);
        if (classic == f.checksum) {
            f.checksumValid = true;
            f.csType = LinFrame::ChecksumType::Classic;
        }
    }

    return f;
}

LinFrame LinParser::parse(const std::vector<uint8_t> &bytes, LinFrame::ChecksumType csType) {
    return parse(bytes.data(), static_cast<int>(bytes.size()), csType);
}

std::vector<LinFrame> LinParser::feed(const std::vector<uint8_t> &bytes, int dlc,
                                       LinFrame::ChecksumType csType) {
    m_buf.insert(m_buf.end(), bytes.begin(), bytes.end());
    std::vector<LinFrame> out;

    while (m_buf.size() >= static_cast<size_t>(kMinFrame)) {
        // Locate sync byte
        auto it = std::find(m_buf.begin(), m_buf.end(), kSyncByte);
        if (it == m_buf.end()) { m_buf.clear(); break; }
        m_buf.erase(m_buf.begin(), it);

        // Frame: sync(1) + pid(1) + data(dlc) + checksum(1)
        size_t needed = 1 + 1 + static_cast<size_t>(dlc) + 1;
        if (m_buf.size() < needed) break;

        LinFrame f = parse(m_buf.data(), static_cast<int>(needed), csType);
        f.dlc = static_cast<uint8_t>(dlc);
        out.push_back(f);
        m_buf.erase(m_buf.begin(), m_buf.begin() + static_cast<int>(needed));
    }
    return out;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

int LinParser::guessDataLength(uint8_t frameId) {
    uint8_t id = frameId & 0x3F;
    if (id <= 0x1F) return 2;
    if (id <= 0x2F) return 4;
    return 8; // 0x30-0x3F (includes diagnostic frames 0x3C, 0x3D, 0x3E, 0x3F)
}

std::string LinParser::frameName(uint8_t frameId) {
    switch (frameId & 0x3F) {
        case 0x3C: return "MasterReq (Diagnostics)";
        case 0x3D: return "SlaveResp (Diagnostics)";
        case 0x3E: return "Reserved (SAE)";
        case 0x3F: return "Reserved (SAE)";
        default:   return "ID_0x" + [frameId]() {
                       char buf[4]; snprintf(buf, sizeof(buf), "%02X", frameId & 0x3F);
                       return std::string(buf);
                   }();
    }
}
