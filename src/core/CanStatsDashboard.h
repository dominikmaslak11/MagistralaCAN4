#pragma once
#include <QWidget>
#include <QTimer>
#include <QHash>
#include <QVector>
#include <array>
#include <cstdint>
#include "CanFrame.h"

class QTableWidget;
class QLabel;
class QChartView;
class QLineSeries;
class QValueAxis;
class QBarSeries;
class QBarSet;
class QBarCategoryAxis;

/// Unified real-time statistics dashboard.
/// Single-panel view: bus load sparkline, DLC histogram, top-10 IDs by frames and bytes.
class CanStatsDashboard : public QWidget {
    Q_OBJECT
public:
    explicit CanStatsDashboard(QWidget *parent = nullptr);

public slots:
    void processFrame(const CanFrame &frame);
    void reset();

private slots:
    void onTick();

private:
    void setupUi();
    void refreshTopTables();
    void refreshDlcChart();
    void refreshLoadChart(double fps, double loadPct);
    void updateCounterLabels();

    // ── Data ─────────────────────────────────────────────────────────────────
    struct IdEntry {
        uint64_t frames = 0;
        uint64_t bytes  = 0;
    };
    QHash<uint32_t, IdEntry> m_idMap;
    std::array<uint64_t, 65> m_dlcHist{};
    uint64_t m_totalFrames = 0;
    uint64_t m_totalBytes  = 0;

    // For FPS/load estimate: ring buffer of frame timestamps (μs)
    struct TimestampRing {
        static constexpr int kCap = 8192;
        uint64_t buf[kCap]{};
        int head = 0, count = 0;
        void push(uint64_t ts) { buf[head] = ts; head = (head + 1) % kCap; if (count < kCap) ++count; }
        // Count frames in last windowUs microseconds
        int countIn(uint64_t windowUs) const {
            if (!count) return 0;
            uint64_t latest = buf[(head - 1 + kCap) % kCap];
            uint64_t cutoff = (latest > windowUs) ? (latest - windowUs) : 0;
            int n = 0;
            for (int i = 0; i < count; ++i) {
                if (buf[(head - 1 - i + kCap) % kCap] >= cutoff) ++n;
                else break;
            }
            return n;
        }
    } m_tsRing;

    // Bus load sparkline ring (1s samples, 60 values)
    QVector<double> m_loadSamples;  // last 60 values (%)
    static constexpr int kLoadHistory = 60;

    // ── UI ───────────────────────────────────────────────────────────────────
    QLabel      *m_lblTotal  = nullptr;
    QLabel      *m_lblIds    = nullptr;
    QLabel      *m_lblBytes  = nullptr;
    QLabel      *m_lblFps    = nullptr;

    // Bus load chart
    QChartView  *m_loadChart  = nullptr;
    QLineSeries *m_loadSeries = nullptr;
    QValueAxis  *m_loadAxisX  = nullptr;
    QValueAxis  *m_loadAxisY  = nullptr;

    // DLC histogram chart
    QChartView        *m_dlcChart    = nullptr;
    QBarSeries        *m_dlcSeries   = nullptr;
    QBarSet           *m_dlcSet      = nullptr;
    QBarCategoryAxis  *m_dlcAxisX    = nullptr;
    QValueAxis        *m_dlcAxisY    = nullptr;

    // Top tables
    QTableWidget *m_topFrames = nullptr;
    QTableWidget *m_topBytes  = nullptr;

    QTimer *m_timer = nullptr;
    static constexpr int kTickMs = 1000;
};
