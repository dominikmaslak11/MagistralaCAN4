#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVector>
#include <QTimer>
#include "CanFrame.h"
#include "CanBitAnalyzer.h"
#include "CanIntervalAnalyzer.h"
#include "CanPayloadSearch.h"

/// Unified reverse-engineering / forensics panel.
/// Three tabs: Bit Profiler | Interval Analyzer | Payload Search.
class CanForensicsWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanForensicsWidget(QWidget *parent = nullptr);

public slots:
    void processFrame(const CanFrame &frame);

private slots:
    void resetBitProfiler();
    void refreshBitProfiler();
    void resetIntervalAnalyzer();
    void refreshIntervalTable();
    void runPayloadSearch();
    void clearFrameStore();
    void copyBitReport();

private:
    QWidget *makeBitProfilerTab();
    QWidget *makeIntervalTab();
    QWidget *makePayloadSearchTab();

    CanBitAnalyzer       m_bitAnalyzer;
    CanIntervalAnalyzer  m_intervalAnalyzer;
    QVector<CanFrame>    m_frameStore;
    static constexpr int kMaxStore = 200'000;

    // ── Bit Profiler ──────────────────────────────────────────
    QTableWidget *m_bitTable       = nullptr;
    QLabel       *m_bitStats       = nullptr;

    // ── Interval Analyzer ─────────────────────────────────────
    QTableWidget *m_intervalTable  = nullptr;
    QLabel       *m_intervalStats  = nullptr;

    // ── Payload Search ─────────────────────────────────────────
    QLineEdit    *m_patternEdit    = nullptr;
    QLineEdit    *m_maskEdit       = nullptr;
    QLineEdit    *m_searchIdEdit   = nullptr;
    QLineEdit    *m_maxResEdit     = nullptr;
    QTableWidget *m_searchTable    = nullptr;
    QLabel       *m_searchStats    = nullptr;
};
