#include "CanSniffer.h"
#include "ICanDriver.h"
#include <QDebug>
#include <QThreadPool>

CanSniffer::CanSniffer(QObject *parent) : QObject(parent) {}

CanSniffer::~CanSniffer() {
    if (m_running) stop();
}

bool CanSniffer::isSocketValid() const {
    return m_driver && m_driver->isValid();
}

void CanSniffer::writeFrame(const CanFrame &frame) {
    if (!m_driver || !m_driver->isValid()) {
        emit errorOccurred("writeFrame: sterownik CAN nie jest otwarty");
        return;
    }
    m_driver->writeFrame(frame);
}

void CanSniffer::start(const QString &interface) {
    if (m_running) return;
    if (!m_driver) {
        emit errorOccurred("Brak sterownika CAN – wywołaj setDriver() przed start()");
        return;
    }

    if (!QMetaType::fromName("CanFrame").isValid()) {
        emit errorOccurred("CanFrame nie jest zarejestrowany w QMetaType");
        return;
    }

    if (!m_driver->open(interface)) {
        emit errorOccurred("Nie można otworzyć interfejsu: " + interface);
        return;
    }

    m_interface = interface;
    m_running = true;
    emit statusChanged(true);
    QThreadPool::globalInstance()->start([this] { doWork(); });
}

void CanSniffer::stop() {
    if (!m_running) return;
    m_running = false;
    if (m_driver) m_driver->close();
    emit statusChanged(false);
}

void CanSniffer::doWork() {
    while (m_running) {
        CanFrame frame = m_driver->readFrame();
        if (frame.id == 0 && frame.dlc == 0 && !frame.error) {
            // Pusta ramka = driver zamknięty / błąd
            if (!m_running) break;  // zamknięcie normalne
            // Driver padł, ale wciąż running → błąd
            if (!m_driver->isValid()) break;
            continue;  // pusta ramka, czytaj dalej
        }
        emit newFrame(frame);
    }
}