#pragma once
#include <cstdint>
#include <vector>
#include <string>

/// XCP (Universal Calibration Protocol) over CAN – ASAM MCD-1 XCP
/// Packet Types

struct XcpFrame {
    enum class Direction { Command, Response, Event, Service };

    uint8_t    pid      = 0;    // Packet ID / Command Code / Response Code
    Direction  dir      = Direction::Command;
    std::vector<uint8_t> payload;  // bytes after PID byte

    bool isPositive = false;   // PID == 0xFF (positive response)
    bool isNegative = false;   // PID == 0xFE (negative response)
    bool isEvent    = false;   // PID == 0xFD (event packet)
    bool isService  = false;   // PID == 0xFC (service request)

    uint8_t errorCode = 0;     // valid when isNegative (byte 1 of neg response)
};

/// XCP Command Codes (master → slave)
struct XcpCmd {
    static constexpr uint8_t Connect           = 0xFF;
    static constexpr uint8_t Disconnect        = 0xFE;
    static constexpr uint8_t GetStatus         = 0xFD;
    static constexpr uint8_t Synch             = 0xFC;
    static constexpr uint8_t GetCommModeInfo   = 0xFB;
    static constexpr uint8_t GetId             = 0xFA;
    static constexpr uint8_t Upload            = 0xF5;
    static constexpr uint8_t ShortUpload       = 0xF4;
    static constexpr uint8_t SetMta            = 0xF6;
    static constexpr uint8_t Download          = 0xF0;
    static constexpr uint8_t ShortDownload     = 0xED;
    static constexpr uint8_t BuildChecksum     = 0xF3;
    static constexpr uint8_t TransportLayer    = 0xC0;
    static constexpr uint8_t TimeCorrelation   = 0xC6;
    // DAQ
    static constexpr uint8_t ClearDaqList      = 0xE3;
    static constexpr uint8_t SetDaqPtr         = 0xE2;
    static constexpr uint8_t WriteDaq          = 0xE1;
    static constexpr uint8_t SetDaqListMode    = 0xE0;
    static constexpr uint8_t StartStopDaqList  = 0xDE;
    static constexpr uint8_t StartStopSynch    = 0xDD;
    static constexpr uint8_t GetDaqClock       = 0xDC;
    static constexpr uint8_t ReadDaq           = 0xDB;
    static constexpr uint8_t GetDaqListStatus  = 0xDA;
    static constexpr uint8_t GetDaqListInfo    = 0xD9;
    // PGM
    static constexpr uint8_t ProgramStart      = 0xD2;
    static constexpr uint8_t ProgramClear      = 0xD1;
    static constexpr uint8_t Program           = 0xD0;
};

/// XCP Error Codes (slave → master in negative response)
struct XcpErr {
    static constexpr uint8_t CmdSynch          = 0x00;
    static constexpr uint8_t CmdBusy           = 0x10;
    static constexpr uint8_t DaqActive         = 0x11;
    static constexpr uint8_t PgmActive         = 0x12;
    static constexpr uint8_t CmdUnknown        = 0x20;
    static constexpr uint8_t CmdSyntax         = 0x21;
    static constexpr uint8_t OutOfRange        = 0x22;
    static constexpr uint8_t WriteProtected    = 0x23;
    static constexpr uint8_t AccessDenied      = 0x24;
    static constexpr uint8_t AccessLocked      = 0x25;
    static constexpr uint8_t PageNotValid      = 0x26;
    static constexpr uint8_t ModeNotValid      = 0x27;
    static constexpr uint8_t SegmentNotValid   = 0x28;
    static constexpr uint8_t Sequence          = 0x29;
    static constexpr uint8_t DaqConfig         = 0x2A;
    static constexpr uint8_t MemoryOverflow    = 0x30;
    static constexpr uint8_t Generic           = 0x31;
    static constexpr uint8_t Verify            = 0x32;
};

/// Parses raw XCP-over-CAN data bytes (first byte = PID).
class XcpParser {
public:
    /// Parse an XCP packet from raw CAN data bytes.
    static XcpFrame parse(const std::vector<uint8_t> &bytes);
    static XcpFrame parse(const uint8_t *data, int len);

    /// Human-readable command name for a command PID.
    static std::string commandName(uint8_t pid);

    /// Human-readable error description.
    static std::string errorDescription(uint8_t errCode);

    /// True if PID is a master→slave command (0xC0–0xFF range, not 0xFF/0xFE/0xFD/0xFC response PIDs).
    static bool isCommand(uint8_t pid);
};
