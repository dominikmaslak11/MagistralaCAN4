#pragma once
#include <QWidget>
#include <QLabel>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QTimer>
#include <QVector>
#include "CanFrame.h"
#include "CanBusErrorAnalyzer.h"
#include "CanCounterValidator.h"

/// Two-tab bus health panel.
/// Tab 1 — Error Monitor: uses CanBusErrorAnalyzer (error classes, rate, event log).
/// Tab 2 — Counter Validator: uses CanCounterValidator (AUTOSAR rolling counter rules).
class CanBusHealthWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanBusHealthWidget(QWidget *parent = nullptr);

public slots:
    /// Feed every CAN frame (including error frames). Use frameProcessed, not throttled.
    void processFrame(const CanFrame &frame);

private slots:
    void refresh();
    void resetAll();
    void addCounterRule();
    void removeSelectedRule();

private:
    QWidget *makeErrorTab();
    QWidget *makeCounterTab();
    void updateErrorCounts();
    void updateCounterTable();
    void appendErrorEvent(const CanBusErrorAnalyzer::ErrorEvent &ev);

    static QString errorClassName(CanBusErrorAnalyzer::ErrorClass cls);

    CanBusErrorAnalyzer   m_errAnalyzer;
    CanCounterValidator   m_counterValidator;
    size_t                m_lastEventIdx = 0;
    QVector<CanCounterValidator::Config> m_rules;

    // ── Error tab UI ──────────────────────────────────────────────────────
    QLabel       *m_busOffAlert   = nullptr;
    QLabel       *m_countLabels[10]{};   // one per ErrorClass (TxTimeout → Unknown)
    QLabel       *m_rateLabel     = nullptr;
    QLabel       *m_totalLabel    = nullptr;
    QTableWidget *m_eventTable    = nullptr;

    // ── Counter tab UI ────────────────────────────────────────────────────
    QLineEdit    *m_ctIdEdit      = nullptr;
    QSpinBox     *m_ctByteSpin    = nullptr;
    QCheckBox    *m_ctNibbleChk   = nullptr;
    QSpinBox     *m_ctModulusSpin = nullptr;
    QTableWidget *m_rulesTable    = nullptr;
};
