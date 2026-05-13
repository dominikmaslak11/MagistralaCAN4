#include "SocketCanDriver.h"

#ifdef __linux__
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstring>
#include <QDir>
#include <QDebug>

static constexpr int CAN_SNIFFER_MTU = CANXL_MTU;

SocketCanDriver::SocketCanDriver() = default;
SocketCanDriver::~SocketCanDriver() { close(); }

bool SocketCanDriver::open(const QString &device) {
    m_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (m_socket < 0) {
        qWarning() << "SocketCAN: socket() nie powiódł się:" << strerror(errno);
        return false;
    }
    int enable = 1;
    setsockopt(m_socket, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable, sizeof(enable));
    if (setsockopt(m_socket, SOL_CAN_RAW, CAN_RAW_XL_FRAMES, &enable, sizeof(enable)) < 0)
        qDebug() << "SocketCAN: CAN XL not supported (ignoring)";

    struct ifreq ifr;
    std::strncpy(ifr.ifr_name, device.toStdString().c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    if (ioctl(m_socket, SIOCGIFINDEX, &ifr) < 0) {
        qWarning() << "SocketCAN: nie znaleziono interfejsu:" << device;
        close(); return false;
    }
    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(m_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        qWarning() << "SocketCAN: bind() nie powiódł się:" << strerror(errno);
        close(); return false;
    }
    qDebug() << "SocketCAN: otwarto" << device;
    return true;
}

void SocketCanDriver::close() {
    if (m_socket >= 0) { ::close(m_socket); m_socket = -1; }
}

bool SocketCanDriver::isValid() const { return m_socket >= 0; }

CanFrame SocketCanDriver::readFrame() {
    if (m_socket < 0) return {};

    uint8_t buf[CAN_SNIFFER_MTU];
    ssize_t nbytes = ::read(m_socket, buf, sizeof(buf));
    if (nbytes < 0) return {};

    uint64_t ts = []() -> uint64_t {
        struct timeval tv; gettimeofday(&tv, nullptr);
        return static_cast<uint64_t>(tv.tv_sec) * 1000000ULL + tv.tv_usec;
    }();

    CanFrame frame;
    if (nbytes == CAN_MTU) {
        auto *cf = (struct can_frame *)buf;
        frame = parseFrame(*cf, ts);
    } else if (nbytes == CANFD_MTU) {
        auto *fdf = (struct canfd_frame *)buf;
        frame.id = fdf->can_id & CAN_EFF_MASK;
        frame.extended = fdf->can_id & CAN_EFF_FLAG;
        frame.rtr = false;
        frame.error = fdf->can_id & CAN_ERR_FLAG;
        frame.fd = true;
        frame.dlc = fdf->len > 64 ? 64 : fdf->len;
        for (int i = 0; i < frame.dlc; ++i) frame.data[i] = fdf->data[i];
        frame.timestamp = ts;
    } else if (nbytes >= CANXL_MIN_MTU) {
        auto *xlf = (struct canxl_frame *)buf;
        frame.id = xlf->prio & CANXL_PRIO_MASK;
        frame.extended = true;
        frame.xl = true;
        frame.sdt = xlf->sdt;
        frame.af = xlf->af;
        frame.dlc = xlf->len > CANXL_MAX_DLC ? CANXL_MAX_DLC : xlf->len;
        int xlN = std::min((int)frame.dlc, 64);
        for (int i = 0; i < xlN; ++i) frame.data[i] = xlf->data[i];
        frame.timestamp = ts;
    }
    return frame;
}

void SocketCanDriver::writeFrame(const CanFrame &frame) {
    if (m_socket < 0) return;
    if (frame.xl) {
        struct canxl_frame xlf;
        memset(&xlf, 0, sizeof(xlf));
        xlf.prio = frame.id & CANXL_PRIO_MASK;
        xlf.sdt = frame.sdt;
        xlf.af = frame.af;
        xlf.len = frame.dlc > CANXL_MAX_DLC ? CANXL_MAX_DLC : frame.dlc;
        int xlWrN = std::min((int)xlf.len, 64);
        for (int i = 0; i < xlWrN; ++i) xlf.data[i] = frame.data[i];
        write(m_socket, &xlf, CANXL_HDR_SIZE + xlf.len);
    } else if (frame.fd) {
        struct canfd_frame fdf;
        memset(&fdf, 0, sizeof(fdf));
        fdf.can_id = frame.id;
        if (frame.extended) fdf.can_id |= CAN_EFF_FLAG;
        fdf.flags |= CANFD_BRS;
        fdf.len = frame.dlc > 64 ? 64 : frame.dlc;
        for (int i = 0; i < fdf.len; ++i) fdf.data[i] = frame.data[i];
        write(m_socket, &fdf, CANFD_MTU);
    } else {
        struct can_frame raw;
        memset(&raw, 0, sizeof(raw));
        raw.can_id = frame.id;
        if (frame.extended) raw.can_id |= CAN_EFF_FLAG;
        if (frame.rtr)      raw.can_id |= CAN_RTR_FLAG;
        if (frame.error)    raw.can_id |= CAN_ERR_FLAG;
        raw.len = frame.dlc;
        for (int i = 0; i < frame.dlc && i < 8; ++i) raw.data[i] = frame.data[i];
        write(m_socket, &raw, sizeof(raw));
    }
}

CanFrame SocketCanDriver::parseFrame(const struct can_frame &raw, uint64_t timestamp) const {
    CanFrame f;
    f.id = raw.can_id & CAN_EFF_MASK;
    f.extended = raw.can_id & CAN_EFF_FLAG;
    f.rtr = raw.can_id & CAN_RTR_FLAG;
    f.error = raw.can_id & CAN_ERR_FLAG;
    f.dlc = raw.len;
    for (int i = 0; i < raw.len && i < 8; ++i) f.data[i] = raw.data[i];
    f.timestamp = timestamp;
    return f;
}

QStringList SocketCanDriver::availableDevices() const {
    QDir net("/sys/class/net");
    QStringList ifaces;
    for (const auto &entry : net.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (entry.startsWith("can") || entry.startsWith("vcan")) ifaces.append(entry);
    return ifaces;
}

#else
//  Stub dla nie-Linux
SocketCanDriver::SocketCanDriver() {}
SocketCanDriver::~SocketCanDriver() {}
bool SocketCanDriver::open(const QString &) { return false; }
void SocketCanDriver::close() {}
CanFrame SocketCanDriver::readFrame() { return {}; }
bool SocketCanDriver::isValid() const { return false; }
void SocketCanDriver::writeFrame(const CanFrame &) {}
QStringList SocketCanDriver::availableDevices() const { return {}; }
#endif
