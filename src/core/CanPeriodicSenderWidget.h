#pragma once
#include "CanPeriodicSender.h"
#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QCheckBox>
#include <QTimer>

/// Widget for configuring and running a periodic CAN frame schedule.
class CanPeriodicSenderWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanPeriodicSenderWidget(CanSniffer *sniffer, QWidget *parent = nullptr);

private slots:
    void addEntry();
    void removeSelected();
    void toggleRun();
    void onStatsUpdated();
    void onCellChanged(int row, int col);

private:
    void buildUi();
    void refreshTable();
    CanFrame frameFromEditors() const;

    CanPeriodicSender *m_sender;

    // Frame editor
    QLineEdit *m_idEdit;
    QLineEdit *m_byteEdits[8];
    QSpinBox  *m_dlcSpin;
    QSpinBox  *m_periodSpin;
    QCheckBox *m_extCheck;
    QLineEdit *m_labelEdit;

    QPushButton  *m_addBtn;
    QPushButton  *m_removeBtn;
    QPushButton  *m_runBtn;

    QTableWidget *m_table;
    QLabel       *m_statusLabel;
};
