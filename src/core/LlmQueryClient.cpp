#include "LlmQueryClient.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <sys/time.h>

// ── LlmResponse ────────────────────────────────────────────────────────────────

uint64_t LlmResponse::tLlmMs() const {
    if (tQueryEndUs <= tQueryStartUs) return 0;
    return (tQueryEndUs - tQueryStartUs) / 1000ULL;
}

// ── LlmQueryClient ─────────────────────────────────────────────────────────────

LlmQueryClient::LlmQueryClient(QObject *parent) : QObject(parent) {
    m_nam = new QNetworkAccessManager(this);
}

void LlmQueryClient::setConfig(const LlmConfig &config) {
    m_config = config;
    // Ustaw domyślny endpoint jeśli nie podano
    if (m_config.endpoint.isEmpty()) {
        switch (m_config.backend) {
        case LlmBackend::OpenAI:
            m_config.endpoint = QStringLiteral("https://api.openai.com/v1/chat/completions");
            if (m_config.model.isEmpty()) m_config.model = QStringLiteral("gpt-4o");
            break;
        case LlmBackend::Anthropic:
            m_config.endpoint = QStringLiteral("https://api.anthropic.com/v1/messages");
            if (m_config.model.isEmpty()) m_config.model = QStringLiteral("claude-3-5-sonnet-20241022");
            break;
        case LlmBackend::DeepSeek:
            m_config.endpoint = QStringLiteral("https://api.deepseek.com/v1/chat/completions");
            if (m_config.model.isEmpty()) m_config.model = QStringLiteral("deepseek-chat");
            break;
        }
    }
}

void LlmQueryClient::setBackend(LlmBackend backend) {
    m_config.backend = backend;
    m_config.endpoint.clear(); // wymusi rekonfigurację endpointu
    m_config.model.clear();
    setConfig(m_config);
}

void LlmQueryClient::query(const LlmQuery &query) {
    if (m_busy) {
        emit llmError(QStringLiteral("LLM query already in progress"));
        return;
    }
    if (m_config.apiKey.isEmpty()) {
        emit llmError(QStringLiteral("LLM API key not configured"));
        return;
    }

    m_currentQuery = query;
    m_busy = true;

    // Zbuduj body w zależności od backendu
    QJsonObject body;
    switch (m_config.backend) {
    case LlmBackend::OpenAI:    body = buildOpenAiBody(query);    break;
    case LlmBackend::Anthropic: body = buildAnthropicBody(query); break;
    case LlmBackend::DeepSeek:  body = buildDeepSeekBody(query);  break;
    }

    QNetworkRequest req(QUrl(m_config.endpoint));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(m_config.timeoutMs);

    // Nagłówki autoryzacji per backend
    switch (m_config.backend) {
    case LlmBackend::OpenAI:
    case LlmBackend::DeepSeek:
        req.setRawHeader("Authorization", ("Bearer " + m_config.apiKey).toUtf8());
        break;
    case LlmBackend::Anthropic:
        req.setRawHeader("x-api-key", m_config.apiKey.toUtf8());
        req.setRawHeader("anthropic-version", "2023-06-01");
        break;
    }

    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    qDebug() << "[LlmQueryClient] Sending query to" << m_config.endpoint
             << "model:" << m_config.model
             << "payload size:" << payload.size() << "bytes";

    m_timer.start();
    uint64_t tStart = nowUs();

    m_reply = m_nam->post(req, payload);
    // Przekazujemy tStart przez QObject property (nie można dodać pól do lambdy w QObject::connect z kontekstem this)
    m_reply->setProperty("tQueryStartUs", QVariant::fromValue(static_cast<qulonglong>(tStart)));

    connect(m_reply, &QNetworkReply::finished, this, &LlmQueryClient::onReplyFinished);
}

void LlmQueryClient::cancel() {
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_busy = false;
}

// ── Slot ───────────────────────────────────────────────────────────────────────

void LlmQueryClient::onReplyFinished() {
    if (!m_reply) return;

    LlmResponse resp;
    resp.tQueryStartUs = static_cast<uint64_t>(m_reply->property("tQueryStartUs").value<qulonglong>());
    resp.tQueryEndUs   = nowUs();

    resp.rawJson = QString::fromUtf8(m_reply->readAll());

    if (m_reply->error() != QNetworkReply::NoError) {
        resp.success = false;
        resp.errorMsg = m_reply->errorString();
        qWarning() << "[LlmQueryClient] Network error:" << resp.errorMsg;
    } else {
        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(resp.rawJson.toUtf8(), &parseErr);
        if (parseErr.error != QJsonParseError::NoError) {
            resp.success = false;
            resp.errorMsg = QStringLiteral("JSON parse error: ") + parseErr.errorString();
        } else {
            QJsonObject json = doc.object();
            switch (m_config.backend) {
            case LlmBackend::OpenAI:    resp.ruleText = extractTextFromOpenAi(json);    break;
            case LlmBackend::Anthropic: resp.ruleText = extractTextFromAnthropic(json); break;
            case LlmBackend::DeepSeek:  resp.ruleText = extractTextFromDeepSeek(json);  break;
            }
            resp.success = !resp.ruleText.isEmpty();
            if (!resp.success)
                resp.errorMsg = QStringLiteral("Empty response from LLM");
        }
    }

    qDebug() << "[LlmQueryClient] Response received, t_llm ="
             << resp.tLlmMs() << "ms, success =" << resp.success;

    m_reply->deleteLater();
    m_reply = nullptr;
    m_busy = false;

    emit llmResponseReceived(resp);
}

