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
#include <algorithm>

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

// Wariant few-shot: ten sam prompt bazowy + dwa w pelni rozwiazane przyklady.
// Przyklad 2 jest kluczowy - pokazuje WPROST, ze JEDEN bajt moze zawierac
// KILKA niezaleznych flag bitowych (dokladnie zaobserwowana slabosc Claude'a:
// traktowanie takiego bajtu jako pojedynczej wartosci skalarnej). CAN ID i
// nazwy sygnalow w przykladach sa CELOWO INNE niz w naszym tescie (0x100/
// 0x150/0x200), zeby model uczyl sie WZORCA, nie zapamietywal konkretnej
// odpowiedzi dla tych samych ramek.
QString DecodingAccuracyRunner::buildSystemPromptFewShot() {
    return buildSystemPrompt() + QStringLiteral(
        "\n\n"
        "Here are two fully worked examples of the expected reasoning and output:\n\n"
        "EXAMPLE 1 — continuous multi-byte signal:\n"
        "CAN ID: 0x050, DLC: 4\n"
        "Recent frames: bytes 0-1 climb steadily together (e.g. 0x10 0x27, then "
        "0x20 0x27, then 0x30 0x27 — interpreted little-endian: 10000, 10016, 10032), "
        "bytes 2-3 stay at 0x00 0x00.\n"
        "Correct output:\n"
        "{\"interpretation\": \"Single 16-bit little-endian counter/speed value in "
        "bytes 0-1; bytes 2-3 unused\", \"signals\": [{\"name\": \"wheel_speed\", "
        "\"byteIdx\": 0, \"byteLen\": 2, \"littleEndian\": true, \"isSigned\": false, "
        "\"bitMask\": null, \"scale\": 0.1, \"offset\": 0.0}], \"confidence\": 0.7}\n\n"
        "EXAMPLE 2 — ONE byte packs SEVERAL independent bit flags (important pattern):\n"
        "CAN ID: 0x060, DLC: 3\n"
        "Recent frames: byte 2 takes values like 0x00, 0x01, 0x02, 0x03, 0x05, 0x04 "
        "(i.e. individual bits toggle independently and in combination), bytes 0-1 "
        "stay constant.\n"
        "Correct output:\n"
        "{\"interpretation\": \"Byte 2 packs at least 3 independent single-bit status "
        "flags; bytes 0-1 constant/unused\", \"signals\": ["
        "{\"name\": \"brake_light\", \"byteIdx\": 2, \"byteLen\": 1, \"littleEndian\": "
        "false, \"isSigned\": false, \"bitMask\": \"0x01\", \"scale\": 1.0, \"offset\": 0.0}, "
        "{\"name\": \"reverse_light\", \"byteIdx\": 2, \"byteLen\": 1, \"littleEndian\": "
        "false, \"isSigned\": false, \"bitMask\": \"0x02\", \"scale\": 1.0, \"offset\": 0.0}, "
        "{\"name\": \"seatbelt_warning\", \"byteIdx\": 2, \"byteLen\": 1, \"littleEndian\": "
        "false, \"isSigned\": false, \"bitMask\": \"0x04\", \"scale\": 1.0, \"offset\": 0.0}"
        "], \"confidence\": 0.6}\n"
        "Note: do NOT propose a single scalar signal for byte 2 here — when a byte's "
        "observed values look like independent bit combinations rather than a smooth "
        "ordered progression, decompose it into one signal per bit instead.\n\n"
        "Now analyze the real frame below the same way.");
}

