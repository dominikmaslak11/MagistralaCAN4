#pragma once
#include <QWidget>
#include <QScrollArea>
#include <QPushButton>
#include <QComboBox>
#include <QGridLayout>
#include <QVector>
#include "CanFrame.h"
#include "CanDashboardConfig.h"
#include "DbcParser.h"
#include "CanGaugeWidget.h"

class CanCustomDashboard : public QWidget {
    Q_OBJECT
public:
    explicit CanCustomDashboard(QWidget *parent = nullptr);

    void setDbcParser(const DbcParser *parser);

public slots:
    void processFrame(const CanFrame &frame);

private slots:
    void toggleEditMode();
    void addGaugeDialog();
    void removeGauge(int index);
    void saveLayout();
    void loadLayout();
    void changeColumns(int cols);

private:
    struct GaugeEntry {
        CanGaugeWidget *gauge  = nullptr;
        QWidget        *cell   = nullptr;
        QPushButton    *delBtn = nullptr;
        GaugeConfig     config;
    };

    void rebuildGrid();
    void applyEditMode();
    void persistLayout();

    const DbcParser    *m_dbcParser = nullptr;
    DashboardLayout     m_layout;
    QVector<GaugeEntry> m_entries;
    bool                m_editMode  = false;

    QScrollArea *m_scroll     = nullptr;
    QWidget     *m_gridWidget = nullptr;
    QGridLayout *m_grid       = nullptr;
    QPushButton *m_editBtn    = nullptr;
    QPushButton *m_addBtn     = nullptr;
    QComboBox   *m_colCombo   = nullptr;
};
