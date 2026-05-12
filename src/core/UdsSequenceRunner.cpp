#include "UdsSequenceRunner.h"
#include "CanSniffer.h"
#include <QDateTime>

UdsSequenceRunner::UdsSequenceRunner(CanSniffer *sniffer, QObject *parent)
    : QObject(parent), m_sniffer(sniffer) {
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, &UdsSequenceRunner::onTimeout);
}

void UdsSequenceRunner::setSteps(const std::vector<UdsStep> &steps) {
    if (m_running) return;
    m_steps = steps;
    m_results.assign(steps.size(), UdsStepResult{});
}

// ── Control ───────────────────────────────────────────────────────────────────

void UdsSequenceRunner::start() {
    if (m_running || m_steps.empty()) return;
    m_running     = true;
    m_currentStep = 0;
    m_results.assign(m_steps.size(), UdsStepResult{});
    emit statusMessage("Sekwencja UDS uruchomiona");
    sendCurrentStep();
}

void UdsSequenceRunner::abort() {
    if (!m_running) return;
    m_timeout->stop();
    m_running = false;

    // Mark remaining steps as Skipped
    for (int i = m_currentStep; i < static_cast<int>(m_steps.size()); ++i) {
        if (m_results[i].status == UdsStepResult::Pending)
            m_results[i].status = UdsStepResult::Skipped;
    }
    emit statusMessage("Sekwencja przerwana przez użytkownika");
    emit sequenceFinished(false);
}

// ── Step execution ────────────────────────────────────────────────────────────

void UdsSequenceRunner::sendCurrentStep() {
    if (m_currentStep >= static_cast<int>(m_steps.size())) {
        // All done
        m_running = false;
        bool allPassed = true;
        for (const auto &r : m_results)
            if (r.status != UdsStepResult::Pass) { allPassed = false; break; }
        emit sequenceFinished(allPassed);
        emit statusMessage(allPassed ? "Sekwencja zakończona — PASS" : "Sekwencja zakończona — FAIL");
        return;
    }

    const UdsStep &step = m_steps[static_cast<size_t>(m_currentStep)];

    CanFrame f{};
    f.id       = step.txCanId;
    f.extended = (step.txCanId > 0x7FF);
    f.dlc      = static_cast<uint8_t>(std::min(step.payload.size(), size_t(8)));
    for (size_t i = 0; i < f.dlc; ++i)
        f.data[i] = step.payload[i];

    if (m_sniffer) m_sniffer->writeFrame(f);
    m_stepStart = QDateTime::currentMSecsSinceEpoch();
    m_timeout->start(step.timeoutMs);

    emit statusMessage(QString("Krok %1/%2: %3 — oczekiwanie na odpowiedź...")
                       .arg(m_currentStep + 1).arg(m_steps.size()).arg(step.name));
}

// ── Response matching ─────────────────────────────────────────────────────────

bool UdsSequenceRunner::matchesExpectedResponse(const CanFrame &frame) const {
    if (!m_running || m_currentStep >= static_cast<int>(m_steps.size())) return false;
    const UdsStep &step = m_steps[static_cast<size_t>(m_currentStep)];

    if (frame.id != step.rxCanId) return false;
    if (frame.dlc < 1) return false;

    uint8_t sid = step.payload.empty() ? 0 : step.payload[0];
    uint8_t firstByte = frame.data[0];

    // Positive response: SID | 0x40
    if (firstByte == (sid | 0x40)) return true;
    // Negative response: 0x7F SID NRC
    if (firstByte == 0x7F && frame.dlc >= 3 && frame.data[1] == sid) return true;
    // Flow control (0x30): for multi-frame, treat as partial acknowledgement
    if ((firstByte & 0xF0) == 0x30) return true;

    return false;
}

void UdsSequenceRunner::processFrame(const CanFrame &frame) {
    if (!m_running) return;
    if (!matchesExpectedResponse(frame)) return;

    m_timeout->stop();

    const UdsStep &step = m_steps[static_cast<size_t>(m_currentStep)];
    UdsStepResult &result = m_results[static_cast<size_t>(m_currentStep)];
    result.elapsedMs = static_cast<double>(QDateTime::currentMSecsSinceEpoch() - m_stepStart);
    result.retries   = m_retryCount;

    for (int i = 0; i < frame.dlc; ++i)
        result.response.push_back(frame.data[i]);

    uint8_t sid = step.payload.empty() ? 0 : step.payload[0];
    if (frame.data[0] == (sid | 0x40)) {
        result.status = UdsStepResult::Pass;
        result.detail = "Pozytywna odpowiedź";
    } else if (frame.data[0] == 0x7F) {
        result.status = UdsStepResult::NegResponse;
        uint8_t nrc = frame.dlc >= 3 ? frame.data[2] : 0;
        result.detail = QString("NRC=0x%1").arg(nrc, 2, 16, QChar('0')).toUpper();
    } else {
        result.status = UdsStepResult::Pass; // flow control, accept
        result.detail = "Flow Control";
    }

    emit stepCompleted(m_currentStep, result);
    emit statusMessage(QString("Krok %1: %2 (%3 ms)")
                       .arg(m_currentStep + 1).arg(result.detail).arg(result.elapsedMs, 0, 'f', 1));

    if (result.status == UdsStepResult::NegResponse && step.stopOnNegResponse) {
        abort();
        return;
    }

    m_retryCount = 0;
    m_currentStep++;
    advanceStep();
}

void UdsSequenceRunner::advanceStep() {
    if (m_currentStep >= static_cast<int>(m_steps.size())) {
        sendCurrentStep(); // triggers the "all done" path
        return;
    }
    sendCurrentStep();
}

void UdsSequenceRunner::onTimeout() {
    if (!m_running) return;
    const UdsStep &step = m_steps[static_cast<size_t>(m_currentStep)];
    UdsStepResult &result = m_results[static_cast<size_t>(m_currentStep)];

    if (m_retryCount < step.maxRetries) {
        m_retryCount++;
        emit statusMessage(QString("Krok %1: timeout — retry %2/%3")
                           .arg(m_currentStep + 1).arg(m_retryCount).arg(step.maxRetries));
        sendCurrentStep();
    } else {
        result.status    = UdsStepResult::Timeout;
        result.detail    = QString("Timeout po %1 ms (%2 retries)")
                           .arg(step.timeoutMs).arg(step.maxRetries);
        result.elapsedMs = static_cast<double>(step.timeoutMs);
        result.retries   = m_retryCount;
        emit stepCompleted(m_currentStep, result);
        emit statusMessage(QString("Krok %1: TIMEOUT").arg(m_currentStep + 1));

        m_retryCount = 0;
        m_currentStep++;
        advanceStep();
    }
}
