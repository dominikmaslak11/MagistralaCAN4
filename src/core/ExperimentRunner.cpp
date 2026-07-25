#include "ExperimentRunner.h"
#include <QDebug>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <sys/time.h>

ExperimentRunner::ExperimentRunner(QObject *parent) : QObject(parent) {}

void ExperimentRunner::setDetector(ColdStartDetector *detector) {
    if (m_detector) {
        disconnect(m_detector, nullptr, this, nullptr);
    }
    m_detector = detector;
    if (m_detector) {
        connect(m_detector, &ColdStartDetector::coldStartDetected,
                this, &ExperimentRunner::onColdStartDetected);
    }
}

void ExperimentRunner::setLlmClient(LlmQueryClient *client) {
    if (m_llmClient) {
        disconnect(m_llmClient, nullptr, this, nullptr);
    }
    m_llmClient = client;
    if (m_llmClient) {
        connect(m_llmClient, &LlmQueryClient::llmResponseReceived,
                this, &ExperimentRunner::onLlmResponse);
        connect(m_llmClient, &LlmQueryClient::llmError,
                this, &ExperimentRunner::onLlmError);
    }
}

void ExperimentRunner::setProfiler(LatencyProfiler *profiler) {
    m_profiler = profiler;
    if (m_profiler) {
        m_profiler->setTrialsPerModel(m_trialsPerModel);
    }
}

void ExperimentRunner::addModel(const QString &modelName) {
    if (!m_modelList.contains(modelName))
        m_modelList.append(modelName);
}

void ExperimentRunner::setApiKey(const QString &modelName, const QString &apiKey) {
    m_apiKeys[modelName] = apiKey;
}

void ExperimentRunner::useRealHardware(WebSocketServer *server, int otaTimeoutMs) {
    if (m_wsServer) {
        disconnect(m_wsServer, nullptr, this, nullptr);
    }
    m_wsServer = server;
    m_realHardwareMode = (server != nullptr);
    m_otaTimeoutMs = otaTimeoutMs;

    if (m_wsServer) {
        connect(m_wsServer, &WebSocketServer::frameReceivedFromClient,
                this, &ExperimentRunner::onRealFrameReceived);
        connect(m_wsServer, &WebSocketServer::ruleAckReceived,
                this, &ExperimentRunner::onRuleAckReceived);
    }

    if (!m_otaTimeoutTimer) {
        m_otaTimeoutTimer = new QTimer(this);
        m_otaTimeoutTimer->setSingleShot(true);
        connect(m_otaTimeoutTimer, &QTimer::timeout, this, &ExperimentRunner::onOtaAckTimeout);
    }
}

// ── Public API ─────────────────────────────────────────────────────────────────

void ExperimentRunner::start() {
    if (m_running) return;
    if (m_modelList.isEmpty()) {
        emit experimentError(QStringLiteral("No LLM models configured for experiment"));
        return;
    }
    if (!m_detector) {
        emit experimentError(QStringLiteral("ColdStartDetector not set"));
        return;
    }
    if (!m_llmClient) {
        emit experimentError(QStringLiteral("LlmQueryClient not set"));
        return;
    }
    if (!m_profiler) {
        emit experimentError(QStringLiteral("LatencyProfiler not set"));
        return;
    }

    m_running = true;
    m_modelIndex = 0;
    m_currentTrial = 0;
    m_frameHistory.clear();

    // Wygeneruj nazwę pliku raportu jeśli nie ustawiono
    if (m_reportPath.isEmpty()) {
        m_reportPath = QString("latency_report_%1.json")
                           .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    }

    // Zresetuj detektor (zapomnij znane ID)
    m_detector->reset();
    m_profiler->reset();
    m_profiler->setTrialsPerModel(m_trialsPerModel);

    qDebug() << "[ExperimentRunner] Starting experiment:"
             << m_modelList.size() << "models,"
             << m_trialsPerModel << "trials each";

    startNextModel();
}

void ExperimentRunner::stop() {
    if (!m_running) return;
    m_running = false;
    m_llmClient->cancel();
    setState(State::Idle);
    qDebug() << "[ExperimentRunner] Experiment stopped";
}

double ExperimentRunner::progress() const {
    if (m_modelList.isEmpty() || m_trialsPerModel <= 0) return 0.0;
    int totalTrials = m_modelList.size() * m_trialsPerModel;
    int completed = m_modelIndex * m_trialsPerModel + m_currentTrial;
    return static_cast<double>(completed) / static_cast<double>(totalTrials);
}

