#include "CanSniffer.h"
#include <QDebug>
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

void CanSniffer::start(const QString &interface) {
    if (m_running) return;
    if (!openSocket(interface)) {
        emit errorOccurred("Nie można otworzyć interfejsu: " + interface);
        return;
    }
    m_interface = interface;
    m_running = true;
    emit statusChanged(true);
    // Uruchom funkcję roboczą w osobnym wątku
    QtConcurrent::run([this] { doWork(); });
}

void CanSniffer::stop() {
    if (!m_running) return;
    m_running = false;
    // Zamknij socket – to przerwie blokujące read()
    closeSocket();
    emit statusChanged(false);
}

void CanSniffer::doWork() {
    // Pętla odczytu ramek
    while (m_running) {
        struct can_frame frame;
        // read blokuje, ale po zamknięciu socketu (przez stop) zwróci błąd
        ssize_t nbytes = read(m_socket, &frame, sizeof(struct can_frame));
        if (nbytes < 0) {
            if (m_running) {   // oczekiwany błąd tylko gdy zatrzymujemy
                emit errorOccurred("Błąd odczytu z CAN: " + QString::fromLocal8Bit(strerror(errno)));
            }
            break;
        }
        if (nbytes == 0) continue; // EOF (nigdy nie powinno wystąpić)

        uint64_t ts = systemTimestamp();
        CanFrame canFrame = parseFrame(frame, ts);
        emit newFrame(canFrame);
    }
    // Po wyjściu z pętli – zwolnienie zasobów
    closeSocket();
}

bool CanSniffer::openSocket(const QString &ifname) {
    // Utwórz socket CAN_RAW
    m_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (m_socket < 0) {
        emit errorOccurred("socket() nie powiódł się");
        return false;
    }

    // Pobierz indeks interfejsu
    struct ifreq ifr;
    std::strncpy(ifr.ifr_name, ifname.toStdString().c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    if (ioctl(m_socket, SIOCGIFINDEX, &ifr) < 0) {
        emit errorOccurred("Nie znaleziono interfejsu: " + ifname);
        closeSocket();
        return false;
    }

    // Zwiąż socket z interfejsem
    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(m_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        emit errorOccurred("bind() nie powiódł się");
        closeSocket();
        return false;
    }

    // Opcjonalnie: włącz odbiór własnych ramek, filtrowanie itp.
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
    frame.dlc = rawFrame.len;
    for (int i = 0; i < rawFrame.len && i < 8; ++i) {
        frame.data[i] = rawFrame.data[i];
    }
    frame.timestamp = timestamp;
    return frame;
}

uint64_t CanSniffer::systemTimestamp() const {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000000ULL + tv.tv_usec;
}
