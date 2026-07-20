#pragma once
#include <QObject>
#include <QTimer>
#include <QVector>
#include <QHash>
#include <QSet>
#include "CanFrame.h"

/**
 * @brief Odtwarzacz nagrań CAN — wczytuje .mcan / .mcan.zst i emituje ramki
 *        z oryginalnym timingiem (lub przyspieszone).
 *
 * Użycie:
 *   player.loadFile("sesja.mcan.zst");
 *   connect(&player, &CanPlayer::frameEmitted, ...);
 *   player.play();
 */
class CanPlayer : public QObject {
    Q_OBJECT
public:
    explicit CanPlayer(QObject *parent = nullptr);

    /// Wczytuje plik .mcan lub .mcan.zst.
    /// Zwraca liczbę wczytanych ramek (0 = błąd).
    int loadFile(const QString &path);

    /// Rozpoczyna / wznawia odtwarzanie.
    void play();
    /// Pauzuje odtwarzanie.
    void pause();
    /// Zatrzymuje i przewija na początek.
    void stop();

    bool isPlaying() const { return m_playing; }
    bool isPaused() const  { return m_loaded && !m_playing; }
    int totalFrames() const { return static_cast<int>(m_frames.size()); }
    int frameCount()  const { return static_cast<int>(m_frames.size()); }
    int currentFrame() const { return m_currentIndex; }
    CanFrame frameAt(int idx) const { return (idx >= 0 && idx < m_frames.size()) ? m_frames[idx] : CanFrame{}; }
    /// Zwraca override jeśli istnieje dla idx, inaczej oryginał.
    CanFrame effectiveFrameAt(int idx) const {
        if (m_overrides.contains(idx)) return m_overrides[idx];
        return frameAt(idx);
    }

    /// Ustawia mnożnik prędkości (0.5 = pół, 2.0 = podwójna, 0 = max).
    void setSpeed(float multiplier);

    /// Przeskakuje do ramki o podanym indeksie (bez wznowienia odtwarzania).
    void seekTo(int idx);

    // ── Override/skip API (dla CanReplayEditorWidget) ──────────────────
    /// Zastępuje ramkę pod indeksem idx zmodyfikowaną kopią.
    void setOverride(int idx, const CanFrame &modified);
    /// Usuwa override dla indeksu idx (powrót do oryginału).
    void clearOverride(int idx);
    /// Usuwa wszystkie overrides i skip-i.
    void clearAllOverrides();
    /// Oznacza ramkę do pominięcia podczas odtwarzania.
    void setSkipped(int idx, bool skip);
    bool isSkipped(int idx) const;
    bool hasOverride(int idx) const;

signals:
    void frameEmitted(const CanFrame &frame);
    void positionChanged(int index, int total);
    void playbackFinished();

private slots:
    void emitNextFrame();

private:
    QVector<CanFrame> m_frames;
    QTimer m_timer;
    int m_currentIndex = 0;
    float m_speed = 1.0f;
    bool m_loaded = false;
    bool m_playing = false;
    uint64_t m_baseTimestamp = 0;  // pierwszy timestamp w nagraniu

    QHash<int, CanFrame> m_overrides;
    QSet<int>            m_skippedIndices;
};