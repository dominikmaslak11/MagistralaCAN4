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
    // Use the atomic open result — avoids cross-thread QSerialPort::isOpen() access.
    return m_driver && m_running.load() && m_openResult.load(std::memory_order_acquire);
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

    m_interface = interface;
    m_running = true;
    m_openResult.store(false);
    while (m_openSemaphore.tryAcquire()) {}  // drain any leftover from previous run

    m_ringBuffer.reserve(8192);
    // Driver open() is called from the worker thread so QSerialPort is created
    // and used in the same thread (Qt requirement — QSerialPort is not thread-safe).
    QThreadPool::globalInstance()->start([this] { doWork(); });

    // Block GUI thread until driver open completes (max 3 s).
    // Serial open is typically <100 ms, so this is acceptable.
    if (!m_openSemaphore.tryAcquire(1, 3000)) {
        m_running = false;
        emit errorOccurred("Timeout przy otwieraniu interfejsu: " + interface);
        m_threadDone.acquire(1);
        return;
    }
    if (!m_openResult.load(std::memory_order_acquire)) {
        m_running = false;
        m_threadDone.acquire(1);
        return;
    }
    emit statusChanged(true);
}

void CanSniffer::stop() {
    if (!m_running) return;
    m_running = false;
    // doWork() calls m_driver->close() from the worker thread before releasing m_threadDone.
    m_bufferNotFull.wakeAll();
    m_threadDone.acquire(1);
    emit statusChanged(false);
}

void CanSniffer::doWork() {
    auto releaseOnExit = qScopeGuard([this] { m_threadDone.release(1); });

    // Open the driver from the worker thread. QSerialPort (and similar serial classes)
    // must be created and used in the same thread — there is no Qt event loop in a
    // thread-pool thread, so the synchronous waitForReadyRead() path is used instead.
    bool opened = m_driver->open(m_interface);
    m_openResult.store(opened, std::memory_order_release);
    if (!opened) {
        QString iface = m_interface;
        QMetaObject::invokeMethod(this, [this, iface] {
            emit errorOccurred("Nie można otworzyć interfejsu: " + iface);
        }, Qt::QueuedConnection);
    }
    m_openSemaphore.release(1);  // unblock start()
    if (!opened) return;

    while (m_running) {
        CanFrame frame = m_driver->readFrame();
        if (frame.id == 0 && frame.dlc == 0 && !frame.error) {
            if (!m_running) break;
            if (!m_driver->isValid()) break;
            QThread::msleep(1);
            continue;
        }
        if (!m_ringBuffer.tryPush(frame)) {
            QMutexLocker lock(&m_waitMutex);
            while (!m_ringBuffer.tryPush(frame) && m_running)
                m_bufferNotFull.wait(&m_waitMutex, 5);
        }
    }

    m_driver->close();  // close from the same thread that opened
}

void CanSniffer::submitFrame(const CanFrame &frame) {
    emit newFrame(frame);
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