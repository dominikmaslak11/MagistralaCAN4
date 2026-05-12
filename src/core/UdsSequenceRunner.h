#pragma once
#include "CanFrame.h"
#include <QObject>
#include <QTimer>
#include <QString>
#include <vector>
#include <cstdint>

class CanSniffer;

/// One step in a UDS diagnostic sequence.
struct UdsStep {
    QString  name;            // Human-readable label
    uint32_t txCanId  = 0x7DF; // Tester→ECU CAN ID (functional = 0x7DF)
    uint32_t rxCanId  = 0x7E8; // ECU→Tester CAN ID
    std::vector<uint8_t> payload; // Raw UDS bytes (SID [sub] [did_hi] [did_lo] [data...])
    int      timeoutMs = 1000;
    int      maxRetries = 2;
    bool     stopOnNegResponse = true; // abort sequence on NRC
};

/// Run result for a single step.
struct UdsStepResult {
    enum Status { Pending, Pass, Fail, NegResponse, Timeout, Skipped };
    Status    status  = Pending;
    QString   detail;
    std::vector<uint8_t> response;
    int       retries = 0;
    double    elapsedMs = 0.0;
};

/// State machine that executes a UDS sequence by sending frames via CanSniffer
/// and monitoring incoming frames for matching responses.
class UdsSequenceRunner : public QObject {
    Q_OBJECT
public:
    explicit UdsSequenceRunner(CanSniffer *sniffer, QObject *parent = nullptr);

    void setSteps(const std::vector<UdsStep> &steps);
    const std::vector<UdsStep>    &steps()   const { return m_steps; }
    const std::vector<UdsStepResult> &results() const { return m_results; }

    bool isRunning() const { return m_running; }
    int  currentStep() const { return m_currentStep; }

public slots:
    void start();
    void abort();
    void processFrame(const CanFrame &frame);

signals:
    void stepCompleted(int stepIndex, UdsStepResult result);
    void sequenceFinished(bool allPassed);
    void statusMessage(const QString &msg);

private slots:
    void onTimeout();

private:
    void sendCurrentStep();
    void advanceStep();
    bool matchesExpectedResponse(const CanFrame &frame) const;

    CanSniffer            *m_sniffer;
    std::vector<UdsStep>   m_steps;
    std::vector<UdsStepResult> m_results;

    bool  m_running     = false;
    int   m_currentStep = 0;
    int   m_retryCount  = 0;
    QTimer *m_timeout   = nullptr;
    qint64  m_stepStart = 0;
};
