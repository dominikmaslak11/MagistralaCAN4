#pragma once
#include "CanFrame.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QString>
#include <vector>
#include <cstdint>

/**
 * @brief Backend LLM wspierany przez MagistralaCAN4.
 */
enum class LlmBackend {
    OpenAI,      // GPT-4o, GPT-4-turbo
    Anthropic,   // Claude 3.5 Sonnet, Claude 3 Opus
    DeepSeek     // DeepSeek-V3, DeepSeek-R1
};

/**
 * @brief Konfiguracja pojedynczego backendu LLM.
 */
struct LlmConfig {
    LlmBackend backend   = LlmBackend::OpenAI;
    QString    apiKey;                   // token API
    QString    model;                    // np. "gpt-4o", "claude-3-5-sonnet-20241022"
    QString    endpoint;                 // pełny URL (z defaultem per backend)
    int        maxTokens    = 1024;
    double     temperature  = 0.3;       // niska temperatura = deterministyczne odpowiedzi
    int        timeoutMs    = 120000;    // 2 minuty (LLM może długo myśleć)
};

/**
 * @brief Zapytanie do LLM — kontekst CAN + prompt.
 */
struct LlmQuery {
    uint32_t             canId = 0;
    CanFrame             triggerFrame;          // ramka wyzwalająca Cold Start
    std::vector<CanFrame> recentFrames;         // ostatnie N ramek dla tego ID
    QString              systemPrompt;          // instrukcja systemowa
    QString              userPrompt;            // treść zapytania
};

/**
 * @brief Odpowiedź z LLM.
 */
struct LlmResponse {
    QString  ruleText;           // sugerowana reguła / interpretacja
    QString  rawJson;            // surowa odpowiedź API
    uint64_t tQueryStartUs = 0; // timestamp wysłania zapytania (μs)
    uint64_t tQueryEndUs   = 0; // timestamp otrzymania odpowiedzi (μs)
    uint64_t tLlmMs() const;    // t_llm [ms]
    bool     success = false;
    QString  errorMsg;
};

/**
 * @brief Klient HTTP do API różnych dostawców LLM.
 *
 * Używa QNetworkAccessManager (już w Qt6::Network). Wysyła asynchroniczne
 * zapytania POST z kontekstem ramek CAN i mierzy czas wnioskowania (t_llm).
 *
 * Obsługuje trzy backendy:
 *   - OpenAI     (api.openai.com/v1/chat/completions)
 *   - Anthropic   (api.anthropic.com/v1/messages)
 *   - DeepSeek    (api.deepseek.com/v1/chat/completions — kompatybilny z OpenAI)
 */
class LlmQueryClient : public QObject {
    Q_OBJECT
public:
    explicit LlmQueryClient(QObject *parent = nullptr);

    /// Ustawia konfigurację dla danego backendu.
    void setConfig(const LlmConfig &config);

    /// Zwraca aktualną konfigurację.
    [[nodiscard]] const LlmConfig &config() const { return m_config; }

    /// Ustawia aktualny backend.
    void setBackend(LlmBackend backend);

    /// Wysyła zapytanie do LLM (asynchronicznie).
    /// Odpowiedź przychodzi przez sygnał llmResponseReceived().
    void query(const LlmQuery &query);

    /// Anuluje trwające zapytanie.
    void cancel();

    /// Czy zapytanie jest w toku.
    [[nodiscard]] bool isBusy() const { return m_busy; }

signals:
    /// Emitowany po otrzymaniu odpowiedzi z LLM.
    void llmResponseReceived(const LlmResponse &response);

    /// Emitowany przy błędzie sieciowym / timeout.
    void llmError(const QString &errorMsg);

private slots:
    void onReplyFinished();

private:
    [[nodiscard]] QJsonObject buildOpenAiBody(const LlmQuery &query) const;
    [[nodiscard]] QJsonObject buildAnthropicBody(const LlmQuery &query) const;
    [[nodiscard]] QJsonObject buildDeepSeekBody(const LlmQuery &query) const;

    [[nodiscard]] QString extractTextFromOpenAi(const QJsonObject &json) const;
    [[nodiscard]] QString extractTextFromAnthropic(const QJsonObject &json) const;
    [[nodiscard]] QString extractTextFromDeepSeek(const QJsonObject &json) const;

    [[nodiscard]] static QString formatFrameList(const std::vector<CanFrame> &frames,
                                                  int maxFrames = 20);
    [[nodiscard]] static uint64_t nowUs();

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply         *m_reply = nullptr;
    QElapsedTimer          m_timer;
    LlmConfig              m_config;
    LlmQuery               m_currentQuery;
    bool                   m_busy = false;
};
