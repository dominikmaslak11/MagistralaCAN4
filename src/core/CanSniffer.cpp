#include "CanSniffer.h"
#include <QDebug>
#include <QtConcurrent>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <cstring>
#include <linux/can/raw.h>

CanSniffer::CanSniffer(QObject *parent) : QObject(parent) {}

CanSniffer::~CanSniffer() {
    if (m_running) stop();
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
        // Bufor na maksymalną ramkę CAN FD (72 bajty nagłówek + 64 dane)
        uint8_t buf[CANFD_MTU];
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
            // Klasyczna ramka
            struct can_frame *frame = (struct can_frame*)buf;
            canFrame = parseFrame(*frame, ts);
        } else if (nbytes == CANFD_MTU) {
            // CAN FD
            struct canfd_frame *frame = (struct canfd_frame*)buf;
            canFrame.id = frame->can_id & CAN_EFF_MASK;
            canFrame.extended = frame->can_id & CAN_EFF_FLAG;
            canFrame.rtr = false; // FD ramki nie mają RTR
            canFrame.error = frame->can_id & CAN_ERR_FLAG;
            canFrame.fd = true;
            canFrame.dlc = frame->len > 64 ? 64 : frame->len;
            for (int i = 0; i < canFrame.dlc; ++i)
                canFrame.data[i] = frame->data[i];
            canFrame.timestamp = ts;
        } else {
            continue; // nieznany rozmiar
        }
        emit newFrame(canFrame);
    }
    closeSocket();
}

bool CanSniffer::openSocket(const QString &ifname) {
    // Próba otwarcia z CAN_RAW i FD
    m_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (m_socket < 0) {
        emit errorOccurred("socket() nie powiódł się");
        return false;
    }

    // Włącz obsługę CAN FD
    int enable = 1;
    if (setsockopt(m_socket, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable, sizeof(enable)) < 0) {
        // Jeśli nie udało się, to tylko klasyczne ramki – nie krytyczne
        qDebug("CAN FD nie jest obsługiwane przez sterownik, tylko CAN 2.0");
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