// ── Build request bodies ───────────────────────────────────────────────────────

QJsonObject LlmQueryClient::buildOpenAiBody(const LlmQuery &query) const {
    QJsonArray messages;

    // System prompt
    if (!query.systemPrompt.isEmpty()) {
        QJsonObject sys;
        sys["role"] = "system";
        sys["content"] = query.systemPrompt;
        messages.append(sys);
    }

    // User prompt (CAN context + pytanie)
    QString userContent = query.userPrompt;
    if (userContent.isEmpty()) {
        // Domyślny prompt jeśli nie podano
        userContent = QStringLiteral(
            "Analyze the following CAN frame and suggest an interpretation:\n\n"
            "CAN ID: 0x%1 (DLC: %2)\n"
            "Data bytes: %3\n\n"
            "Recent frames for this ID:\n%4\n\n"
            "Please provide:\n"
            "1. Likely meaning of each byte (e.g., RPM, temperature, counter)\n"
            "2. Suggested decoding rule (byte index, bit mask, scale, offset)\n"
            "3. Confidence level (0.0-1.0)\n"
            "Respond in JSON format with keys: interpretation, rule, confidence.")
            .arg(query.canId, 3, 16, QChar('0'))
            .arg(query.triggerFrame.dlc)
            .arg(formatFrameList({query.triggerFrame}, 1))
            .arg(formatFrameList(query.recentFrames));
    }

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = userContent;
    messages.append(userMsg);

    QJsonObject body;
    body["model"] = m_config.model;
    body["messages"] = messages;
    body["max_tokens"] = m_config.maxTokens;
    body["temperature"] = m_config.temperature;
    return body;
}

QJsonObject LlmQueryClient::buildAnthropicBody(const LlmQuery &query) const {
    QString userContent = query.userPrompt;
    if (userContent.isEmpty()) {
        userContent = QStringLiteral(
            "Analyze the following CAN frame and suggest an interpretation:\n\n"
            "CAN ID: 0x%1 (DLC: %2)\n"
            "Data bytes: %3\n\n"
            "Recent frames for this ID:\n%4")
            .arg(query.canId, 3, 16, QChar('0'))
            .arg(query.triggerFrame.dlc)
            .arg(formatFrameList({query.triggerFrame}, 1))
            .arg(formatFrameList(query.recentFrames));
    }

    QJsonArray messages;
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = userContent;
    messages.append(userMsg);

    QJsonObject body;
    body["model"] = m_config.model;
    body["max_tokens"] = m_config.maxTokens;
    body["messages"] = messages;
    if (!query.systemPrompt.isEmpty())
        body["system"] = query.systemPrompt;
    body["temperature"] = m_config.temperature;
    return body;
}

QJsonObject LlmQueryClient::buildDeepSeekBody(const LlmQuery &query) const {
    // DeepSeek używa formatu kompatybilnego z OpenAI
    return buildOpenAiBody(query);
}

// ── Extract text from responses ────────────────────────────────────────────────

QString LlmQueryClient::extractTextFromOpenAi(const QJsonObject &json) const {
    const QJsonArray choices = json["choices"].toArray();
    if (choices.isEmpty()) return {};
    return choices[0].toObject()["message"].toObject()["content"].toString().trimmed();
}

QString LlmQueryClient::extractTextFromAnthropic(const QJsonObject &json) const {
    const QJsonArray content = json["content"].toArray();
    if (content.isEmpty()) return {};
    // Anthropic zwraca tablicę bloków content[{type:"text", text:"..."}]
    QString text;
    for (const auto &block : content) {
        QJsonObject blockObj = block.toObject();
        if (blockObj["type"].toString() == "text")
            text += blockObj["text"].toString();
    }
    return text.trimmed();
}

QString LlmQueryClient::extractTextFromDeepSeek(const QJsonObject &json) const {
    // DeepSeek używa formatu kompatybilnego z OpenAI
    return extractTextFromOpenAi(json);
}

// ── Helpers ────────────────────────────────────────────────────────────────────

QString LlmQueryClient::formatFrameList(const std::vector<CanFrame> &frames,
                                         int maxFrames) {
    QString result;
    int count = 0;
    for (const auto &f : frames) {
        if (count >= maxFrames) break;
        if (count > 0) result += "\n";
        QString dataStr;
        for (int i = 0; i < f.dlc && i < 64; ++i)
            dataStr += QString("%1").arg(f.data[i], 2, 16, QChar('0'));
        result += QString("  ID=0x%1 DLC=%2 data=[%3] ts=%4")
                      .arg(f.id, 3, 16, QChar('0'))
                      .arg(f.dlc)
                      .arg(dataStr)
                      .arg(f.timestamp);
        count++;
    }
    if (static_cast<int>(frames.size()) > maxFrames)
        result += QString("\n  ... and %1 more frames").arg(frames.size() - maxFrames);
    return result;
}

uint64_t LlmQueryClient::nowUs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000000ULL + tv.tv_usec;
}