// Wariant "entropy-analysis": zamiast przykladow, WYMUSZA jawna, obowiazkowa
// procedure krok-po-kroku sprawdzenia KAZDEGO bajtu PRZED zaproponowaniem
// interpretacji. W przeciwienstwie do few-shot (ktory Claude calkowicie
// zignorowal - patrz Eksperyment_4.1_FewShot_Claude_Wynik_Negatywny_20260727.txt),
// to nie jest przyklad do naslodowania, tylko wprost sformulowany algorytm
// decyzyjny, ktory model MA zastosowac do KAZDEGO bajtu tej konkretnej ramki.
QString DecodingAccuracyRunner::buildSystemPromptEntropyAnalysis() {
    return buildSystemPrompt() + QStringLiteral(
        "\n\n"
        "MANDATORY procedure — apply this to EVERY byte position (0 to DLC-1) "
        "BEFORE deciding on your final signal list. Do not skip this, and do not "
        "default to treating a byte as a single scalar without first ruling out "
        "the bit-flags pattern below:\n\n"
        "For byte position B:\n"
        "  1. List the distinct raw values observed for byte B across the trigger "
        "frame and all recent frames.\n"
        "  2. Write each distinct value in binary (8 bits).\n"
        "  3. Check the BIT-FLAGS pattern: if byte B has only a FEW distinct values "
        "(roughly 2-8) AND the bits that differ between those values do not form a "
        "smooth ordered/incrementing sequence (e.g. observed set is like "
        "{0x00, 0x01, 0x02, 0x03, 0x10, 0x11} rather than {0x00, 0x01, 0x02, 0x03, "
        "0x04, 0x05, ...} counting up by 1), this is strong evidence of MULTIPLE "
        "INDEPENDENT BIT FLAGS packed into byte B, not one scalar value.\n"
        "  4. Check the SCALAR pattern: if the values change in a smooth, ordered, "
        "roughly monotonic or counter-like way, OR the byte combines with an "
        "adjacent byte to form a larger measurement, this is a genuine scalar/"
        "multi-byte signal.\n"
        "  5. If step 3 applies to byte B: propose ONE separate signal per bit "
        "position that actually changes across observations (byteIdx=B, byteLen=1, "
        "bitMask isolating exactly that bit, scale=1, offset=0) — do NOT also "
        "propose a combined scalar signal for that same byte.\n"
        "  6. If step 4 applies: propose the scalar/multi-byte signal as usual.\n\n"
        "This byte-by-byte bit-flags-vs-scalar check is mandatory for every byte, "
        "including byte 0. A byte with a small set of non-sequential values is "
        "bit flags far more often than it is a meaningful single scalar — treat "
        "the bit-flags interpretation as the default hypothesis to disprove, not "
        "the other way around.");
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
    // Domyslne maxTokens=1024 (LlmQueryClient) wystarcza Eksperymentowi 1.1
    // (nie parsuje tresci, liczy tylko czas), ale tutaj MUSIMY otrzymac
    // KOMPLETNY, poprawny JSON z wieloma sygnalami - 1024 okazalo sie za
    // malo (modele reasoning jak DeepSeek ucinaly odpowiedz w polowie
    // "myslenia na glos" zanim doszly do wlasciwego JSON-a, Gemini ucinal
    // sie w polowie pierwszego pola). Zwiekszone z duzym zapasem.
    cfg.maxTokens = 8192;
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
    m_frameHistoryByCanId.clear();

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
    std::deque<CanFrame> &history = m_frameHistoryByCanId[canId];
    history.push_back(frame);
    while (static_cast<int>(history.size()) > kMaxHistory) history.pop_front();
    m_detector->markKnownWithPattern(canId, frame);
    m_currentGroundTruth = groundTruthFor(canId);
    m_currentHistoryFrames = {history.begin(), history.end()};

    setState(State::QueryingLlm);

    LlmQuery query;
    query.canId = canId;
    query.triggerFrame = frame;
    query.recentFrames = m_currentHistoryFrames;
    switch (m_promptVariant) {
        case PromptVariant::FewShot:         query.systemPrompt = buildSystemPromptFewShot(); break;
        case PromptVariant::EntropyAnalysis: query.systemPrompt = buildSystemPromptEntropyAnalysis(); break;
        case PromptVariant::ZeroShot:
        default:                            query.systemPrompt = buildSystemPrompt(); break;
    }

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
    m_currentRulesOverride = applyBitFlagOverride(m_currentRules, m_currentHistoryFrames);
    logEntry["overrideRuleCount"] = static_cast<int>(m_currentRulesOverride.size());
    logEntry["overrideChangedRules"] = (m_currentRulesOverride.size() != m_currentRules.size());

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

// ── Hybrydowy override klasyczny (Kierunek B) ──────────────────────────────────
// Zwraca maske bitow na pozycji byteIdx, ktore w podanych ramkach przyjmuja
// OBA stany (0 i 1) — kandydaci na niezalezne flagi bitowe. Liczone WYLACZNIE
// z obserwowanych wartosci (nie z ground truth) — to musi byc uczciwa,
// samodzielna heurystyka, nie podgladanie odpowiedzi.
uint8_t DecodingAccuracyRunner::independentBitMask(const std::vector<CanFrame> &frames, int byteIdx) {
    uint8_t seen0 = 0, seen1 = 0;
    for (const auto &f : frames) {
        uint8_t v = f.byteAt(byteIdx);
        for (int b = 0; b < 8; ++b) {
            if (v & (1u << b)) seen1 |= static_cast<uint8_t>(1u << b);
            else seen0 |= static_cast<uint8_t>(1u << b);
        }
    }
    return seen0 & seen1;
}

// Heurystyka: bajt "wyglada jak flagi bitowe" jesli (1) przynajmniej jeden bit
// realnie przyjmuje oba stany w historii, ORAZ (2) kolejne (W KOLEJNOSCI
// CZASOWEJ) probki czesto roznia sie SKOKOWO (>3), a nie plynnie o male kroki.
// WAZNE: analiza samego ZBIORU wartosci (posortowanego) nie wystarcza — N
// niezaleznych bitow generuje zbior {0..2^N-1}, czyli GESTY ciag kolejnych
// liczb, nieodrozniajacy sie od licznika/skalara po samym zakresie. Prawdziwa
// roznica jest w PRZEBIEGU W CZASIE: przelaczenie bitu wyzszego rzedu (np.
// bit2..bit4) daje skok o >=4, podczas gdy skalar/licznik (np. powolny
// random-walk throttle/temperatury) zmienia sie w kolejnych probkach o male
// wartosci. Zweryfikowane symulacja Python przed wdrozeniem (100% trafien na
// syntetycznych flagach bitowych, 0% falszywych trafien na syntetycznych
// sygnalach ciaglych, w calym realistycznym zakresie odstepow probkowania).
bool DecodingAccuracyRunner::looksLikeBitFlags(const std::vector<CanFrame> &frames, int byteIdx) {
    if (frames.size() < 2) return false;
    if (independentBitMask(frames, byteIdx) == 0) return false;

    int bigJumps = 0, changedPairs = 0;
    for (size_t i = 1; i < frames.size(); ++i) {
        int a = frames[i - 1].byteAt(byteIdx);
        int b = frames[i].byteAt(byteIdx);
        if (a == b) continue;
        changedPairs++;
        if (std::abs(a - b) > 3) bigJumps++;
    }
    if (changedPairs == 0) return false;
    return (double(bigJumps) / double(changedPairs)) >= 0.5;
}

// Jesli LLM zaproponowal pojedynczy skalar (bitMask=null, byteLen=1) dla bajtu,
// ktory klasyfikuje sie jako flagi bitowe, zastepuje te regule zestawem reguł
// per-bit (po jednej na kazdy bit realnie przyjmujacy oba stany). Inne reguly
// (wielobajtowe skalary, juz zamaskowane, bajty nie-flagowe) przechodza bez zmian.
std::vector<DecodingAccuracyRunner::LlmSignalRule> DecodingAccuracyRunner::applyBitFlagOverride(
        const std::vector<LlmSignalRule> &rules, const std::vector<CanFrame> &frames) {
    std::vector<LlmSignalRule> out;
    for (const auto &r : rules) {
        bool isPlainByteScalar = (r.byteLen == 1 && r.bitMask == 0xFFFFFFFFu);
        if (isPlainByteScalar && !frames.empty() && looksLikeBitFlags(frames, r.byteIdx)) {
            uint8_t mask = independentBitMask(frames, r.byteIdx);
            for (int b = 0; b < 8; ++b) {
                if (!(mask & (1u << b))) continue;
                LlmSignalRule bitRule;
                bitRule.name = QString("%1_bit%2_override").arg(r.name).arg(b);
                bitRule.byteIdx = r.byteIdx;
                bitRule.byteLen = 1;
                bitRule.littleEndian = true;
                bitRule.isSigned = false;
                bitRule.bitMask = (1u << b);
                bitRule.scale = 1.0;
                bitRule.offset = 0.0;
                out.push_back(bitRule);
            }
            continue; // oryginalny skalar zastapiony regulami per-bit
        }
        out.push_back(r);
    }
    return out;
}

void DecodingAccuracyRunner::evaluateFrameAgainstRules(const CanFrame &frame) {
    for (const auto &gt : m_currentGroundTruth) {
        SignalAccum &acc = m_results[gt.name];
        acc.name = gt.name;
        acc.isDiscrete = gt.isDiscrete;

        if (const LlmSignalRule *matched = findMatchingRule(gt, m_currentRules)) {
            double truth = gt.decode(frame);
            double pred = matched->decode(frame);
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

        // Rownolegla ocena z zastosowanym hybrydowym override'em (Kierunek B) —
        // patrz applyBitFlagOverride(). Nie wplywa na powyzsze "surowe" liczniki.
        if (const LlmSignalRule *matchedOv = findMatchingRule(gt, m_currentRulesOverride)) {
            double truth = gt.decode(frame);
            double pred = matchedOv->decode(frame);
            if (gt.isDiscrete) {
                int truthClass = truth >= 0.5 ? 1 : 0;
                int predClass  = pred  >= 0.5 ? 1 : 0;
                if (truthClass == 1 && predClass == 1) acc.tpOverride++;
                else if (truthClass == 0 && predClass == 0) acc.tnOverride++;
                else if (truthClass == 0 && predClass == 1) acc.fpOverride++;
                else acc.fnOverride++;
            } else {
                double err = truth - pred;
                acc.squaredErrorsOverride.push_back(err * err);
            }
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
        if (findMatchingRule(gt, m_currentRulesOverride) != nullptr) acc.trialsDetectedOverride++;
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
    switch (m_promptVariant) {
        case PromptVariant::FewShot:         report["promptVariant"] = QStringLiteral("few-shot"); break;
        case PromptVariant::EntropyAnalysis: report["promptVariant"] = QStringLiteral("entropy-analysis"); break;
        case PromptVariant::ZeroShot:
        default:                            report["promptVariant"] = QStringLiteral("zero-shot"); break;
    }
    report["totalTrials"] = m_currentTrial;
    report["framesEvaluatedPerTrial"] = m_framesToEvaluate;
    report["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonArray signalsArr;
    double sumDetectionRate = 0.0, sumDetectionRateOverride = 0.0;
    int nSignals = 0;
    for (auto it = m_results.constBegin(); it != m_results.constEnd(); ++it) {
        const SignalAccum &acc = it.value();
        QJsonObject so;
        so["name"] = acc.name;
        so["isDiscrete"] = acc.isDiscrete;
        so["trialsSeen"] = acc.trialsSeen;
        so["trialsDetected"] = acc.trialsDetected;
        double detectionRate = acc.trialsSeen > 0
            ? double(acc.trialsDetected) / double(acc.trialsSeen) : 0.0;
        so["detectionRate"] = detectionRate;

        // Rownolegle metryki z hybrydowym override'em (Kierunek B) — patrz
        // applyBitFlagOverride(). Dodatkowe, NIE zastepuja powyzszych "surowych"
        // metryk LLM.
        so["trialsDetectedOverride"] = acc.trialsDetectedOverride;
        double detectionRateOverride = acc.trialsSeen > 0
            ? double(acc.trialsDetectedOverride) / double(acc.trialsSeen) : 0.0;
        so["detectionRateOverride"] = detectionRateOverride;

        sumDetectionRate += detectionRate;
        sumDetectionRateOverride += detectionRateOverride;
        nSignals++;

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

            int totalOv = acc.tpOverride + acc.tnOverride + acc.fpOverride + acc.fnOverride;
            double precisionOv = (acc.tpOverride + acc.fpOverride) > 0
                ? double(acc.tpOverride) / double(acc.tpOverride + acc.fpOverride) : 0.0;
            double recallOv = (acc.tpOverride + acc.fnOverride) > 0
                ? double(acc.tpOverride) / double(acc.tpOverride + acc.fnOverride) : 0.0;
            double f1Ov = (precisionOv + recallOv) > 0
                ? 2.0 * precisionOv * recallOv / (precisionOv + recallOv) : 0.0;
            double accuracyOv = totalOv > 0
                ? double(acc.tpOverride + acc.tnOverride) / double(totalOv) : 0.0;
            QJsonObject cmOv;
            cmOv["tp"] = acc.tpOverride; cmOv["tn"] = acc.tnOverride;
            cmOv["fp"] = acc.fpOverride; cmOv["fn"] = acc.fnOverride;
            so["confusionMatrixOverride"] = cmOv;
            so["precisionOverride"] = precisionOv;
            so["recallOverride"] = recallOv;
            so["f1Override"] = f1Ov;
            so["accuracyOverride"] = accuracyOv;
            so["nSamplesEvaluatedOverride"] = totalOv;
        } else {
            double mse = 0.0;
            for (double se : acc.squaredErrors) mse += se;
            int n = static_cast<int>(acc.squaredErrors.size());
            mse = n > 0 ? mse / n : 0.0;
            so["mse"] = mse;
            so["rmse"] = std::sqrt(mse);
            so["nSamplesEvaluated"] = n;

            double mseOv = 0.0;
            for (double se : acc.squaredErrorsOverride) mseOv += se;
            int nOv = static_cast<int>(acc.squaredErrorsOverride.size());
            mseOv = nOv > 0 ? mseOv / nOv : 0.0;
            so["mseOverride"] = mseOv;
            so["rmseOverride"] = std::sqrt(mseOv);
            so["nSamplesEvaluatedOverride"] = nOv;
        }
        signalsArr.append(so);
    }
    report["signals"] = signalsArr;
    report["trialLog"] = m_trialLog;

    QJsonObject summary;
    summary["avgDetectionRate"] = nSignals > 0 ? sumDetectionRate / nSignals : 0.0;
    summary["avgDetectionRateOverride"] = nSignals > 0 ? sumDetectionRateOverride / nSignals : 0.0;
    summary["note"] = QStringLiteral(
        "avgDetectionRateOverride uwzglednia hybrydowy override klasyczny "
        "(Kierunek B) - jesli LLM zaproponuje skalar dla bajtu wygladajacego "
        "jak flagi bitowe (na podstawie obserwowanych wartosci, nie ground "
        "truth), kod programowo wymusza dekompozycje per-bit przed ocena. "
        "avgDetectionRate (bez sufiksu) to nadal 'surowy' wynik samego LLM, "
        "porownywalny z wczesniejszymi raportami.");
    report["summary"] = summary;

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
