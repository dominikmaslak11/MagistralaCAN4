#pragma once
#include <QWidget>
#include <QVector>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QChartView>
#include <QtCharts/QValueAxis>
#include "CanFrame.h"
#include "DbcParser.h"
#include "CanSignalTrendData.h"

// ── Chart widget ─────────────────────────────────────────────────────────────

class CanSignalTrendWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanSignalTrendWidget(QWidget *parent = nullptr);

    void setDbcParser(const DbcParser *p) { m_dbc = p; rebuildSignalList(); }

public slots:
    void processFrame(const CanFrame &frame);

private slots:
    void onSignalComboChanged(int comboIdx, int sigIdx);
    void onWindowChanged(int seconds);
    void onClearClicked();
    void onPauseToggled(bool paused);

private:
    static constexpr int    kMaxSignals   = 4;
    static constexpr double kUpdateHz     = 10.0;  // max chart refresh rate

    void setupUi();
    void rebuildSignalList();
    void refreshChart();
    void updateSeries(int slot);

    static const QColor kColors[kMaxSignals];

    const DbcParser *m_dbc = nullptr;

    // Per-slot controls
    QComboBox    *m_sigCombo[kMaxSignals]  = {};
    QLabel       *m_sigLabel[kMaxSignals]  = {};

    QSpinBox     *m_windowSpin   = nullptr;
    QPushButton  *m_clearBtn     = nullptr;
    QCheckBox    *m_pauseChk     = nullptr;
    QLabel       *m_statusLabel  = nullptr;

    // Chart
    QChart       *m_chart       = nullptr;
    QChartView   *m_chartView   = nullptr;
    QLineSeries  *m_series[kMaxSignals] = {};
    QValueAxis   *m_axisX       = nullptr;
    QValueAxis   *m_axisY       = nullptr;

    // Data
    struct SignalSlot {
        QString         signalName;
        uint32_t        canId   = 0;
        CanSignalTrendData data;
    };
    SignalSlot      m_slots[kMaxSignals];

    double          m_windowS    = 30.0;
    bool            m_paused     = false;
    double          m_startTimeS = -1;     // set on first frame
    double          m_lastRefreshS = 0;
    QStringList     m_signalNames; // cached list of all DBC signals
};
