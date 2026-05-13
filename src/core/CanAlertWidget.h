#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include "CanAlertEngine.h"
#include "CanFrame.h"

class CanAlertWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanAlertWidget(QWidget *parent = nullptr);

    CanAlertEngine *engine() { return &m_engine; }

public slots:
    void onFrame(const CanFrame &frame, uint64_t tsUs = 0);

private slots:
    void addRule();
    void removeRule();
    void onAlertTriggered(const CanAlert &alert);
    void clearAlerts();
    void resetEngine();
    void onTypeChanged(int idx);

private:
    void buildUi();
    void refreshRuleTable();

    CanAlertEngine m_engine;

    // Rule editor
    QLineEdit       *m_nameEdit;
    QComboBox       *m_typeCombo;
    QComboBox       *m_actionCombo;
    QLineEdit       *m_idEdit;
    QSpinBox        *m_byteIdxSpin;
    QComboBox       *m_opCombo;
    QSpinBox        *m_thresholdSpin;
    QDoubleSpinBox  *m_rateDevSpin;
    QPushButton     *m_addBtn;
    QPushButton     *m_removeBtn;

    // Rule list
    QTableWidget    *m_ruleTable;

    // Alert log
    QTableWidget    *m_alertTable;

    QLabel          *m_statsLabel;
};
