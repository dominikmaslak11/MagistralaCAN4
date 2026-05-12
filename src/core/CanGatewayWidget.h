#pragma once
#include "CanGateway.h"
#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QRadioButton>
#include <QLineEdit>
#include <QTimer>

class ICanDriver;

/// UI for the CAN Gateway — configures source/sink interfaces and filter rules.
class CanGatewayWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanGatewayWidget(QWidget *parent = nullptr);

    /// Provide list of available (name, driver) pairs
    void addDriver(const QString &name, ICanDriver *driver);

    CanGateway *gateway() { return &m_gateway; }

public slots:
    /// Feed a frame from the main sniffer (source = "MainWindow sniffer")
    void processFrame(const CanFrame &frame);

private slots:
    void onStartStop();
    void addRule();
    void removeSelectedRule();
    void onTick();

private:
    void buildUi();
    void applyRulesToGateway();
    void updateStatus(bool running);

    CanGateway m_gateway;

    // Driver list
    struct DriverEntry { QString name; ICanDriver *driver; };
    std::vector<DriverEntry> m_drivers;

    // Source selection
    QComboBox *m_srcCombo  = nullptr;
    QComboBox *m_sinkCombo = nullptr;

    // Filter mode
    QRadioButton *m_passAllBtn  = nullptr;
    QRadioButton *m_whitelistBtn = nullptr;
    QRadioButton *m_blacklistBtn = nullptr;

    // Rule editor
    QLineEdit    *m_ruleIdEdit  = nullptr;  // hex ID
    QLineEdit    *m_ruleDstEdit = nullptr;  // remap destination (optional)
    QPushButton  *m_addRuleBtn  = nullptr;
    QPushButton  *m_delRuleBtn  = nullptr;
    QTableWidget *m_ruleTable   = nullptr;  // id | dst | action

    // Control
    QPushButton *m_startStopBtn = nullptr;
    QLabel      *m_statusLabel  = nullptr;

    // Stats
    QLabel *m_fwdLabel  = nullptr;
    QLabel *m_blkLabel  = nullptr;
    QLabel *m_totLabel  = nullptr;

    QTimer *m_statsTimer = nullptr;
};
