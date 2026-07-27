#include "DecodingAccuracyRunner.h"
#include <QDebug>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QFile>
#include <QTimer>
#include <cmath>

// ── Ground truth: 10 syntetycznych sygnałów na 3 CAN ID (mini-DBC) ────────────
// Musi być IDENTYCZNE z kodowaniem w generatorze ruchu (Python, PEAK PCAN-USB) —
// patrz esp_experiment_4_1/generate_traffic.py.

const std::vector<uint32_t> kMessageIds = {0x100, 0x150, 0x200};

std::vector<DecodingAccuracyRunner::GroundTruthSignal>
DecodingAccuracyRunner::groundTruthFor(uint32_t canId) {
    std::vector<GroundTruthSignal> sigs;
    if (canId == 0x100) {
        sigs.push_back({"RPM", 0, 2, true, false, 0.25, 0.0, false, -1});
        sigs.push_back({"CoolantTemp", 2, 1, true, false, 1.0, -40.0, false, -1});
        sigs.push_back({"Throttle", 3, 1, true, false, 0.4, 0.0, false, -1});
    } else if (canId == 0x150) {
        sigs.push_back({"SteeringAngle", 0, 2, true, true, 0.1, 0.0, false, -1});
        sigs.push_back({"VehicleSpeed", 2, 2, true, false, 0.01, 0.0, false, -1});
    } else if (canId == 0x200) {
        sigs.push_back({"LeftIndicator",  0, 1, true, false, 1.0, 0.0, true, 0});
        sigs.push_back({"RightIndicator", 0, 1, true, false, 1.0, 0.0, true, 1});
        sigs.push_back({"Headlights",     0, 1, true, false, 1.0, 0.0, true, 2});
        sigs.push_back({"DriverDoor",     0, 1, true, false, 1.0, 0.0, true, 3});
        sigs.push_back({"Handbrake",      0, 1, true, false, 1.0, 0.0, true, 4});
    }
    return sigs;
}

// ── Bit/byte extraction (współdzielone przez ground truth i reguły LLM) ───────

uint32_t DecodingAccuracyRunner::extractRaw(const CanFrame &frame, int byteIdx, int byteLen,
                                             bool littleEndian) {
    uint32_t raw = 0;
    if (littleEndian) {
        for (int i = byteLen - 1; i >= 0; --i)
            raw = (raw << 8) | frame.byteAt(byteIdx + i);
    } else {
        for (int i = 0; i < byteLen; ++i)
            raw = (raw << 8) | frame.byteAt(byteIdx + i);
    }
    return raw;
}

static int32_t signExtend(uint32_t raw, int byteLen) {
    int bits = byteLen * 8;
    uint32_t signBit = 1u << (bits - 1);
    if (raw & signBit)
        return static_cast<int32_t>(raw) - static_cast<int32_t>(1u << bits);
    return static_cast<int32_t>(raw);
}

double DecodingAccuracyRunner::GroundTruthSignal::decode(const CanFrame &frame) const {
    if (isDiscrete) {
        uint8_t byteVal = frame.byteAt(byteIdx);
        return double((byteVal >> bitIndex) & 0x1);
    }
    uint32_t raw = extractRaw(frame, byteIdx, byteLen, littleEndian);
    double signedRaw = isSigned ? double(signExtend(raw, byteLen)) : double(raw);
    return signedRaw * scale + offset;
}

double DecodingAccuracyRunner::LlmSignalRule::decode(const CanFrame &frame) const {
    uint32_t raw = extractRaw(frame, byteIdx, byteLen, littleEndian);
    if (bitMask != 0xFFFFFFFFu) {
        raw &= bitMask;
        if (bitMask != 0) {
            int shift = 0;
            uint32_t m = bitMask;
            while (!(m & 1u)) { m >>= 1; shift++; }
            raw >>= shift;
        }
    }
    double signedRaw = isSigned ? double(signExtend(raw, byteLen)) : double(raw);
    return signedRaw * scale + offset;
}

// ── Prompt ──────────────────────────────────────────────────────────────────

