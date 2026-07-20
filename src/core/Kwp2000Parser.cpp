#include "Kwp2000Parser.h"
#include <numeric>
#include <format>

Kwp2000Frame Kwp2000Parser::parse(const std::vector<uint8_t> &bytes) {
    return parse(bytes.data(), static_cast<int>(bytes.size()));
}

Kwp2000Frame Kwp2000Parser::parse(const uint8_t *data, int len) {
    Kwp2000Frame f;
    if (len < 1) return f;

    if (data[0] == Kwp2000Sid::NegativeResponse) {
        // Format: 7F SID NRC
        f.isResponse = true;
        f.isNegative = true;
        f.sid = (len >= 2) ? data[1] : 0;
        f.nrc = (len >= 3) ? data[2] : 0;
        for (int i = 3; i < len; ++i) f.payload.push_back(data[i]);
    } else if (data[0] & 0x40) {
        // Positive response: SID | 0x40
        f.isResponse = true;
        f.isPositive = true;
        f.sid = data[0] & ~0x40u;
        for (int i = 1; i < len; ++i) f.payload.push_back(data[i]);
    } else {
        // Request
        f.isResponse = false;
        f.sid = data[0];
        for (int i = 1; i < len; ++i) f.payload.push_back(data[i]);
    }

    // LRC = XOR of all bytes
    f.lrc = computeLrc(data, len);
    // LRC is valid when XOR of all bytes including appended checksum byte == 0.
    // KWP2000 over UART appends a checksum; over CAN it's typically omitted.
    // We store the computed LRC rather than validate it here.
    f.checksumValid = (f.lrc == 0); // valid only if caller appended checksum byte

    return f;
}

std::string Kwp2000Parser::serviceName(uint8_t sid) {
    switch (sid) {
        case Kwp2000Sid::StartDiagSession:      return "StartDiagnosticSession";
        case Kwp2000Sid::StopDiagSession:       return "StopDiagnosticSession";
        case Kwp2000Sid::EcuReset:              return "ECUReset";
        case Kwp2000Sid::ReadDataByLocalId:     return "ReadDataByLocalIdentifier";
        case Kwp2000Sid::ReadDataByIdentifier:  return "ReadDataByIdentifier";
        case Kwp2000Sid::ReadEcuIdent:          return "ReadEcuIdentification";
        case Kwp2000Sid::ReadFreezeFrame:       return "ReadFreezeFrameData";
        case Kwp2000Sid::ReadDtcByStatus:       return "ReadDTCByStatus";
        case Kwp2000Sid::ClearDtc:              return "ClearDiagnosticInformation";
        case Kwp2000Sid::WriteDataByLocalId:    return "WriteDataByLocalIdentifier";
        case Kwp2000Sid::InputOutputControl:    return "InputOutputControlByLocalId";
        case Kwp2000Sid::StartRoutine:          return "StartRoutineByLocalId";
        case Kwp2000Sid::StopRoutine:           return "StopRoutineByLocalId";
        case Kwp2000Sid::RequestRoutineResults: return "RequestRoutineResultsByLocalId";
        case Kwp2000Sid::TesterPresent:         return "TesterPresent";
        case Kwp2000Sid::SecurityAccess:        return "SecurityAccess";
        case Kwp2000Sid::NegativeResponse:      return "NegativeResponse";
        default: {
            return std::format("Unknown(0x{:02X})", sid);
        }
    }
}

std::string Kwp2000Parser::nrcDescription(uint8_t nrc) {
    switch (nrc) {
        case Kwp2000Nrc::GeneralReject:                return "General reject";
        case Kwp2000Nrc::ServiceNotSupported:          return "Service not supported";
        case Kwp2000Nrc::SubFuncNotSupported:          return "Sub-function not supported";
        case Kwp2000Nrc::BusyRepeatRequest:            return "Busy, repeat request";
        case Kwp2000Nrc::ConditionsNotCorrect:         return "Conditions not correct";
        case Kwp2000Nrc::RequestSequenceError:         return "Request sequence error";
        case Kwp2000Nrc::RequestOutOfRange:            return "Request out of range";
        case Kwp2000Nrc::SecurityAccessDenied:         return "Security access denied";
        case Kwp2000Nrc::InvalidKey:                   return "Invalid key";
        case Kwp2000Nrc::ExceededAttempts:             return "Exceeded number of attempts";
        case Kwp2000Nrc::UploadDownloadNotAccepted:    return "Upload/download not accepted";
        case Kwp2000Nrc::TransferDataSuspended:        return "Transfer data suspended";
        case Kwp2000Nrc::ResponsePending:              return "Response pending";
        case Kwp2000Nrc::ServiceNotSupportedInSession: return "Service not supported in active session";
        default: {
            return std::format("Unknown NRC (0x{:02X})", nrc);
        }
    }
}

uint8_t Kwp2000Parser::computeLrc(const uint8_t *data, int len) {
    uint8_t lrc = 0;
    for (int i = 0; i < len; ++i) lrc ^= data[i];
    return lrc;
}

bool Kwp2000Parser::isRequestSid(uint8_t sid) {
    return sid != Kwp2000Sid::NegativeResponse && !(sid & 0x40);
}
