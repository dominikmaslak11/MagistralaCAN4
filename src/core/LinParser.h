#pragma once
#include "LinFrame.h"
#include <vector>
#include <string>

/// LIN bus protocol parser.
/// Parses raw byte streams from UART captures or pre-framed LIN data.
class LinParser {
public:
    /// Parse a pre-framed LIN message:
    ///   bytes[0] = sync (0x55)
    ///   bytes[1] = PID
    ///   bytes[2..2+dlc-1] = data
    ///   bytes[2+dlc] = checksum
    ///
    /// Returns parsed LinFrame; checksumValid is set accordingly.
    static LinFrame parse(const std::vector<uint8_t> &bytes,
                          LinFrame::ChecksumType csType = LinFrame::ChecksumType::Enhanced);

    static LinFrame parse(const uint8_t *data, int len,
                          LinFrame::ChecksumType csType = LinFrame::ChecksumType::Enhanced);

    /// Feed raw UART bytes into the internal reassembly buffer.
    /// Returns complete frames as they're detected.
    std::vector<LinFrame> feed(const std::vector<uint8_t> &bytes,
                               int dlc,
                               LinFrame::ChecksumType csType = LinFrame::ChecksumType::Enhanced);

    /// Heuristic: guess DLC from frame ID (standard assignment):
    ///   ID 0x00–0x1F → 2 bytes
    ///   ID 0x20–0x2F → 4 bytes
    ///   ID 0x30–0x3B → 8 bytes
    ///   ID 0x3C–0x3F → diagnostic frames (8 bytes)
    static int guessDataLength(uint8_t frameId);

    /// Well-known LIN frame names (diagnostic / standard assignments)
    static std::string frameName(uint8_t frameId);

    void reset() { m_buf.clear(); }

private:
    std::vector<uint8_t> m_buf;

    static constexpr uint8_t kSyncByte = 0x55;
    static constexpr int     kMinFrame = 4;    // sync + pid + 1 byte data + checksum
    static constexpr int     kMaxFrame = 11;   // sync + pid + 8 bytes + checksum
};
