#pragma once
#include "BusLoadAnalyzer.h"
#include "CanFrame.h"
#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

// Qt6 Charts removed the QtCharts namespace; classes are in the global namespace
class QChartView;
class QLineSeries;
class QValueAxis;

class CanBusLoadWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanBusLoadWidget(QWidget *parent = nullptr);

public slots:
    void processFrame(const CanFrame &frame);
    void reset();

private slots:
    void onTick();
    void onBaudRateChanged();
    void onWindowChanged(double sec);

private:
    void buildUi();
    void refreshTable();
    void updateChart(double loadPct);
    void updateAlerts(double loadPct);

    BusLoadAnalyzer m_analyzer;

    // Controls
    QComboBox      *m_baudCombo   = nullptr;
    QDoubleSpinBox *m_windowSpin  = nullptr;
    QSpinBox       *m_alertSpin   = nullptr;

    // Display
    QProgressBar   *m_loadBar     = nullptr;
    QLabel         *m_loadLabel   = nullptr;
    QLabel         *m_peakLabel   = nullptr;
    QLabel         *m_fpsLabel    = nullptr;
    QLabel         *m_idsLabel    = nullptr;
    QLabel         *m_alertLabel  = nullptr;
    QTableWidget   *m_topTable    = nullptr;

    // Rolling chart
    QChartView  *m_chartView  = nullptr;
    QLineSeries *m_loadSeries = nullptr;
    QValueAxis  *m_axisX      = nullptr;
    QValueAxis  *m_axisY      = nullptr;
    double m_chartTimeSec = 0.0;
    static constexpr double kChartWindowSec = 60.0;

    QTimer *m_timer = nullptr;
    static constexpr int kTickMs = 250;
};
