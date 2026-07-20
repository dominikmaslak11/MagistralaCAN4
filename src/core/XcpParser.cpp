#include "XcpParser.h"
#include <format>

XcpFrame XcpParser::parse(const std::vector<uint8_t> &bytes) {
    return parse(bytes.data(), static_cast<int>(bytes.size()));
}

XcpFrame XcpParser::parse(const uint8_t *data, int len) {
    XcpFrame f;
    if (len < 1) return f;

    f.pid = data[0];

    // Response PIDs from slave → master
    if (f.pid == 0xFF) {
        f.isPositive = true;
        f.dir        = XcpFrame::Direction::Response;
    } else if (f.pid == 0xFE) {
        f.isNegative = true;
        f.dir        = XcpFrame::Direction::Response;
        f.errorCode  = (len >= 2) ? data[1] : 0;
    } else if (f.pid == 0xFD) {
        f.isEvent = true;
        f.dir     = XcpFrame::Direction::Event;
    } else if (f.pid == 0xFC) {
        f.isService = true;
        f.dir       = XcpFrame::Direction::Service;
    } else {
        f.dir = XcpFrame::Direction::Command;
    }

    for (int i = 1; i < len; ++i)
        f.payload.push_back(data[i]);

    return f;
}

std::string XcpParser::commandName(uint8_t pid) {
    switch (pid) {
        // High PIDs: same value used as command on CMD channel and as response marker on RESP channel
        case XcpCmd::Connect:          return "CONNECT / POSITIVE_RESPONSE";
        case XcpCmd::Disconnect:       return "DISCONNECT / NEGATIVE_RESPONSE";
        case XcpCmd::GetStatus:        return "GET_STATUS / EV (Event)";
        case XcpCmd::Synch:            return "SYNCH / SERV (Service)";
        case XcpCmd::GetCommModeInfo:  return "GET_COMM_MODE_INFO";
        case XcpCmd::GetId:            return "GET_ID";
        case XcpCmd::Upload:           return "UPLOAD";
        case XcpCmd::ShortUpload:      return "SHORT_UPLOAD";
        case XcpCmd::SetMta:           return "SET_MTA";
        case XcpCmd::Download:         return "DOWNLOAD";
        case XcpCmd::ShortDownload:    return "SHORT_DOWNLOAD";
        case XcpCmd::BuildChecksum:    return "BUILD_CHECKSUM";
        case XcpCmd::TransportLayer:   return "TRANSPORT_LAYER";
        case XcpCmd::TimeCorrelation:  return "TIME_CORRELATION";
        case XcpCmd::ClearDaqList:     return "CLEAR_DAQ_LIST";
        case XcpCmd::SetDaqPtr:        return "SET_DAQ_PTR";
        case XcpCmd::WriteDaq:         return "WRITE_DAQ";
        case XcpCmd::SetDaqListMode:   return "SET_DAQ_LIST_MODE";
        case XcpCmd::StartStopDaqList: return "START_STOP_DAQ_LIST";
        case XcpCmd::StartStopSynch:   return "START_STOP_SYNCH";
        case XcpCmd::GetDaqClock:      return "GET_DAQ_CLOCK";
        case XcpCmd::ReadDaq:          return "READ_DAQ";
        case XcpCmd::GetDaqListStatus: return "GET_DAQ_LIST_STATUS";
        case XcpCmd::GetDaqListInfo:   return "GET_DAQ_LIST_INFO";
        case XcpCmd::ProgramStart:     return "PROGRAM_START";
        case XcpCmd::ProgramClear:     return "PROGRAM_CLEAR";
        case XcpCmd::Program:          return "PROGRAM";
        default: {
            return std::format("CMD_0x{:02X}", pid);
        }
    }
}

std::string XcpParser::errorDescription(uint8_t errCode) {
    switch (errCode) {
        case XcpErr::CmdSynch:       return "Command processor synchronisation";
        case XcpErr::CmdBusy:        return "Command was not executed (busy)";
        case XcpErr::DaqActive:      return "Command rejected because DAQ is running";
        case XcpErr::PgmActive:      return "Command rejected because PGM is running";
        case XcpErr::CmdUnknown:     return "Unknown command or not implemented";
        case XcpErr::CmdSyntax:      return "Command syntax invalid";
        case XcpErr::OutOfRange:     return "Command syntax valid but parameter(s) out of range";
        case XcpErr::WriteProtected: return "The memory location is write protected";
        case XcpErr::AccessDenied:   return "The memory location is not accessible";
        case XcpErr::AccessLocked:   return "Access denied (seed & key required)";
        case XcpErr::PageNotValid:   return "Selected page is not available";
        case XcpErr::ModeNotValid:   return "Selected page mode is not available";
        case XcpErr::SegmentNotValid:return "Selected segment is not valid";
        case XcpErr::Sequence:       return "Sequence error";
        case XcpErr::DaqConfig:      return "DAQ configuration is not valid";
        case XcpErr::MemoryOverflow: return "Memory overflow error";
        case XcpErr::Generic:        return "Generic error";
        case XcpErr::Verify:         return "The slave internal program verify routine detects an error";
        default: {
            return std::format("Unknown error (0x{:02X})", errCode);
        }
    }
}

bool XcpParser::isCommand(uint8_t pid) {
    // All 0xC0–0xFF are standard XCP command PIDs (same bytes double as response markers
    // on the response channel — direction disambiguates, not the PID alone).
    return pid >= 0xC0;
}
