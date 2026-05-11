#pragma once
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QSlider>
#include <QCheckBox>
#include <QTimer>
#include <QVector>
#include <QListWidget>
#include "CanFrame.h"

class AssociativeLearner;
class LuaScriptEngine;
class CanSniffer;

class OfflineAnalyzer : public QWidget {
    Q_OBJECT
public:
    explicit OfflineAnalyzer(AssociativeLearner *learner,
                             LuaScriptEngine *lua = nullptr,
                             QWidget *parent = nullptr);

    void setSniffer(CanSniffer *sniffer) { m_sniffer = sniffer; }

public slots:
    void loadFile();
    void playPause();
    void stop();
    void setSpeed(int value);
    void nextFrame();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void startBinarySearch();
    void onBinarySearchResponse(bool phenomenonOccurred);

private:
    QVector<CanFrame> m_frames;
    int m_currentIndex = 0;
    bool m_playing = false;

    // ── Binary search state ────────────────────────────────
    enum class BsState { Idle, PlayingHalf, AwaitingResponse, Found };
    BsState m_bsState = BsState::Idle;
    int m_bsLeft = 0;
    int m_bsRight = 0;
    int m_bsMid = 0;
    int m_bsPlayEnd = 0;  // exclusive end index for current half
    bool m_bsPlayingFirstHalf = true;

    void bsPlayHalf();
    void bsContinue();

    // ── Frame log (last 20 before pause) ────────────────────
    QListWidget *m_frameLog;
    static constexpr int MAX_LOG_ENTRIES = 20;
    void addFrameToLog(const CanFrame &frame);

    // ── UI widgets ──────────────────────────────────────────
    QPushButton *m_loadBtn;
    QPushButton *m_playPauseBtn;
    QPushButton *m_stopBtn;
    QPushButton *m_nextBtn;
    QPushButton *m_binarySearchBtn;
    QSlider *m_speedSlider;
    QCheckBox *m_originalTimestampsCheck;
    QCheckBox *m_sendToBusCheck;
    QLabel *m_statusLabel;
    QLabel *m_bsRangeLabel;
    QProgressBar *m_progressBar;
    QTimer m_timer;

    AssociativeLearner *m_learner;
    LuaScriptEngine *m_luaEngine;
    CanSniffer *m_sniffer = nullptr;
};
