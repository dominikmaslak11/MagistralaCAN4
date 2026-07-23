#include "ColdStartDetector.h"
#include <QDebug>

ColdStartDetector::ColdStartDetector(QObject *parent) : QObject(parent) {}

void ColdStartDetector::markKnown(uint32_t canId) {
    m_knownIds.insert(canId);
}

void ColdStartDetector::markKnownWithPattern(uint32_t canId, const CanFrame &frame) {
    m_knownIds.insert(canId);
    std::vector<uint8_t> pattern(frame.dlc);
    for (int i = 0; i < frame.dlc && i < 64; ++i)
        pattern[i] = frame.data[i];
    m_knownPatterns[canId] = std::move(pattern);
}

void ColdStartDetector::reset() {
    m_knownIds.clear();
    m_knownPatterns.clear();
}

bool ColdStartDetector::isKnown(uint32_t canId) const {
    return m_knownIds.find(canId) != m_knownIds.end();
}

void ColdStartDetector::evaluate(const CanFrame &frame) {
    // Pomijamy ramki błędów i RTR
    if (frame.error || frame.rtr) return;

    uint32_t id = frame.id;
    uint64_t ts = frame.timestamp;

    auto it = m_knownIds.find(id);
    if (it == m_knownIds.end()) {
        // Nowy CAN ID → Cold Start
        qDebug() << "[ColdStartDetector] new CAN ID 0x"
                 << QString::number(id, 16) << "detected";
        emit coldStartDetected(id, frame, ts, QStringLiteral("new_id"));
        return;
    }

    // Znany ID — sprawdź odległość Hamminga od wzorca
    auto pit = m_knownPatterns.find(id);
    if (pit != m_knownPatterns.end()) {
        int len = std::min(static_cast<int>(pit->second.size()),
                           static_cast<int>(frame.dlc));
        int dist = hammingDistance(pit->second.data(), frame.data.data(), len);
        if (dist > m_hammingThreshold) {
            qDebug() << "[ColdStartDetector] CAN ID 0x"
                     << QString::number(id, 16)
                     << "Hamming distance" << dist
                     << "> threshold" << m_hammingThreshold;
            emit coldStartDetected(id, frame, ts, QStringLiteral("hamming_distance"));
        }
    }
}

int ColdStartDetector::hammingDistance(const uint8_t *a, const uint8_t *b, int len) {
    int dist = 0;
    for (int i = 0; i < len; ++i) {
        uint8_t xorVal = a[i] ^ b[i];
        // Brian Kernighan's bit count
        while (xorVal) {
            dist++;
            xorVal &= xorVal - 1;
        }
    }
    return dist;
}