QString DecodingAccuracyRunner::buildSystemPrompt() {
    return QStringLiteral(
        "You are a CAN bus reverse-engineering assistant. A CAN message (fixed ID) "
        "typically packs SEVERAL distinct signals into its payload bytes (e.g. one "
        "message might contain both engine RPM as a 2-byte value AND a temperature "
        "as a 1-byte value). Analyze the trigger frame and the recent frames for this "
        "ID (which show how values change over time) and identify ALL signals you can "
        "confidently distinguish, then propose a decoding rule for each.\n\n"
        "Respond with ONLY a JSON object (no markdown fences, no prose outside the "
        "JSON) with this exact shape:\n"
        "{\n"
        "  \"interpretation\": \"short description of the message\",\n"
        "  \"signals\": [\n"
        "    {\n"
        "      \"name\": \"short_signal_name\",\n"
        "      \"byteIdx\": <int, 0-based starting byte offset>,\n"
        "      \"byteLen\": <1 or 2, number of bytes the signal spans>,\n"
        "      \"littleEndian\": <bool, true if multi-byte signal is little-endian>,\n"
        "      \"isSigned\": <bool, true if the value can be negative (two's complement)>,\n"
        "      \"bitMask\": <hex string like \"0x01\" if this signal is a SINGLE BIT "
        "within byteIdx (discrete on/off state), or null if it uses the full byte(s)>,\n"
        "      \"scale\": <float, physical_value = raw * scale + offset>,\n"
        "      \"offset\": <float>\n"
        "    }\n"
        "  ],\n"
        "  \"confidence\": <float 0.0-1.0>\n"
        "}\n\n"
        "Discrete on/off states (lights, doors, indicators) are single bits within one "
        "byte — use bitMask to isolate the bit, byteLen=1, scale=1, offset=0. Continuous "
        "measurements (RPM, temperature, angle, speed) use byteLen=1 or 2 depending on "
        "the value range you observe, bitMask=null, and scale/offset chosen so the "
        "decoded physical value matches plausible real-world magnitudes for that quantity.");
}

// ── Parsowanie odpowiedzi LLM ──────────────────────────────────────────────────

static QString stripCodeFences(const QString &text) {
    QString t = text.trimmed();
    if (t.startsWith("```")) {
        int firstNl = t.indexOf('\n');
        if (firstNl >= 0) t = t.mid(firstNl + 1);
        if (t.endsWith("```")) t.chop(3);
    }
    return t.trimmed();
}

static QString extractOuterJsonObject(const QString &text) {
    int start = text.indexOf('{');
    int end = text.lastIndexOf('}');
    if (start < 0 || end < 0 || end <= start) return {};
    return text.mid(start, end - start + 1);
}

static uint32_t parseBitMask(const QJsonValue &v) {
    if (v.isNull() || v.isUndefined()) return 0xFFFFFFFFu;
    if (v.isString()) {
        QString s = v.toString().trimmed();
        bool ok = false;
        uint32_t m = s.startsWith("0x", Qt::CaseInsensitive)
                         ? s.mid(2).toUInt(&ok, 16)
                         : s.toUInt(&ok, 10);
        return ok ? m : 0xFFFFFFFFu;
    }
    if (v.isDouble()) return static_cast<uint32_t>(v.toDouble());
    return 0xFFFFFFFFu;
}