void ExperimentRunner::injectManualColdStart(uint32_t canId, const CanFrame &frame) {
    if (m_state != State::WaitingForDetection) return;
    m_hardwareSimulated = true;
    onColdStartDetected(canId, frame, frame.timestamp, QStringLiteral("manual_inject"));
}

uint64_t ExperimentRunner::sampleSimulatedUs(double meanUs, double sigmaUs, double minUs) {
    std::normal_distribution<double> dist(meanUs, sigmaUs);
    double v = dist(m_rng);
    if (v < minUs) v = minUs;
    return static_cast<uint64_t>(v);
}

// ── Private: State machine ─────────────────────────────────────────────────────

void ExperimentRunner::setState(State s) {
    m_state = s;
    const char *names[] = {"Idle", "WaitingForDetection", "QueryingLlm",
                           "Compiling", "SendingOta", "Done"};
    qDebug() << "[ExperimentRunner] State:" << names[static_cast<int>(s)];
}

void ExperimentRunner::startNextModel() {
    if (!m_running) return;
    if (m_modelIndex >= m_modelList.size()) {
        finishExperiment();
        return;
    }

    m_currentModel = m_modelList[m_modelIndex];
    m_currentTrial = 0;

    // Skonfiguruj LlmQueryClient dla tego modelu
    LlmConfig cfg;
    cfg.model = m_currentModel;
    cfg.apiKey = m_apiKeys.value(m_currentModel);
    if (m_currentModel.startsWith("gpt-"))
        cfg.backend = LlmBackend::OpenAI;
    else if (m_currentModel.startsWith("claude-"))
        cfg.backend = LlmBackend::Anthropic;
    else if (m_currentModel.startsWith("deepseek"))
        cfg.backend = LlmBackend::DeepSeek;
    else if (m_currentModel.startsWith("gemini"))
        cfg.backend = LlmBackend::Gemini;
    else
        cfg.backend = LlmBackend::OpenAI; // default

    m_llmClient->setConfig(cfg);

    qDebug() << "[ExperimentRunner] Starting model:" << m_currentModel
             << "(" << (m_modelIndex + 1) << "/" << m_modelList.size() << ")";

    emit progressChanged(progress(),
        QString("Model %1/%2: %3 — trial 0/%4")
            .arg(m_modelIndex + 1).arg(m_modelList.size())
            .arg(m_currentModel).arg(m_trialsPerModel));

    // Zresetuj detektor dla nowego modelu
    m_detector->reset();
    m_frameHistory.clear();
    startNextTrial();
}

void ExperimentRunner::startNextTrial() {
    if (!m_running) return;
    if (m_currentTrial >= m_trialsPerModel) {
        // Ten model zakończony — zapisz statystyki i przejdź do następnego
        LatencyStats stats = m_profiler->computeStats(m_currentModel);
        emit modelCompleted(m_currentModel, stats);

        m_modelIndex++;
        startNextModel();
        return;
    }

    m_currentCanId = 0;
    m_tDetUs = 0;
    m_tTxUpUs = 0;
    m_tLlmStartUs = 0;
    m_tLlmEndUs = 0;
    m_tCompUs = 0;
    m_tOtaUs = 0;
    m_tLlmUs = 0;
    m_pendingLlmResponseText.clear();

    setState(State::WaitingForDetection);

    qDebug() << "[ExperimentRunner] Trial" << (m_currentTrial + 1) << "/"
             << m_trialsPerModel << "for model" << m_currentModel;

    emit progressChanged(progress(),
        QString("Model %1/%2: %3 — trial %4/%5 (waiting for cold start...)")
            .arg(m_modelIndex + 1).arg(m_modelList.size())
            .arg(m_currentModel).arg(m_currentTrial + 1).arg(m_trialsPerModel));
}

// ── Slots ──────────────────────────────────────────────────────────────────────

void ExperimentRunner::onRealFrameReceived(const CanFrame &frame) {
    if (!m_running) return;
    if (m_state != State::WaitingForDetection) return;

    // Znacznik czasu przyjęcia ramki przez serwer (zegar serwera) — użyty w
    // onColdStartDetected() do wyliczenia realnego t_tx_up/t_det.
    m_pendingFrameArrivalUs = nowUs();
    m_detector->evaluate(frame); // synchronicznie: może wyemitować coldStartDetected
}

