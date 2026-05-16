#pragma once
#include <QWidget>
#include <QTimer>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QProcess>
#include "IcSimDecoder.h"

class CanSniffer;
class IcSimDashPanel;

// Widget obsługi symulatora ICSim (Open Garages Instrument Cluster Simulator).
// Dekoduje ramki ICSim ze sniffera i pozwala wysyłać ramki sterujące.
// Obsługuje tryb seeded (losowe ID) przez pola konfiguracji w UI.
// Pozwala też uruchomić ICSim i automatycznie połączyć klienta zdalnego CAN.
class IcSimWidget : public QWidget {
    Q_OBJECT
public:
    explicit IcSimWidget(CanSniffer *sniffer, QWidget *parent = nullptr);
    ~IcSimWidget() override;

signals:
    // Emitowany przez "Uruchom ICSim" — MainWindow podpina do RemoteCanClient::connectToServer
    void connectRequested(const QString &url, const QString &token);

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
    void onLaunchICSim();
    void onStopICSim();
    void onBridgeStateChanged(QProcess::ProcessState state);

private:
    void buildLayout();
    QGroupBox *buildSpeedGroup();
    QGroupBox *buildDoorGroup();
    QGroupBox *buildSignalGroup();
    QGroupBox *buildConfigGroup();
    QGroupBox *buildLaunchGroup();
    void updateDoorButtons();
    void updateSignalButtons();
    void updateLaunchStatus();
    void sendFrame(const CanFrame &f);

    uint32_t speedId()  const;
    uint32_t doorId()   const;
    uint32_t signalId() const;

    CanSniffer    *m_sniffer;
    IcSimState     m_state;
    IcSimDashPanel *m_dash;

    // Speed controls
    QSlider     *m_speedSlider   = nullptr;
    QLabel      *m_speedLabel    = nullptr;
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

    // ICSim launcher
    QLineEdit   *m_buildDirEdit  = nullptr;
    QSpinBox    *m_bridgePort    = nullptr;
    QPushButton *m_launchBtn     = nullptr;
    QPushButton *m_stopBtn       = nullptr;
    QLabel      *m_launchStatus  = nullptr;
    QProcess    *m_bridgeProc    = nullptr;
    QProcess    *m_simProc       = nullptr;
};
