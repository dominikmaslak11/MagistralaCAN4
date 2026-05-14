#pragma once
#include <QWidget>
#include <QTimer>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QGroupBox>
#include "IcSimDecoder.h"

class CanSniffer;
class IcSimDashPanel;

// Widget obsługi symulatora ICSim (Open Garages Instrument Cluster Simulator).
// Dekoduje ramki ICSim ze sniffera i pozwala wysyłać ramki sterujące.
// Obsługuje tryb seeded (losowe ID) przez pola konfiguracji w UI.
class IcSimWidget : public QWidget {
    Q_OBJECT
public:
    explicit IcSimWidget(CanSniffer *sniffer, QWidget *parent = nullptr);

public slots:
    void processFrame(const CanFrame &frame);

private slots:
    void onSpeedSliderChanged(int mph);
    void onLockAll();
    void onUnlockAll();
    void onToggleDoor(int door);
    void onToggleLeftSignal();
    void onToggleRightSignal();
    void onContinuousTimer();
    void onResetState();

private:
    void buildLayout();
    QGroupBox *buildSpeedGroup();
    QGroupBox *buildDoorGroup();
    QGroupBox *buildSignalGroup();
    QGroupBox *buildConfigGroup();
    void updateDoorButtons();
    void updateSignalButtons();
    void sendFrame(const CanFrame &f);

    uint32_t speedId()  const;
    uint32_t doorId()   const;
    uint32_t signalId() const;

    CanSniffer    *m_sniffer;
    IcSimState     m_state;
    IcSimDashPanel *m_dash;

    // Speed controls
    QSlider     *m_speedSlider  = nullptr;
    QLabel      *m_speedLabel   = nullptr;
    QCheckBox   *m_continuousChk = nullptr;
    QTimer       m_continuousTimer;

    // Door buttons (4)
    QPushButton *m_doorBtn[4]   = {};

    // Signal buttons
    QPushButton *m_leftSigBtn  = nullptr;
    QPushButton *m_rightSigBtn = nullptr;

    // CAN ID overrides (for seeded ICSim)
    QSpinBox *m_speedIdSpin  = nullptr;
    QSpinBox *m_doorIdSpin   = nullptr;
    QSpinBox *m_signalIdSpin = nullptr;
};
