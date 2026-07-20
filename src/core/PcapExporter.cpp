#include "PcapExporter.h"
#include <QFile>
#include <QDataStream>

// PCAP global header constants
static constexpr uint32_t PCAP_MAGIC_LE     = 0xa1b2c3d4; // little-endian, µs precision
static constexpr uint16_t PCAP_MAJOR        = 2;
static constexpr uint16_t PCAP_MINOR        = 4;
static constexpr uint32_t PCAP_SNAPLEN      = 65535;
static constexpr uint32_t PCAP_LINKTYPE_CAN = 227; // LINKTYPE_CAN_SOCKETCAN

// socketcan frame layout written to PCAP payload
// struct can_frame { uint32_t can_id; uint8_t len; uint8_t pad[3]; uint8_t data[8]; }
// Total = 16 bytes

namespace {
void writeU32LE(QDataStream &ds, uint32_t v) { ds.writeRawData(reinterpret_cast<const char*>(&v), 4); }
void writeU16LE(QDataStream &ds, uint16_t v) { ds.writeRawData(reinterpret_cast<const char*>(&v), 2); }
} // namespace

bool PcapExporter::exportFrames(const QString &path,
                                const std::vector<CanFrame> &frames,
                                const std::vector<uint64_t> &timestampsUs) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);

    // Global header
    writeU32LE(ds, PCAP_MAGIC_LE);
    writeU16LE(ds, PCAP_MAJOR);
    writeU16LE(ds, PCAP_MINOR);
    writeU32LE(ds, 0);            // thiszone (UTC)
    writeU32LE(ds, 0);            // sigfigs
    writeU32LE(ds, PCAP_SNAPLEN);
    writeU32LE(ds, PCAP_LINKTYPE_CAN);

    for (size_t i = 0; i < frames.size(); ++i) {
        const CanFrame &frame = frames[i];
        uint64_t tsUs = (!timestampsUs.empty() && i < timestampsUs.size())
                        ? timestampsUs[i]
                        : i * 1000ULL; // synthetic 1ms spacing

        uint32_t tsSec  = static_cast<uint32_t>(tsUs / 1'000'000ULL);
        uint32_t tsUsec = static_cast<uint32_t>(tsUs % 1'000'000ULL);

        // Build socketcan payload (16 bytes)
        uint32_t canId = frame.id;
        if (frame.extended) canId |= 0x80000000u; // EFF flag
        if (frame.fd)       canId |= 0x40000000u; // FD flag (non-standard, informational)
        uint8_t  len    = std::min(frame.dlc, uint8_t(8));

        uint8_t payload[16] = {};
        payload[0] = static_cast<uint8_t>(canId & 0xFF);
        payload[1] = static_cast<uint8_t>((canId >> 8)  & 0xFF);
        payload[2] = static_cast<uint8_t>((canId >> 16) & 0xFF);
        payload[3] = static_cast<uint8_t>((canId >> 24) & 0xFF);
        payload[4] = len;
        // payload[5..7] = pad (0x00)
        for (int b = 0; b < len; ++b)
            payload[8 + b] = frame.data[b];

        // Packet header
        writeU32LE(ds, tsSec);
        writeU32LE(ds, tsUsec);
        writeU32LE(ds, 16); // incl_len
        writeU32LE(ds, 16); // orig_len

        ds.writeRawData(reinterpret_cast<const char*>(payload), 16);
    }

    f.flush();
    return true;
}

bool PcapExporter::writeHeader(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    QDataStream ds(&f);
    writeU32LE(ds, PCAP_MAGIC_LE);
    writeU16LE(ds, PCAP_MAJOR);
    writeU16LE(ds, PCAP_MINOR);
    writeU32LE(ds, 0);            // thiszone
    writeU32LE(ds, 0);            // sigfigs
    writeU32LE(ds, PCAP_SNAPLEN);
    writeU32LE(ds, PCAP_LINKTYPE_CAN);

    f.flush();
    return true;
}
