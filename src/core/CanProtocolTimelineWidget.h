#pragma once
#include <QWidget>
#include <QTimer>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QtCharts/QChart>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QChartView>
#include <QtCharts/QValueAxis>
#include <QtCharts/QCategoryAxis>
#include "CanTimelineModel.h"
#include "CanFrame.h"
#include "DbcParser.h"

class CanProtocolTimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanProtocolTimelineWidget(QWidget *parent = nullptr);

    void setDbcParser(const DbcParser *dbc);

public slots:
    void processFrame(const CanFrame &frame, uint64_t tsUs = 0);

private slots:
    void refresh();
    void clearData();
    void togglePause(bool paused);

private:
    void buildUi();
    void rebuildSeries();
    QString labelForId(uint32_t id) const;

    CanTimelineModel   m_model;
    const DbcParser   *m_dbc = nullptr;

    QChart            *m_chart{};
    QChartView        *m_chartView{};
    QValueAxis        *m_axisX{};   // time in ms (relative to window start)
    QCategoryAxis     *m_axisY{};   // ID labels

    QComboBox         *m_windowCombo{};   // rolling window: 5/10/30/60s
    QSpinBox          *m_maxIdsSpin{};
    QCheckBox         *m_pauseCheck{};
    QPushButton       *m_clearBtn{};
    QLabel            *m_statsLabel{};

    QTimer             m_refreshTimer;
    bool               m_paused = false;

    // Color palette — up to 16 series
    static const QColor kPalette[16];
};
