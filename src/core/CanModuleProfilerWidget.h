#pragma once
#include "CanModuleProfiler.h"
#include <QWidget>
#include <QVector>

class CanAlertEngine;
class CanPeriodicSender;
class CanSniffer;
class QGroupBox;
class QListWidget;
class QProgressBar;
class QTextEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class CanModuleProfilerWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanModuleProfilerWidget(CanSniffer *sniffer = nullptr,
                                     QWidget   *parent  = nullptr);

    void setAlertEngine(CanAlertEngine *engine) { m_alertEngine = engine; }

public slots:
    void onFrame(const CanFrame &frame, uint64_t tsUs = 0);

private slots:
    void onStartLearning();
    void onCancelLearning();
    void onLearningFinished(const ModuleProfile &profile);
    void onLearningProgress(int pct);
    void onStartBackgroundLearning();
    void onCancelBackgroundLearning();
    void onBackgroundLearningProgress(int pct);
    void onBackgroundLearningFinished(const ModuleProfile &profile);
    void onStartDetecting();
    void onStopDetecting();
    void onStartSimulation();
    void onStopSimulation();
    void onProfileSelectionChanged();
    void onDeleteProfile();
    void onSaveProfiles();
    void onLoadProfiles();
    void onModuleOffline(const QString &name, uint32_t id);
    void onModuleOnline(const QString &name);

private:
    void updateProfileList();
    void updateDetails(const ModuleProfile &p);

    CanModuleProfiler  *m_profiler;
    CanPeriodicSender  *m_simSender   = nullptr;
    CanAlertEngine     *m_alertEngine = nullptr;

    QVector<ModuleProfile> m_profiles;

    QLineEdit    *m_nameEdit;
    QSpinBox     *m_durationSpin;
    QPushButton  *m_learnBtn;
    QPushButton  *m_cancelLearnBtn;
    QProgressBar *m_progressBar;

    // Phase 2 — background learning (differential mode)
    QGroupBox    *m_bgLearnGroup;
    QSpinBox     *m_bgDurationSpin;
    QPushButton  *m_bgLearnBtn;
    QPushButton  *m_bgCancelBtn;
    QProgressBar *m_bgProgressBar;
    ModuleProfile m_pendingPhase1;  // phase 1 result waiting to be refined

    QListWidget  *m_profileList;
    QPushButton  *m_detectBtn;
    QPushButton  *m_stopDetectBtn;
    QPushButton  *m_simulateBtn;
    QPushButton  *m_stopSimBtn;
    QPushButton  *m_deleteBtn;
    QPushButton  *m_saveBtn;
    QPushButton  *m_loadBtn;

    QLabel    *m_statusLabel;
    QTextEdit *m_detailsView;
};
