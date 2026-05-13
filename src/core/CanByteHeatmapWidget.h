#pragma once
#include "CanByteHeatmapModel.h"
#include "CanFrame.h"
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QTimer>
#include <vector>
#include <array>

class DbcParser;

/**
 * Byte-value heatmap for a single CAN ID.
 *
 * Rows = time buckets (oldest at top, newest at bottom).
 * Columns = bytes B0..B7.
 * Cell color encodes mean byte value within the bucket:
 *   0x00 → dark blue  |  0x80 → cyan/green  |  0xFF → bright yellow/white
 *
 * Navigation: Prev / Next buttons cycle through tracked IDs.
 */
class CanByteHeatmapWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanByteHeatmapWidget(QWidget *parent = nullptr);

    void setDbcParser(const DbcParser *dbc) { m_dbc = dbc; }

public slots:
    void processFrame(const CanFrame &f, uint64_t tsUs = 0);
    void clearData();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void buildUi();
    void refresh();
    void prevId();
    void nextId();
    void updateIdLabel();
    QString labelForId(uint32_t id) const;
    void rebuildCache();

    static QColor valueToColor(double v);  // v in [0,255], -1 = no data

    // Layout constants
    static constexpr int kMarginLeft   = 64;
    static constexpr int kMarginTop    = 28;
    static constexpr int kMarginRight  = 70;
    static constexpr int kMarginBottom = 16;
    static constexpr int kLegendW      = 18;

    CanByteHeatmapModel m_model;
    const DbcParser    *m_dbc = nullptr;

    // Selection
    int      m_idIndex    = -1;
    uint32_t m_selectedId = 0;

    // Controls
    QComboBox  *m_windowCombo = nullptr;
    QComboBox  *m_bucketCombo = nullptr;
    QPushButton *m_prevBtn   = nullptr;
    QPushButton *m_nextBtn   = nullptr;
    QLabel     *m_idLabel    = nullptr;
    QLabel     *m_statsLabel = nullptr;
    QCheckBox  *m_pauseCheck = nullptr;
    QPushButton *m_clearBtn  = nullptr;

    // Heatmap widget (draws inside paintEvent of this widget)
    // We reserve kMarginTop px at the top for controls via layout,
    // so the heatmap paints in the remaining area.
    int m_heatmapTop = 0; // set in paintEvent based on control height

    QTimer m_refreshTimer;
    bool   m_paused = false;

    // Cached data (rebuilt on refresh)
    std::vector<std::array<double, 8>> m_cache;   // [bucket][byte]
    int m_bucketCount = 30;
    uint64_t m_windowUs = 30'000'000ULL;

    // Hover
    int m_hoverBucket = -1;
    int m_hoverByte   = -1;
};
