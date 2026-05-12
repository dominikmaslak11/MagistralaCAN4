#pragma once
#include <QWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QSplitter>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QChart>
#include <QtCharts/QValueAxis>
#include <array>
#include "CanFrame.h"
#include "DbcParser.h"

class SignalPlotterWidget : public QWidget {
    Q_OBJECT
public:
    explicit SignalPlotterWidget(QWidget *parent = nullptr);

    void setDbcParser(const DbcParser *dbc);

public slots:
    void processFrame(const CanFrame &frame);
    void clearPlot();
    void refreshSignalTree();
    void exportPng();

private slots:
    void onSignalDoubleClicked(QTreeWidgetItem *item, int column);
    void onWindowChanged(int idx);
    void onNormalizeToggled(bool on);
    void removeChannel(int ch);

private:
    void addToChannel(const QString &signalName, uint32_t msgId, const DbcSignal &sig);
    void trimSeries(int ch, double latestSec);
    void rescaleYAxis();
    void updateStatsLabel();

    static constexpr int    kMaxChannels = 4;
    static constexpr double kDefaultWindow = 10.0; // seconds
    static const QColor     kColors[kMaxChannels];

    struct Channel {
        QString     name;
        uint32_t    msgId    = 0;
        double      scale    = 1.0;
        double      offset   = 0.0;
        double      minVal   =  1e18;
        double      maxVal   = -1e18;
        double      lastVal  = 0.0;
        bool        active   = false;
        bool        normalize= false;
        QLineSeries *series  = nullptr;
    };

    const DbcParser *m_dbc = nullptr;

    std::array<Channel, kMaxChannels> m_channels;
    double   m_windowSec  = kDefaultWindow;
    uint64_t m_firstTs    = 0;    // µs, from first frame
    bool     m_normalize  = false;

    // ── Widgets ────────────────────────────────────────────────
    QTreeWidget  *m_signalTree;
    QChartView   *m_chartView;
    QChart       *m_chart;
    QValueAxis   *m_timeAxis;
    QValueAxis   *m_valueAxis;
    QLabel       *m_statsLabel;
    QComboBox    *m_windowCombo;
    QCheckBox    *m_normalizeCheck;
    QPushButton  *m_clearBtn;
    QPushButton  *m_exportBtn;
    QPushButton  *m_refreshBtn;
    QPushButton  *m_removeBtn[kMaxChannels];
    QLabel       *m_chLabel[kMaxChannels];
};
