#pragma once
#include <QString>
#include <QVector>
#include <cstdint>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QObject>
#include "CanFrame.h"

class CanSniffer;
class LuaScriptEngine;

/**
 * @brief Definicja pojedynczego symulowanego węzła CAN.
 *
 * Każdy węzeł ma warunek wyzwalający (trigger) na podstawie ID i opcjonalnie
 * danych ramki, oraz odpowiedź — statyczną lub generowaną przez skrypt Lua.
 */
struct CanNodeDefinition {
    QString name;
    uint32_t triggerId        = 0;
    uint32_t triggerIdMask    = 0xFFFFFFFF;   // które bity ID porównywać
    QVector<uint8_t> triggerData;             // oczekiwane bajty (puste = dowolne)
    QVector<uint8_t> triggerDataMask;         // maska bitowa dla bajtów

    // ID odpowiedzi (domyślnie triggerId + 1)
    uint32_t responseId = 0;

    // Odpowiedź statyczna (używana gdy responseLuaFunc jest pusty)
    QVector<uint8_t> staticResponse;

    // Odpowiedź dynamiczna – nazwa funkcji Lua (np. "onEngineTrigger")
    QString responseLuaFunc;

    int  responseDelayMs = 0;     // opóźnienie przed wysłaniem odpowiedzi
    bool enabled         = true;

    // Statystyki
    uint64_t matchCount    = 0;
    uint64_t responseCount = 0;

    [[nodiscard]] QJsonObject toJson() const;
    static CanNodeDefinition fromJson(const QJsonObject &obj);
};

/**
 * @brief Rdzeń symulatora węzłów CAN.
 *
 * Nasłuchuje ramek CAN, porównuje z triggerami zdefiniowanych węzłów
 * i wysyła odpowiedzi (statyczne lub przez Lua) po opcjonalnym opóźnieniu.
 */
class CanNodeSimulator : public QObject {
    Q_OBJECT
public:
    explicit CanNodeSimulator(QObject *parent = nullptr);

    void setSniffer(CanSniffer *sniffer)  { m_sniffer = sniffer; }
    void setLuaEngine(LuaScriptEngine *lua) { m_lua = lua; }

    QVector<CanNodeDefinition> &nodes()       { return m_nodes; }
    const QVector<CanNodeDefinition> &nodes() const { return m_nodes; }

    /// Ładuje/zapisuje konfigurację węzłów (JSON).
    bool loadConfig(const QString &path);
    bool saveConfig(const QString &path) const;

signals:
    /// Emitowany gdy symulator zbudował ramkę odpowiedzi (przed próbą wysłania przez sniffer).
    void frameReady(const CanFrame &frame);

public slots:
    /// Odbiera ramkę CAN i sprawdza triggery.
    void onNewFrame(const CanFrame &frame);

private slots:
    void sendPendingResponse();

private:
    bool matchTrigger(const CanNodeDefinition &node, const CanFrame &frame) const;
    void scheduleResponse(const CanNodeDefinition &node);

    struct PendingResponse {
        uint32_t id;
        QVector<uint8_t> data;
    };

    CanSniffer      *m_sniffer = nullptr;
    LuaScriptEngine *m_lua     = nullptr;
    QVector<CanNodeDefinition> m_nodes;
    QTimer          m_responseTimer;
    QVector<PendingResponse> m_pendingResponses;
};