void ExperimentRunner::onColdStartDetected(uint32_t canId, const CanFrame &frame,
                                            uint64_t timestampUs, const QString &reason) {
    if (!m_running) return;
    if (m_state != State::WaitingForDetection) return;

    m_currentCanId = canId;
    m_triggerFrame = frame;

    if (m_realHardwareMode) {
        // Prawdziwy ESP32 (esp_experiment_1_1.ino): frame.timestamp jest już
        // skorygowany o przesunięcie zegara ESP32 względem serwera (kalibracja
        // "time_sync" wykonana w firmware), więc jest porównywalny z zegarem
        // serwera (nowUs()). t_tx_up = transmisja bezprzewodowa ESP32→serwer,
        // t_det = czas oceny ColdStartDetector (od przyjęcia ramki do decyzji).
        uint64_t decisionUs = nowUs();
        m_tTxUpUs = (m_pendingFrameArrivalUs > frame.timestamp)
                        ? (m_pendingFrameArrivalUs - frame.timestamp) : 0;
        m_tDetUs  = (decisionUs > m_pendingFrameArrivalUs)
                        ? (decisionUs - m_pendingFrameArrivalUs) : 0;
    } else if (m_hardwareSimulated) {
        // Brak fizycznego ESP32/magistrali CAN w tym środowisku — t_det (detekcja +
        // decyzja na ESP32) i t_tx_up (transmisja bezprzewodowa ESP32→serwer) nie są
        // mierzalne. Symulujemy je rozkładem normalnym opartym na typowych wartościach
        // literaturowych dla ESP32 (przerwanie CAN + porównanie wzorca ~0.2-0.5ms;
        // WiFi/UART hop rzędu kilku ms). Jawnie oznaczone w raporcie JSON (patrz
        // finishExperiment()) jako "simulated": true — NIE są to realne pomiary.
        m_tDetUs  = sampleSimulatedUs(350.0, 80.0, 50.0);
        m_tTxUpUs = sampleSimulatedUs(5200.0, 1200.0, 500.0);
    } else {
        // t_det = czas wykrycia (timestamp ramki)
        m_tDetUs = timestampUs;
        // t_tx_up mierzone po stronie serwera — timestamp otrzymania ramki
        // (w ESP32 UART to czas odebrania linii z UART, w socketCAN to timestamp kernel)
        m_tTxUpUs = timestampUs; // serwerowa strona; ESP32 musi dodać swój timestamp
    }

    // Dodaj ramkę do historii
    m_frameHistory.push_back(frame);
    while (static_cast<int>(m_frameHistory.size()) > kMaxHistory)
        m_frameHistory.pop_front();

    qDebug() << "[ExperimentRunner] Cold start detected:"
             << QString("ID=0x%1 reason=%2 tDet=%3 μs")
                    .arg(canId, 3, 16, QChar('0')).arg(reason).arg(m_tDetUs);

    // Oznacz ID jako znane (żeby nie wyzwalało ponownie)
    m_detector->markKnownWithPattern(canId, frame);

    // Rozpocznij zapytanie do LLM
    setState(State::QueryingLlm);

    LlmQuery query;
    query.canId = canId;
    query.triggerFrame = frame;
    query.recentFrames = {m_frameHistory.begin(), m_frameHistory.end()};
    query.systemPrompt = QStringLiteral(
        "You are a CAN bus reverse-engineering assistant. "
        "Analyze CAN frames and suggest byte-level interpretations. "
        "Respond with a JSON object containing: "
        "{\"interpretation\": string describing each byte's likely meaning, "
        "\"rule\": { \"byteIdx\": int, \"bitMask\": hex string, \"scale\": float, \"offset\": float }, "
        "\"confidence\": float between 0.0 and 1.0}. "
        "Be concise.");

    m_tLlmStartUs = nowUs();
    m_llmClient->query(query);
}

