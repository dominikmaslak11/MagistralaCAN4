#pragma once
#include "CanFrame.h"
#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QTableWidget>
#include <QTimer>

class CanSniffer;
class DbcParser;

/// Manual CAN frame sender — one-shot and periodic injection.
class CanFrameSenderWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanFrameSenderWidget(CanSniffer *sniffer, QWidget *parent = nullptr);

    void setDbcParser(const DbcParser *dbc);

public slots:
    void sendOnce();
    void startPeriodic();
    void stopPeriodic();

private slots:
    void onPeriodicTick();
    void onDlcChanged(int dlc);
    void onLoadFromDbc(int index);
    void addToHistory(const CanFrame &frame);
    void onHistorySelected(int row, int col);

private:
    void buildUi();
    CanFrame buildFrame() const;
    bool validateInput();

    CanSniffer        *m_sniffer;
    const DbcParser   *m_dbc = nullptr;

    // Frame construction
    QLineEdit  *m_idEdit   = nullptr;
    QCheckBox  *m_extCheck = nullptr;
    QCheckBox  *m_fdCheck  = nullptr;
    QSpinBox   *m_dlcSpin  = nullptr;
    QLineEdit  *m_byteEdits[8] = {};

    // Load from DBC
    QComboBox  *m_dbcMsgCombo = nullptr;

    // Periodic
    QSpinBox   *m_periodSpin  = nullptr; // ms
    QPushButton *m_startPeriodicBtn = nullptr;
    QPushButton *m_stopPeriodicBtn  = nullptr;
    QLabel      *m_sentCountLabel   = nullptr;
    QLabel      *m_statusLabel      = nullptr;

    QTimer     *m_periodicTimer  = nullptr;
    int         m_sentCount      = 0;

    // History
    QTableWidget *m_historyTable = nullptr;
    static constexpr int kMaxHistory = 50;
};
