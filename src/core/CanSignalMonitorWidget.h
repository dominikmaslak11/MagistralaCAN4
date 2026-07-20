#pragma once
#include "CanSignalMonitor.h"
#include "DbcParser.h"
#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QLineEdit>
#include <QSpinBox>

/// Live signal dashboard: shows current physical values for all DBC-mapped signals.
class CanSignalMonitorWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanSignalMonitorWidget(QWidget *parent = nullptr);

    void setDbc(const DbcParser *dbc);

public slots:
    void processFrame(const CanFrame &frame);
    void reset();

private slots:
    void onTick();
    void applyFilter();

private:
    void buildUi();
    void refreshTable();

    CanSignalMonitor m_monitor;
    QTableWidget    *m_table{};
    QLabel          *m_infoLabel{};
    QLineEdit       *m_filterEdit{};
    QSpinBox        *m_staleSpin{};   // stale threshold in ms
    QPushButton     *m_resetBtn{};
    QTimer          *m_timer;

    static constexpr int kTickMs = 250;
    static constexpr int kDefaultStaleMs = 2000;
};