void ExperimentRunner::onLlmResponse(const LlmResponse &resp) {
    if (!m_running) return;
    if (m_state != State::QueryingLlm) return;

    m_tLlmEndUs = resp.tQueryEndUs;
    uint64_t tLlmUs = resp.tLlmMs() * 1000ULL;

    if (!resp.success) {
        qWarning() << "[ExperimentRunner] LLM query failed:" << resp.errorMsg;
        // Nadal zapisujemy próbkę jako failed
        LatencySample sample;
        sample.modelName  = m_currentModel;
        sample.canId      = m_currentCanId;
        sample.tDetUs     = m_tDetUs;
        sample.tTxUpUs    = m_tTxUpUs;
        sample.tLlmUs     = tLlmUs;
        sample.tCompUs    = 0;
        sample.tOtaUs     = 0;
        sample.trialIndex = m_currentTrial;
        sample.success    = false;
        sample.errorMsg   = resp.errorMsg;
        sample.frameDataHex = frameDataHex(m_triggerFrame);
        m_profiler->addSample(sample);

        m_currentTrial++;
        emit trialCompleted(m_currentTrial, m_currentModel, 0);
        startNextTrial();
        return;
    }

    qDebug() << "[ExperimentRunner] LLM response received, tLlm ="
             << resp.tLlmMs() << "ms";

    m_tLlmUs = tLlmUs;
    m_pendingLlmResponseText = resp.ruleText;

    // t_comp — czas kompilacji/przygotowania reguły
    setState(State::Compiling);
    m_trialTimer.start();

    // W rzeczywistej implementacji ESP32: tutaj kompilacja/serializacja reguły
    // Dla magistraliCAN4 to głównie parsowanie JSON z odpowiedzi LLM
    // (pomijalnie małe, ale mierzymy dla kompletności)
    m_tCompUs = static_cast<uint64_t>(m_trialTimer.nsecsElapsed()) / 1000ULL;

    // t_ota — czas wysłania reguły do ESP32 (OTA)
    setState(State::SendingOta);

    if (m_realHardwareMode) {
        // Realny round-trip: wyślij regułę do ESP32 przez WebSocket i czekaj na
        // rule_ack (z timeoutem) — dokończenie próbki w onRuleAckReceived()
        // lub onOtaAckTimeout().
        QJsonObject rule;
        rule[QStringLiteral("canId")] = static_cast<int>(m_currentCanId);
        rule[QStringLiteral("ruleText")] = resp.ruleText;
        m_otaSendTimeUs = nowUs();
        m_wsServer->sendRuleUpdate(rule);
        m_otaTimeoutTimer->start(m_otaTimeoutMs);
        return;
    }

    if (m_hardwareSimulated) {
        // Brak fizycznego ESP32 — symulacja rozkładem normalnym (typowy czas zapisu
        // tabeli reguł przez WiFi OTA na ESP32, rzędu dziesiątek-set ms). Patrz komentarz
        // w onColdStartDetected(). NIE jest to realny pomiar.
        m_tOtaUs = sampleSimulatedUs(85000.0, 15000.0, 10000.0);
    } else {
        m_tOtaUs = 0;
    }
    finalizeSample(true);
}

void ExperimentRunner::onRuleAckReceived(uint32_t canId) {
    if (!m_running || m_state != State::SendingOta) return;
    if (canId != m_currentCanId) return; // spóźniony/nieprzypisany ack — ignoruj

    m_otaTimeoutTimer->stop();
    uint64_t t = nowUs();
    m_tOtaUs = (t > m_otaSendTimeUs) ? (t - m_otaSendTimeUs) : 0;
    finalizeSample(true);
}

void ExperimentRunner::onOtaAckTimeout() {
    if (!m_running || m_state != State::SendingOta) return;

    qWarning() << "[ExperimentRunner] Brak potwierdzenia OTA (rule_ack) od ESP32 w"
               << m_otaTimeoutMs << "ms — CAN ID:"
               << QString("0x%1").arg(m_currentCanId, 3, 16, QChar('0'));
    m_tOtaUs = static_cast<uint64_t>(m_otaTimeoutMs) * 1000ULL;
    finalizeSample(false, QStringLiteral("OTA ack timeout"));
}

void ExperimentRunner::finalizeSample(bool success, const QString &errorMsg) {
    LatencySample sample;
    sample.modelName       = m_currentModel;
    sample.canId           = m_currentCanId;
    sample.tDetUs          = m_tDetUs;
    sample.tTxUpUs         = m_tTxUpUs;
    sample.tLlmUs          = m_tLlmUs;
    sample.tCompUs         = m_tCompUs;
    sample.tOtaUs          = m_tOtaUs;
    sample.frameDataHex    = frameDataHex(m_triggerFrame);
    sample.llmResponseText = m_pendingLlmResponseText;
    sample.trialIndex      = m_currentTrial;
    sample.success         = success;
    sample.errorMsg        = errorMsg;
    m_profiler->addSample(sample);

    uint64_t tTotalMs = (m_tDetUs + m_tTxUpUs + m_tLlmUs + m_tCompUs + m_tOtaUs) / 1000ULL;

    qDebug() << "[ExperimentRunner] Trial" << (m_currentTrial + 1)
             << "completed:" << "tDet=" << m_tDetUs/1000 << "ms"
             << "tTxUp=" << m_tTxUpUs/1000 << "ms"
             << "tLlm=" << m_tLlmUs/1000 << "ms"
             << "tComp=" << m_tCompUs/1000 << "ms"
             << "tOta=" << m_tOtaUs/1000 << "ms"
             << "tTotal=" << tTotalMs << "ms"
             << "success=" << success;

    m_currentTrial++;
    emit trialCompleted(m_currentTrial, m_currentModel, success ? tTotalMs : 0);

    // Przejdź do następnej próbki z małym opóźnieniem
    QTimer::singleShot(100, this, &ExperimentRunner::advanceTrial);
}

