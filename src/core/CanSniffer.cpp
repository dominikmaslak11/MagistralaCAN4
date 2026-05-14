#include "CanSniffer.h"
#include "ICanDriver.h"
#include <QDebug>
#include <QThread>
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
    m_ringBuffer.reserve(8192);  // pre-alloc 8192 slotów (8191 usable)
    emit statusChanged(true);
    QThreadPool::globalInstance()->start([this] { doWork(); });
}

void CanSniffer::stop() {
    if (!m_running) return;
    m_running = false;
    if (m_driver) m_driver->close();
    m_bufferNotFull.wakeAll();       // unblock producer if it's waiting
    m_threadDone.acquire(1);         // wait for doWork() to exit
    emit statusChanged(false);
}

void CanSniffer::doWork() {
    auto releaseOnExit = qScopeGuard([this] { m_threadDone.release(1); });
    while (m_running) {
        CanFrame frame = m_driver->readFrame();
        if (frame.id == 0 && frame.dlc == 0 && !frame.error) {
            // Pusta ramka = brak danych lub driver zamknięty
            if (!m_running) break;  // zamknięcie normalne
            if (!m_driver->isValid()) break;  // driver padł
            QThread::msleep(1);  // idle — zapobiega busy-wait na nieblokujących driverach
            continue;
        }
        // Lock-free push to ring buffer
        if (!m_ringBuffer.tryPush(frame)) {
            // Ring buffer pełny — czekamy na sygnał od consumer (GUI)
            QMutexLocker lock(&m_waitMutex);
            while (!m_ringBuffer.tryPush(frame) && m_running)
                m_bufferNotFull.wait(&m_waitMutex, 5);  // max 5ms wait
        }
    }
}

void CanSniffer::drainAndEmit() {
    // Called from GUI thread — drains all buffered frames and emits them
    QVector<CanFrame> batch;
    int n = m_ringBuffer.drain(batch);
    if (n > 0)
        m_bufferNotFull.wakeOne();  // signal producer: buffer has space
    for (int i = 0; i < n; ++i)
        emit newFrame(batch[i]);
}