std::vector<DecodingAccuracyRunner::LlmSignalRule>
DecodingAccuracyRunner::parseRulesFromResponseText(const QString &text) {
    std::vector<LlmSignalRule> out;
    QString candidate = extractOuterJsonObject(stripCodeFences(text));
    if (candidate.isEmpty()) return out;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(candidate.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return out;

    QJsonArray sigArr = doc.object().value("signals").toArray();
    for (const auto &sv : sigArr) {
        QJsonObject so = sv.toObject();
        if (!so.contains("byteIdx")) continue;
        LlmSignalRule r;
        r.name         = so.value("name").toString();
        r.byteIdx      = so.value("byteIdx").toInt(-1);
        r.byteLen      = std::max(1, so.value("byteLen").toInt(1));
        r.littleEndian = so.value("littleEndian").toBool(true);
        r.isSigned     = so.value("isSigned").toBool(false);
        r.bitMask      = parseBitMask(so.value("bitMask"));
        r.scale        = so.value("scale").toDouble(1.0);
        r.offset       = so.value("offset").toDouble(0.0);
        if (r.byteIdx >= 0) out.push_back(r);
    }
    return out;
}

// ── Konstrukcja / konfiguracja ─────────────────────────────────────────────────

DecodingAccuracyRunner::DecodingAccuracyRunner(QObject *parent) : QObject(parent) {}

void DecodingAccuracyRunner::setDetector(ColdStartDetector *detector) {
    if (m_detector) disconnect(m_detector, nullptr, this, nullptr);
    m_detector = detector;
    if (m_detector)
        connect(m_detector, &ColdStartDetector::coldStartDetected,
                this, &DecodingAccuracyRunner::onColdStartDetected);
}

void DecodingAccuracyRunner::setLlmClient(LlmQueryClient *client) {
    if (m_llmClient) disconnect(m_llmClient, nullptr, this, nullptr);
    m_llmClient = client;
    if (m_llmClient) {
        connect(m_llmClient, &LlmQueryClient::llmResponseReceived,
                this, &DecodingAccuracyRunner::onLlmResponse);
        connect(m_llmClient, &LlmQueryClient::llmError,
                this, &DecodingAccuracyRunner::onLlmError);
    }
}

void DecodingAccuracyRunner::useRealHardware(WebSocketServer *server) {
    if (m_wsServer) disconnect(m_wsServer, nullptr, this, nullptr);
    m_wsServer = server;
    if (m_wsServer)
        connect(m_wsServer, &WebSocketServer::frameReceivedFromClient,
                this, &DecodingAccuracyRunner::onRealFrameReceived);
}

void DecodingAccuracyRunner::setModel(const QString &modelName, const QString &apiKey) {
    m_modelName = modelName;
    m_apiKey = apiKey;
}

// ── State machine ──────────────────────────────────────────────────────────────

void DecodingAccuracyRunner::setState(State s) { m_state = s; }

void DecodingAccuracyRunner::start() {
    if (m_running) return;
    if (!m_detector || !m_llmClient || !m_wsServer) {
        emit experimentError(QStringLiteral("Detector/LlmClient/WebSocketServer not set"));
        return;
    }

    LlmConfig cfg;
    cfg.model = m_modelName;
    cfg.apiKey = m_apiKey;
    if (m_modelName.startsWith("gpt-")) cfg.backend = LlmBackend::OpenAI;
    else if (m_modelName.startsWith("claude-")) cfg.backend = LlmBackend::Anthropic;
    else if (m_modelName.startsWith("deepseek")) cfg.backend = LlmBackend::DeepSeek;
    else if (m_modelName.startsWith("gemini")) cfg.backend = LlmBackend::Gemini;
    else cfg.backend = LlmBackend::OpenAI;
    m_llmClient->setConfig(cfg);

    if (m_reportPath.isEmpty()) {
        m_reportPath = QString("decoding_accuracy_report_%1.json")
                           .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    }

    m_running = true;
    m_currentTrial = 0;
    m_targetIdCycle = 0;
    m_targetCanId = kMessageIds[0];
    m_results.clear();
    m_trialLog = QJsonArray{};
    m_frameHistory.clear();

    m_detector->reset();
    setState(State::WaitingForColdStart);

    qDebug() << "[DecodingAccuracyRunner] Start:" << m_modelName
             << "totalTrials=" << m_totalTrials
             << "framesPerTrial=" << m_framesToEvaluate;
    emit progressChanged(0.0, QString("Oczekiwanie na Cold Start (target ID 0x%1)...")
                                  .arg(m_targetCanId, 3, 16, QChar('0')));
}

void DecodingAccuracyRunner::onRealFrameReceived(const CanFrame &frame) {
    if (!m_running) return;

    if (m_state == State::WaitingForColdStart) {
        m_detector->evaluate(frame);
        return;
    }
    if (m_state == State::CollectingFrames && frame.id == m_currentCanId) {
        evaluateFrameAgainstRules(frame);
        m_framesCollected++;
        if (m_framesCollected >= m_framesToEvaluate)
            finalizeCurrentTrial();
    }
}

void DecodingAccuracyRunner::onColdStartDetected(uint32_t canId, const CanFrame &frame,
                                                  uint64_t /*timestampUs*/, const QString &/*reason*/) {
    if (!m_running || m_state != State::WaitingForColdStart) return;

    if (canId != m_targetCanId) {
        // Nie nasz cel w tej rundzie round-robin — oznacz jako znane i czekaj dalej
        // na docelowe ID (żeby nie wyzwalało ponownie, ale nie liczy się do próby).
        m_detector->markKnown(canId);
        return;
    }

    m_currentCanId = canId;
    m_frameHistory.push_back(frame);
    while (static_cast<int>(m_frameHistory.size()) > kMaxHistory) m_frameHistory.pop_front();
    m_detector->markKnownWithPattern(canId, frame);
    m_currentGroundTruth = groundTruthFor(canId);

    setState(State::QueryingLlm);

    LlmQuery query;
    query.canId = canId;
    query.triggerFrame = frame;
    query.recentFrames = {m_frameHistory.begin(), m_frameHistory.end()};
    query.systemPrompt = buildSystemPrompt();

    m_llmClient->query(query);
}

void DecodingAccuracyRunner::onLlmResponse(const LlmResponse &resp) {
    if (!m_running || m_state != State::QueryingLlm) return;

    QJsonObject logEntry;
    logEntry["trial"] = m_currentTrial;
    logEntry["canId"] = QString("0x%1").arg(m_currentCanId, 3, 16, QChar('0'));
    logEntry["success"] = resp.success;
    logEntry["ruleText"] = resp.ruleText;
    if (!resp.success) logEntry["error"] = resp.errorMsg;

    m_currentRules = resp.success ? parseRulesFromResponseText(resp.ruleText)
                                   : std::vector<LlmSignalRule>{};

    QJsonArray parsedArr;
    for (const auto &r : m_currentRules) {
        QJsonObject ro;
        ro["name"] = r.name;
        ro["byteIdx"] = r.byteIdx;
        ro["byteLen"] = r.byteLen;
        ro["littleEndian"] = r.littleEndian;
        ro["isSigned"] = r.isSigned;
        ro["scale"] = r.scale;
        ro["offset"] = r.offset;
        parsedArr.append(ro);
    }
    logEntry["parsedSignals"] = parsedArr;
    m_trialLog.append(logEntry);

    m_framesCollected = 0;
    setState(State::CollectingFrames);
    emit progressChanged(double(m_currentTrial) / double(m_totalTrials),
        QString("Proba %1/%2: ID 0x%3 — %4 sygnalow zaproponowanych, zbieram ramki...")
            .arg(m_currentTrial + 1).arg(m_totalTrials)
            .arg(m_currentCanId, 3, 16, QChar('0')).arg(m_currentRules.size()));
}

void DecodingAccuracyRunner::onLlmError(const QString &errorMsg) {
    if (!m_running || m_state != State::QueryingLlm) return;
    qWarning() << "[DecodingAccuracyRunner] LLM error:" << errorMsg;

    QJsonObject logEntry;
    logEntry["trial"] = m_currentTrial;
    logEntry["canId"] = QString("0x%1").arg(m_currentCanId, 3, 16, QChar('0'));
    logEntry["success"] = false;
    logEntry["error"] = errorMsg;
    m_trialLog.append(logEntry);

    m_currentRules.clear();
    m_framesCollected = 0;
    setState(State::CollectingFrames);
}

// Znajduje pozycje jedynego ustawionego bitu w masce (0-31), albo -1 (brak
// ustawionych bitow) / -2 (wiecej niz jeden bit ustawiony — maska niejednoznaczna
// dla pojedynczego stanu dyskretnego).
int DecodingAccuracyRunner::singleBitPosition(uint32_t mask) {
    int pos = -1;
    for (int b = 0; b < 32; ++b) {
        if (mask & (1u << b)) {
            if (pos != -1) return -2;
            pos = b;
        }
    }
    return pos;
}

// Dopasowuje regule LLM do sygnalu ground truth. KRYTYCZNE: kilka sygnalow
// dyskretnych moze dzielic ten sam byteIdx (np. 5 flag w jednym bajcie) —
// samo dopasowanie po byteIdx nie wystarczy, trzeba tez sprawdzic KTORY bit
// maska izoluje. Dla sygnalow ciaglych preferujemy regule BEZ maskowania
// (pelny zakres bajtow) na tym samym byteIdx.
const DecodingAccuracyRunner::LlmSignalRule *DecodingAccuracyRunner::findMatchingRule(
        const GroundTruthSignal &gt, const std::vector<LlmSignalRule> &rules) {
    if (gt.isDiscrete) {
        for (const auto &r : rules) {
            if (r.byteIdx != gt.byteIdx) continue;
            if (r.bitMask == 0xFFFFFFFFu || r.bitMask == 0) continue;
            if (singleBitPosition(r.bitMask) == gt.bitIndex) return &r;
        }
        return nullptr;
    }
    const LlmSignalRule *fallback = nullptr;
    for (const auto &r : rules) {
        if (r.byteIdx != gt.byteIdx) continue;
        if (r.bitMask == 0xFFFFFFFFu) return &r;
        if (!fallback) fallback = &r;
    }
    return fallback;
}

void DecodingAccuracyRunner::evaluateFrameAgainstRules(const CanFrame &frame) {
    for (const auto &gt : m_currentGroundTruth) {
        const LlmSignalRule *matched = findMatchingRule(gt, m_currentRules);
        if (!matched) continue; // brak dopasowania — zliczone w finalizeCurrentTrial()

        double truth = gt.decode(frame);
        double pred = matched->decode(frame);

        SignalAccum &acc = m_results[gt.name];
        acc.name = gt.name;
        acc.isDiscrete = gt.isDiscrete;

        if (gt.isDiscrete) {
            int truthClass = truth >= 0.5 ? 1 : 0;
            int predClass  = pred  >= 0.5 ? 1 : 0;
            if (truthClass == 1 && predClass == 1) acc.tp++;
            else if (truthClass == 0 && predClass == 0) acc.tn++;
            else if (truthClass == 0 && predClass == 1) acc.fp++;
            else acc.fn++;
        } else {
            double err = truth - pred;
            acc.squaredErrors.push_back(err * err);
        }
    }
}

void DecodingAccuracyRunner::finalizeCurrentTrial() {
    for (const auto &gt : m_currentGroundTruth) {
        SignalAccum &acc = m_results[gt.name];
        acc.name = gt.name;
        acc.isDiscrete = gt.isDiscrete;
        acc.trialsSeen++;
        if (findMatchingRule(gt, m_currentRules) != nullptr) acc.trialsDetected++;
    }

    m_currentTrial++;
    emit trialCompleted(m_currentTrial, m_currentCanId);

    if (m_currentTrial >= m_totalTrials) {
        finishExperiment();
        return;
    }

    m_targetIdCycle = (m_targetIdCycle + 1) % static_cast<int>(kMessageIds.size());
    m_targetCanId = kMessageIds[m_targetIdCycle];
    m_detector->reset();
    setState(State::WaitingForColdStart);

    QTimer::singleShot(50, this, [this]() {
        emit progressChanged(double(m_currentTrial) / double(m_totalTrials),
            QString("Proba %1/%2: oczekiwanie na ID 0x%3...")
                .arg(m_currentTrial + 1).arg(m_totalTrials)
                .arg(m_targetCanId, 3, 16, QChar('0')));
    });
}

// ── Zakończenie / raport ────────────────────────────────────────────────────────

void DecodingAccuracyRunner::finishExperiment() {
    m_running = false;
    setState(State::Done);

    QJsonObject report;
    report["experiment"] = QStringLiteral("4.1 — Decoding Accuracy vs Ground Truth");
    report["model"] = m_modelName;
    report["totalTrials"] = m_currentTrial;
    report["framesEvaluatedPerTrial"] = m_framesToEvaluate;
    report["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonArray signalsArr;
    for (auto it = m_results.constBegin(); it != m_results.constEnd(); ++it) {
        const SignalAccum &acc = it.value();
        QJsonObject so;
        so["name"] = acc.name;
        so["isDiscrete"] = acc.isDiscrete;
        so["trialsSeen"] = acc.trialsSeen;
        so["trialsDetected"] = acc.trialsDetected;
        so["detectionRate"] = acc.trialsSeen > 0
            ? double(acc.trialsDetected) / double(acc.trialsSeen) : 0.0;

        if (acc.isDiscrete) {
            int total = acc.tp + acc.tn + acc.fp + acc.fn;
            double precision = (acc.tp + acc.fp) > 0
                ? double(acc.tp) / double(acc.tp + acc.fp) : 0.0;
            double recall = (acc.tp + acc.fn) > 0
                ? double(acc.tp) / double(acc.tp + acc.fn) : 0.0;
            double f1 = (precision + recall) > 0
                ? 2.0 * precision * recall / (precision + recall) : 0.0;
            double accuracy = total > 0 ? double(acc.tp + acc.tn) / double(total) : 0.0;
            QJsonObject cm;
            cm["tp"] = acc.tp; cm["tn"] = acc.tn; cm["fp"] = acc.fp; cm["fn"] = acc.fn;
            so["confusionMatrix"] = cm;
            so["precision"] = precision;
            so["recall"] = recall;
            so["f1"] = f1;
            so["accuracy"] = accuracy;
            so["nSamplesEvaluated"] = total;
        } else {
            double mse = 0.0;
            for (double se : acc.squaredErrors) mse += se;
            int n = static_cast<int>(acc.squaredErrors.size());
            mse = n > 0 ? mse / n : 0.0;
            so["mse"] = mse;
            so["rmse"] = std::sqrt(mse);
            so["nSamplesEvaluated"] = n;
        }
        signalsArr.append(so);
    }
    report["signals"] = signalsArr;
    report["trialLog"] = m_trialLog;

    QJsonDocument doc(report);
    QFile file(m_reportPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    } else {
        qWarning() << "[DecodingAccuracyRunner] Cannot write report to" << m_reportPath;
    }

    qDebug() << "[DecodingAccuracyRunner] Finished. Report:" << m_reportPath;
    emit progressChanged(1.0, QString("Eksperyment zakonczony — raport: %1").arg(m_reportPath));
    emit experimentFinished(m_reportPath);
}
