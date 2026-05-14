#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include "CanFrame.h"
#include "CanSignalStatistics.h"
#include "DbcParser.h"

/// Live statistics table for DBC signals: min/max/mean/stdDev + histogram bar.
/// Requires a DBC parser to be set via setDbcParser().
class CanSignalStatisticsWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanSignalStatisticsWidget(QWidget *parent = nullptr);

    void setDbcParser(const DbcParser *parser);

public slots:
    void processFrame(const CanFrame &frame);

private slots:
    void resetStats();
    void refreshTable();
    void exportCsv();
    void applyFilter(const QString &text);

private:
    void updateRow(int row, const SignalStats &s);

    const DbcParser      *m_dbc   = nullptr;
    CanSignalStatistics   m_stats;
    QString               m_filter;

    QTableWidget *m_table       = nullptr;
    QLabel       *m_statsLbl    = nullptr;
    QLineEdit    *m_filterEdit  = nullptr;
};
