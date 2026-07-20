#pragma once
#include "CanIdStats.h"
#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QLineEdit>

/// Widget showing per-ID byte statistics accumulated from live traffic.
class CanIdStatsWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanIdStatsWidget(QWidget *parent = nullptr);

public slots:
    void processFrame(const CanFrame &frame);
    void reset();

private slots:
    void onTick();
    void exportCsv();
    void applyFilter();

private:
    void buildUi();
    void refreshTable();

    CanIdStats   m_stats;
    QTableWidget *m_table{};
    QLabel       *m_infoLabel{};
    QLineEdit    *m_filterEdit{};
    QPushButton  *m_exportBtn{};
    QPushButton  *m_resetBtn{};
    QTimer       *m_timer;

    static constexpr int kTickMs = 1000;
};
