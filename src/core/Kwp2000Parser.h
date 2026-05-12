#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <array>

/// KWP2000 / ISO 14230-3 frame (over CAN via ISO 15765-4 or raw serial)
struct Kwp2000Frame {
    uint8_t  sid  = 0;       // Service ID (request) or SID|0x40 (positive response)
    uint8_t  nrc  = 0;       // Negative Response Code (valid when isNegative)
    std::vector<uint8_t> payload;  // Parameters (after SID / after 0x7F SID NRC)

    bool isResponse  = false;
    bool isPositive  = false;
    bool isNegative  = false;

    bool checksumValid = false;  // LRC (XOR of all bytes) or sum-based
    uint8_t lrc = 0;
};

/// KWP2000 service identifiers
struct Kwp2000Sid {
    static constexpr uint8_t StartDiagSession      = 0x10;
    static constexpr uint8_t StopDiagSession       = 0x20;
    static constexpr uint8_t EcuReset              = 0x11;
    static constexpr uint8_t ReadDataByLocalId     = 0x21;
    static constexpr uint8_t ReadDataByIdentifier  = 0x22;
    static constexpr uint8_t ReadEcuIdent          = 0x1A;
    static constexpr uint8_t ReadFreezeFrame       = 0x12;
    static constexpr uint8_t ReadDtcByStatus       = 0x18;
    static constexpr uint8_t ClearDtc              = 0x14;
    static constexpr uint8_t WriteDataByLocalId    = 0x3B;
    static constexpr uint8_t InputOutputControl    = 0x30;
    static constexpr uint8_t StartRoutine          = 0x31;
    static constexpr uint8_t StopRoutine           = 0x32;
    static constexpr uint8_t RequestRoutineResults = 0x33;
    static constexpr uint8_t TesterPresent         = 0x3E;
    static constexpr uint8_t SecurityAccess        = 0x27;
    static constexpr uint8_t NegativeResponse      = 0x7F;
};

/// KWP2000 Negative Response Codes
struct Kwp2000Nrc {
    static constexpr uint8_t GeneralReject                = 0x10;
    static constexpr uint8_t ServiceNotSupported          = 0x11;
    static constexpr uint8_t SubFuncNotSupported          = 0x12;
    static constexpr uint8_t BusyRepeatRequest            = 0x21;
    static constexpr uint8_t ConditionsNotCorrect         = 0x22;
    static constexpr uint8_t RequestSequenceError         = 0x24;
    static constexpr uint8_t RequestOutOfRange            = 0x31;
    static constexpr uint8_t SecurityAccessDenied         = 0x33;
    static constexpr uint8_t InvalidKey                   = 0x35;
    static constexpr uint8_t ExceededAttempts             = 0x36;
    static constexpr uint8_t UploadDownloadNotAccepted    = 0x70;
    static constexpr uint8_t TransferDataSuspended        = 0x71;
    static constexpr uint8_t ResponsePending              = 0x78;
    static constexpr uint8_t ServiceNotSupportedInSession = 0x80;
};

/// Parser for raw KWP2000 byte payloads.
/// Input: the data bytes of a CAN frame (or raw KWP2000 UART payload after framing).
class Kwp2000Parser {
public:
    /// Parse a raw byte buffer (no framing headers — just SID + parameters).
    /// If bytes[0] == 0x7F this is treated as a negative response.
    static Kwp2000Frame parse(const std::vector<uint8_t> &bytes);
    static Kwp2000Frame parse(const uint8_t *data, int len);

    /// Human-readable service name for a SID (request, not response).
    static std::string serviceName(uint8_t sid);

    /// Human-readable NRC description.
    static std::string nrcDescription(uint8_t nrc);

    /// Compute Longitudinal Redundancy Check (XOR of all bytes).
    static uint8_t computeLrc(const uint8_t *data, int len);

    /// True if byte is a valid request SID (not a response SID and not 0x7F).
    static bool isRequestSid(uint8_t sid);
};
