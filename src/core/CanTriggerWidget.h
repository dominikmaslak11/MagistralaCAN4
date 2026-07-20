#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include "CanFrame.h"
#include "CanTriggerRecorder.h"

/// GUI front-end for CanTriggerRecorder.
/// Arm a condition, then capture N pre-trigger + trigger + M post-trigger frames.
class CanTriggerWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanTriggerWidget(QWidget *parent = nullptr);

public slots:
    void processFrame(const CanFrame &frame);

private slots:
    void toggleArm();
    void exportCapture();
    void clearCapture();

private:
    void buildTrigger();
    void onCaptureComplete();
    void populateCaptureTable();
    void setStatus(const QString &text, const QString &color);

    CanTriggerRecorder m_recorder;
    bool               m_armed            = false;
    int                m_triggerFrameIdx  = -1;

    // Condition UI
    QLineEdit  *m_idEdit         = nullptr;
    QComboBox  *m_modeCombo      = nullptr;
    QSpinBox   *m_byteOffsetSpin = nullptr;
    QLineEdit  *m_valueEdit      = nullptr;
    QLineEdit  *m_maskEdit       = nullptr;

    // Buffer UI
    QSpinBox   *m_preSpin        = nullptr;
    QSpinBox   *m_postSpin       = nullptr;

    // Control
    QPushButton *m_armBtn        = nullptr;
    QLabel      *m_statusLbl     = nullptr;
    QPushButton *m_exportBtn     = nullptr;
    QPushButton *m_clearBtn      = nullptr;

    // Results
    QTableWidget *m_captureTable = nullptr;
};
