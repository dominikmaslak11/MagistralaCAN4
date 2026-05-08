#include "CanSniffer.h"
#include <QDebug>
#include <QtConcurrent>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <cstring>

CanSniffer::CanSniffer(QObject *parent) : QObject(parent) {}

CanSniffer::~CanSniffer() {
    if (m_running) stop();
}

bool CanSniffer::isSocketValid() const {
    return m_socket >= 0;
}

void CanSniffer::writeFrame(const CanFrame &frame) {
    if (m_socket < 0) {
        emit errorOccurred("writeFrame: socket nie jest otwarty");
        return;
    }

    if (frame.xl) {
        struct canxl_frame xlf;
        memset(&xlf, 0, sizeof(xlf));
        xlf.prio = frame.id & CANXL_PRIO_MASK;
        xlf.flags = 0;
        xlf.sdt = frame.sdt;
        xlf.af = frame.af;
        xlf.len = frame.dlc > CANXL_MAX_DLC ? CANXL_MAX_DLC : frame.dlc;
        for (int i = 0; i < xlf.len && i < 2048; ++i)
            xlf.data[i] = frame.data[i];
        ssize_t n = write(m_socket, &xlf, CANXL_HDR_SIZE + xlf.len);
        if (n != (ssize_t)(CANXL_HDR_SIZE + xlf.len))
            emit errorOccurred(QString("writeFrame XL: zapis się nie powiódł (%1)").arg(strerror(errno)));
    } else if (frame.fd) {
        struct canfd_frame fdf;
        memset(&fdf, 0, sizeof(fdf));
        fdf.can_id = frame.id;
        if (frame.extended) fdf.can_id |= CAN_EFF_FLAG;
        fdf.flags |= CANFD_BRS;
        fdf.len = frame.dlc > 64 ? 64 : frame.dlc;
        for (int i = 0; i < fdf.len; ++i) fdf.data[i] = frame.data[i];
        ssize_t n = write(m_socket, &fdf, CANFD_MTU);
        if (n != CANFD_MTU)
            emit errorOccurred(QString("writeFrame FD: zapis się nie powiódł (%1)").arg(strerror(errno)));
    } else {
        struct can_frame raw;
        memset(&raw, 0, sizeof(raw));
        raw.can_id = frame.id;
        if (frame.extended) raw.can_id |= CAN_EFF_FLAG;
        if (frame.rtr)      raw.can_id |= CAN_RTR_FLAG;
        if (frame.error)    raw.can_id |= CAN_ERR_FLAG;
        raw.len = frame.dlc;
        for (int i = 0; i < frame.dlc && i < 8; ++i)
            raw.data[i] = frame.data[i];
        ssize_t n = write(m_socket, &raw, sizeof(raw));
        if (n != sizeof(raw))
            emit errorOccurred(QString("writeFrame: zapis się nie powiódł (%1)").arg(strerror(errno)));
    }
}

void CanSniffer::start(const QString &interface) {
    if (m_running) return;
    if (!openSocket(interface)) {
        emit errorOccurred("Nie można otworzyć interfejsu: " + interface);
        return;
    }
    m_interface = interface;
    m_running = true;
    emit statusChanged(true);
    QtConcurrent::run([this] { doWork(); });
}

void CanSniffer::stop() {
    if (!m_running) return;
    m_running = false;
    closeSocket();
    emit statusChanged(false);
}

void CanSniffer::doWork() {
    while (m_running) {
        uint8_t buf[CAN_SNIFFER_MTU];
        ssize_t nbytes = read(m_socket, buf, sizeof(buf));
        if (nbytes < 0) {
            if (m_running) {
                emit errorOccurred("Błąd odczytu z CAN: " + QString::fromLocal8Bit(strerror(errno)));
            }
            break;
        }
        if (nbytes == 0) continue;

        uint64_t ts = systemTimestamp();
        CanFrame canFrame;
        if (nbytes == CAN_MTU) {
            struct can_frame *frame = (struct can_frame*)buf;
            canFrame = parseFrame(*frame, ts);
        } else if (nbytes == CANFD_MTU) {
            struct canfd_frame *frame = (struct canfd_frame*)buf;
            canFrame.id = frame->can_id & CAN_EFF_MASK;
            canFrame.extended = frame->can_id & CAN_EFF_FLAG;
            canFrame.rtr = false;
            canFrame.error = frame->can_id & CAN_ERR_FLAG;
            canFrame.fd = true;
            canFrame.dlc = frame->len > 64 ? 64 : frame->len;
            for (int i = 0; i < canFrame.dlc; ++i)
                canFrame.data[i] = frame->data[i];
            canFrame.timestamp = ts;
        } else if (nbytes == CANXL_MTU || nbytes >= CANXL_MIN_MTU) {
            // CAN XL frame
            struct canxl_frame *xlf = (struct canxl_frame*)buf;
            canFrame.id = xlf->prio & CANXL_PRIO_MASK;
            canFrame.extended = true;
            canFrame.xl = true;
            canFrame.fd = false;
            canFrame.sdt = xlf->sdt;
            canFrame.af = xlf->af;
            canFrame.dlc = xlf->len > CANXL_MAX_DLC ? CANXL_MAX_DLC : xlf->len;
            for (int i = 0; i < canFrame.dlc && i < 2048; ++i)
                canFrame.data[i] = xlf->data[i];
            canFrame.timestamp = ts;
        } else {
            continue;
        }
        emit newFrame(canFrame);
    }
    closeSocket();
}

bool CanSniffer::openSocket(const QString &ifname) {
    m_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (m_socket < 0) {
        emit errorOccurred("socket() nie powiódł się");
        return false;
    }

    int enable = 1;
    setsockopt(m_socket, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable, sizeof(enable));
    // Włącz CAN XL (jeśli kernel wspiera)
    if (setsockopt(m_socket, SOL_CAN_RAW, CAN_RAW_XL_FRAMES, &enable, sizeof(enable)) < 0) {
        qDebug() << "CAN XL not supported by kernel (ignoring)";
    }

    struct ifreq ifr;
    std::strncpy(ifr.ifr_name, ifname.toStdString().c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    if (ioctl(m_socket, SIOCGIFINDEX, &ifr) < 0) {
        emit errorOccurred("Nie znaleziono interfejsu: " + ifname);
        closeSocket();
        return false;
    }

    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(m_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        emit errorOccurred("bind() nie powiódł się");
        closeSocket();
        return false;
    }

    return true;
}

void CanSniffer::closeSocket() {
    if (m_socket >= 0) {
        ::close(m_socket);
        m_socket = -1;
    }
}

CanFrame CanSniffer::parseFrame(const struct can_frame &rawFrame, uint64_t timestamp) const {
    CanFrame frame;
    frame.id = rawFrame.can_id & CAN_EFF_MASK;
    frame.extended = rawFrame.can_id & CAN_EFF_FLAG;
    frame.rtr = rawFrame.can_id & CAN_RTR_FLAG;
    frame.error = rawFrame.can_id & CAN_ERR_FLAG;
    frame.fd = false;
    frame.dlc = rawFrame.len;
    for (int i = 0; i < rawFrame.len && i < 8; ++i)
        frame.data[i] = rawFrame.data[i];
    frame.timestamp = timestamp;
    return frame;
}

uint64_t CanSniffer::systemTimestamp() const {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000000ULL + tv.tv_usec;
}
