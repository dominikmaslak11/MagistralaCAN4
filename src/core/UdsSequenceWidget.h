#pragma once
#include "UdsSequenceRunner.h"
#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QComboBox>

class CanSniffer;

class UdsSequenceWidget : public QWidget {
    Q_OBJECT
public:
    explicit UdsSequenceWidget(CanSniffer *sniffer, QWidget *parent = nullptr);

public slots:
    void processFrame(const CanFrame &frame);

private slots:
    void addStep();
    void removeStep();
    void moveUp();
    void moveDown();
    void onStart();
    void onAbort();
    void onStepCompleted(int stepIndex, UdsStepResult result);
    void onSequenceFinished(bool allPassed);
    void onStatus(const QString &msg);
    void loadTemplate(int index);

private:
    void buildUi();
    UdsStep stepFromRow(int row) const;
    void refreshResultColors();

    CanSniffer         *m_sniffer;
    UdsSequenceRunner   m_runner;

    // Step definition table
    QTableWidget *m_stepTable  = nullptr; // name | txId | rxId | payload | timeout | retries | stopOnNeg
    QLineEdit    *m_nameEdit   = nullptr;
    QLineEdit    *m_txIdEdit   = nullptr;
    QLineEdit    *m_rxIdEdit   = nullptr;
    QLineEdit    *m_payloadEdit = nullptr; // space-separated hex bytes
    QSpinBox     *m_timeoutSpin = nullptr;
    QSpinBox     *m_retriesSpin = nullptr;
    QCheckBox    *m_stopNegCheck = nullptr;
    QComboBox    *m_templateCombo = nullptr;

    // Results table
    QTableWidget *m_resultTable = nullptr; // step | status | detail | elapsed | response

    // Control
    QPushButton *m_startBtn  = nullptr;
    QPushButton *m_abortBtn  = nullptr;
    QLabel      *m_statusLabel = nullptr;
    QLabel      *m_progressLabel = nullptr;
};
