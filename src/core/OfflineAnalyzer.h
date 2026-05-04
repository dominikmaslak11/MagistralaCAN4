#pragma once
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QSlider>
#include <QCheckBox>
#include <QTimer>
#include <QVector>
#include "CanFrame.h"

class AssociativeLearner;
class LuaScriptEngine;

class OfflineAnalyzer : public QWidget {
    Q_OBJECT
public:
    explicit OfflineAnalyzer(AssociativeLearner *learner,
                             LuaScriptEngine *lua = nullptr,
                             QWidget *parent = nullptr);

public slots:
    void loadFile();
    void playPause();
    void stop();
    void setSpeed(int value);  // 1-100

private slots:
    void playNextFrame();

private:
    QVector<CanFrame> m_frames;
    int m_currentIndex = 0;
    bool m_playing = false;

    QPushButton *m_loadBtn;
    QPushButton *m_playPauseBtn;
    QPushButton *m_stopBtn;
    QSlider *m_speedSlider;
    QCheckBox *m_originalTimestampsCheck;   // NOWE
    QLabel *m_statusLabel;
    QProgressBar *m_progressBar;
    QTimer m_timer;

    AssociativeLearner *m_learner;
    LuaScriptEngine *m_luaEngine;
};