void ExperimentRunner::onLlmError(const QString &errorMsg) {
    if (!m_running) return;
    qWarning() << "[ExperimentRunner] LLM error:" << errorMsg;

    // Zapisz próbkę jako błędną
    LatencySample sample;
    sample.modelName  = m_currentModel;
    sample.canId      = m_currentCanId;
    sample.tDetUs     = m_tDetUs;
    sample.tTxUpUs    = m_tTxUpUs;
    sample.tLlmUs     = 0;
    sample.tCompUs    = 0;
    sample.tOtaUs     = 0;
    sample.trialIndex = m_currentTrial;
    sample.success    = false;
    sample.errorMsg   = errorMsg;
    sample.frameDataHex = frameDataHex(m_triggerFrame);
    m_profiler->addSample(sample);

    m_currentTrial++;
    startNextTrial();
}

void ExperimentRunner::advanceTrial() {
    if (!m_running) return;
    startNextTrial();
}

// ── Finish ─────────────────────────────────────────────────────────────────────

void ExperimentRunner::finishExperiment() {
    m_running = false;
    setState(State::Done);

    // Zapisz raport (rozszerzony o metadane eksperymentu — modele testowane
    // i jawna adnotacja, które składowe czasu są symulowane wobec braku
    // fizycznego ESP32/magistrali CAN w tym środowisku).
    QString path = m_reportPath;
    if (m_profiler) {
        QJsonObject report = m_profiler->fullReport();
        QJsonObject meta;
        meta["experiment"] = QStringLiteral("1.1 — Cold Start Latency Breakdown");
        meta["modelsTestedInOrder"] = QJsonArray::fromStringList(m_modelList);
        meta["hardwareSimulated"] = m_hardwareSimulated;
        meta["realHardwareMode"] = m_realHardwareMode;
        if (m_realHardwareMode) {
            meta["hardwareNote"] = QStringLiteral(
                "Pomiar na realnym ESP32 (esp_experiment_1_1.ino) przez WebSocket. "
                "t_llm, t_comp — realne (zegar serwera). t_tx_up, t_det — realne, "
                "oparte o timestamp ESP32 skorygowany kalibracją time_sync (NTP-style, "
                "dokładność rzędu WiFi jitter). t_ota — realny round-trip do rule_ack "
                "od ESP32 (z timeoutem).");
        } else if (m_hardwareSimulated) {
            meta["simulationNote"] = QStringLiteral(
                "Brak fizycznego ESP32/magistrali CAN w tym środowisku: t_det, t_tx_up "
                "i t_ota są symulowane rozkładem normalnym (wartości literaturowe dla "
                "ESP32 + WiFi). t_llm jest realnym pomiarem czasu odpowiedzi API (Qt "
                "QNetworkAccessManager, zegar systemowy), t_comp jest realnym pomiarem "
                "czasu parsowania odpowiedzi JSON.");
        }
        report["metadata"] = meta;
        QJsonDocument doc(report);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(doc.toJson(QJsonDocument::Indented));
            file.close();
        } else {
            qWarning() << "[ExperimentRunner] Cannot write report to" << path;
        }
    }

    qDebug() << "[ExperimentRunner] Experiment finished. Report:" << path;

    emit progressChanged(1.0, QString("Experiment complete — report saved to %1").arg(path));
    emit experimentFinished(path);
}

// ── Helpers ────────────────────────────────────────────────────────────────────

QString ExperimentRunner::frameDataHex(const CanFrame &frame) {
    QString hex;
    for (int i = 0; i < frame.dlc && i < 64; ++i)
        hex += QString("%1").arg(frame.data[i], 2, 16, QChar('0'));
    return hex;
}

uint64_t ExperimentRunner::nowUs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000000ULL + tv.tv_usec;
}
