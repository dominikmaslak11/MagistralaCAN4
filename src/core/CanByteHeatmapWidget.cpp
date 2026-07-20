#include "CanByteHeatmapWidget.h"
#include "DbcParser.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QToolTip>
#include <QSpinBox>
#include <chrono>
#include <cmath>

// ── Color mapping ─────────────────────────────────────────────────────────────

QColor CanByteHeatmapWidget::valueToColor(double v) {
    if (v < 0.0)
        return QColor(20, 20, 35);   // no-data: near-black background

    // Normalize 0-255 → 0.0-1.0
    double t = std::clamp(v / 255.0, 0.0, 1.0);

    // 5-stop gradient: dark-navy → blue → cyan → green → yellow
    struct Stop { double pos; int r, g, b; };
    static constexpr Stop stops[] = {
        {0.00,   8,  12,  40},   // near-black navy
        {0.25,   0,  80, 220},   // blue
        {0.50,   0, 210, 200},   // cyan-teal
        {0.75,  80, 220,  60},   // green
        {1.00, 255, 240,  30},   // yellow
    };
    constexpr int N = 5;

    // Find the two surrounding stops
    int lo = 0;
    for (int i = 0; i < N - 1; ++i) {
        if (t <= stops[i + 1].pos) { lo = i; break; }
        lo = i;
    }
    int hi = std::min(lo + 1, N - 1);

    double span = stops[hi].pos - stops[lo].pos;
    double f = (span > 0) ? (t - stops[lo].pos) / span : 0.0;

    auto lerp = [&](int a, int b_) {
        return static_cast<int>(a + f * (b_ - a));
    };
    return QColor(
        lerp(stops[lo].r, stops[hi].r),
        lerp(stops[lo].g, stops[hi].g),
        lerp(stops[lo].b, stops[hi].b));
}

// ── Construction ──────────────────────────────────────────────────────────────

CanByteHeatmapWidget::CanByteHeatmapWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    setMouseTracking(true);
    m_refreshTimer.setInterval(500);
    connect(&m_refreshTimer, &QTimer::timeout, this, &CanByteHeatmapWidget::refresh);
    m_refreshTimer.start();
}

void CanByteHeatmapWidget::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // ── Control row ───────────────────────────────────────────────────────────
    auto *ctrl = new QHBoxLayout;

    m_prevBtn = new QPushButton("◀", this);
    m_prevBtn->setMaximumWidth(28);
    ctrl->addWidget(m_prevBtn);

    m_idLabel = new QLabel("—", this);
    m_idLabel->setMinimumWidth(160);
    m_idLabel->setAlignment(Qt::AlignCenter);
    ctrl->addWidget(m_idLabel);

    m_nextBtn = new QPushButton("▶", this);
    m_nextBtn->setMaximumWidth(28);
    ctrl->addWidget(m_nextBtn);

    ctrl->addSpacing(12);
    ctrl->addWidget(new QLabel("Okno:", this));
    m_windowCombo = new QComboBox(this);
    m_windowCombo->addItems({"5 s", "10 s", "30 s", "60 s", "120 s"});
    m_windowCombo->setCurrentIndex(2);
    m_windowCombo->setMaximumWidth(70);
    ctrl->addWidget(m_windowCombo);

    ctrl->addWidget(new QLabel("Kubełki:", this));
    m_bucketCombo = new QComboBox(this);
    m_bucketCombo->addItems({"20", "30", "50", "100"});
    m_bucketCombo->setCurrentIndex(1);
    m_bucketCombo->setMaximumWidth(55);
    ctrl->addWidget(m_bucketCombo);

    m_pauseCheck = new QCheckBox("Pause", this);
    ctrl->addWidget(m_pauseCheck);

    m_clearBtn = new QPushButton("Clear", this);
    m_clearBtn->setMaximumWidth(55);
    ctrl->addWidget(m_clearBtn);

    ctrl->addStretch();

    m_statsLabel = new QLabel("IDs: 0  |  Ramki: 0", this);
    ctrl->addWidget(m_statsLabel);

    root->addLayout(ctrl);

    // The heatmap is drawn directly in paintEvent below the controls widget.
    // A blank spacer fills the rest of the layout.
    root->addStretch(1);

    connect(m_prevBtn,   &QPushButton::clicked, this, &CanByteHeatmapWidget::prevId);
    connect(m_nextBtn,   &QPushButton::clicked, this, &CanByteHeatmapWidget::nextId);
    connect(m_clearBtn,  &QPushButton::clicked, this, &CanByteHeatmapWidget::clearData);
    connect(m_pauseCheck, &QCheckBox::toggled, this, [this](bool p) { m_paused = p; });
    connect(m_windowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { rebuildCache(); update(); });
    connect(m_bucketCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { rebuildCache(); update(); });
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void CanByteHeatmapWidget::processFrame(const CanFrame &f, uint64_t tsUs) {
    if (m_paused) return;
    if (tsUs == 0) {
        using namespace std::chrono;
        tsUs = static_cast<uint64_t>(
            duration_cast<microseconds>(system_clock::now().time_since_epoch()).count());
    }
    m_model.addFrame(f, tsUs);

    // Auto-select the first tracked ID
    if (m_idIndex < 0 && !m_model.trackedIds().empty()) {
        m_idIndex    = 0;
        m_selectedId = m_model.trackedIds()[0];
        updateIdLabel();
    }
}

void CanByteHeatmapWidget::clearData() {
    m_model.clear();
    m_idIndex    = -1;
    m_selectedId = 0;
    m_cache.clear();
    updateIdLabel();
    update();
}

// ── Refresh ───────────────────────────────────────────────────────────────────

void CanByteHeatmapWidget::refresh() {
    if (m_paused) return;
    rebuildCache();
    update();
}

void CanByteHeatmapWidget::rebuildCache() {
    // Parse window
    static const int kWindowSecs[] = {5, 10, 30, 60, 120};
    int winIdx   = std::min(m_windowCombo->currentIndex(), 4);
    m_windowUs   = static_cast<uint64_t>(kWindowSecs[winIdx]) * 1'000'000ULL;

    // Parse bucket count
    static const int kBuckets[] = {20, 30, 50, 100};
    int bIdx     = std::min(m_bucketCombo->currentIndex(), 3);
    m_bucketCount = kBuckets[bIdx];

    if (m_idIndex >= 0 && m_idIndex < static_cast<int>(m_model.trackedIds().size())) {
        m_selectedId = m_model.trackedIds()[m_idIndex];
        m_cache      = m_model.heatmapData(m_selectedId, m_windowUs, m_bucketCount);
    } else {
        m_cache.clear();
    }

    m_statsLabel->setText(
        QString("IDs: %1  |  Ramki: %2  |  Kubełek: %3 ms")
            .arg(m_model.totalIds())
            .arg(m_model.frameCount(m_selectedId))
            .arg(kWindowSecs[winIdx] * 1000 / m_bucketCount));
}

// ── ID navigation ─────────────────────────────────────────────────────────────

void CanByteHeatmapWidget::prevId() {
    const auto &ids = m_model.trackedIds();
    if (ids.empty()) return;
    m_idIndex = (m_idIndex <= 0) ? static_cast<int>(ids.size()) - 1 : m_idIndex - 1;
    m_selectedId = ids[m_idIndex];
    updateIdLabel();
    rebuildCache();
    update();
}

void CanByteHeatmapWidget::nextId() {
    const auto &ids = m_model.trackedIds();
    if (ids.empty()) return;
    m_idIndex = (m_idIndex + 1) % static_cast<int>(ids.size());
    m_selectedId = ids[m_idIndex];
    updateIdLabel();
    rebuildCache();
    update();
}

void CanByteHeatmapWidget::updateIdLabel() {
    if (m_idIndex < 0 || m_model.trackedIds().empty()) {
        m_idLabel->setText("—  (brak danych)");
    } else {
        m_idLabel->setText(QString("[%1 / %2]  %3")
                               .arg(m_idIndex + 1)
                               .arg(m_model.totalIds())
                               .arg(labelForId(m_selectedId)));
    }
}

QString CanByteHeatmapWidget::labelForId(uint32_t id) const {
    if (m_dbc) {
        for (const auto &msg : m_dbc->messages()) {
            if (msg.id == id)
                return msg.name + QString(" (0x%1)").arg(id, 0, 16);
        }
    }
    return QString("0x%1").arg(id, 0, 16);
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void CanByteHeatmapWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Background
    p.fillRect(rect(), QColor(15, 15, 25));

    // Control strip height: find bottom of last widget in layout
    int ctrlH = 0;
    if (layout() && layout()->count() > 0) {
        // Controls are in the first layout item (HBoxLayout inside VBox)
        // Use a fixed estimate based on font metrics
        ctrlH = fontMetrics().height() * 2 + 16;
    }
    m_heatmapTop = ctrlH;

    // Heatmap area
    int hmLeft   = kMarginLeft;
    int hmTop    = m_heatmapTop + kMarginTop;
    int hmRight  = width()  - kMarginRight;
    int hmBottom = height() - kMarginBottom;
    int hmW      = hmRight - hmLeft;
    int hmH      = hmBottom - hmTop;

    if (hmW < 8 || hmH < 4 || m_cache.empty()) {
        p.setPen(QColor(120, 120, 160));
        p.drawText(rect(), Qt::AlignCenter, "Brak danych — wybierz CAN ID lub rozpocznij sniffing");
        return;
    }

    const int rows = static_cast<int>(m_cache.size());
    const int cols = 8;
    double cellW = static_cast<double>(hmW) / cols;
    double cellH = static_cast<double>(hmH) / rows;

    // ── Draw cells ────────────────────────────────────────────────────────────
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            double val = m_cache[r][c];
            QColor col = valueToColor(val);

            // Hover highlight
            if (r == m_hoverBucket && c == m_hoverByte)
                col = col.lighter(160);

            int x = hmLeft  + static_cast<int>(c * cellW);
            int y = hmTop   + static_cast<int>(r * cellH);
            int w = std::max(1, static_cast<int>((c + 1) * cellW) - static_cast<int>(c * cellW));
            int h = std::max(1, static_cast<int>((r + 1) * cellH) - static_cast<int>(r * cellH));

            p.fillRect(x, y, w, h, col);

            // Draw value text if cells are large enough
            if (cellW >= 32 && cellH >= 14 && val >= 0) {
                p.setPen(val > 128 ? QColor(0, 0, 0, 200) : QColor(255, 255, 255, 180));
                p.setFont(QFont("Monospace", 7));
                p.drawText(x, y, w, h, Qt::AlignCenter,
                           QString("%1").arg(static_cast<int>(val), 2, 16, QChar('0')).toUpper());
            }
        }
    }

    // ── Grid lines ────────────────────────────────────────────────────────────
    p.setPen(QPen(QColor(40, 40, 60), 1));
    for (int c = 1; c < cols; ++c) {
        int x = hmLeft + static_cast<int>(c * cellW);
        p.drawLine(x, hmTop, x, hmBottom);
    }
    // Sparse horizontal lines (every 10 buckets)
    for (int r = 0; r <= rows; r += std::max(1, rows / 10)) {
        int y = hmTop + static_cast<int>(r * cellH);
        p.drawLine(hmLeft, y, hmRight, y);
    }

    // ── Column headers (B0–B7) ────────────────────────────────────────────────
    p.setFont(QFont("Monospace", 9, QFont::Bold));
    for (int c = 0; c < cols; ++c) {
        int x = hmLeft + static_cast<int>(c * cellW);
        int w = static_cast<int>(cellW);
        p.setPen(QColor(180, 200, 255));
        p.drawText(x, m_heatmapTop, w, kMarginTop, Qt::AlignCenter,
                   QString("B%1").arg(c));
    }

    // ── Time labels (left axis) ───────────────────────────────────────────────
    p.setFont(QFont("Monospace", 7));
    p.setPen(QColor(140, 150, 180));
    int labelStride = std::max(1, rows / 8);
    for (int r = 0; r <= rows; r += labelStride) {
        // r=0 is oldest, r=rows is "now"
        double secAgo = static_cast<double>(m_windowUs) / 1e6
                        * (1.0 - static_cast<double>(r) / rows);
        QString lbl = (secAgo < 0.5) ? "now"
                      : QString("-%1s").arg(static_cast<int>(secAgo));
        int y = hmTop + static_cast<int>(r * cellH);
        p.drawText(0, y - 8, kMarginLeft - 4, 16, Qt::AlignRight | Qt::AlignVCenter, lbl);
    }

    // ── Legend bar (right side) ───────────────────────────────────────────────
    int lgX = hmRight + 8;
    int lgY = hmTop;
    int lgH = hmH;
    for (int py = 0; py < lgH; ++py) {
        double t = 1.0 - static_cast<double>(py) / lgH;  // top = max
        p.setPen(valueToColor(t * 255.0));
        p.drawLine(lgX, lgY + py, lgX + kLegendW, lgY + py);
    }
    p.setPen(QColor(100, 110, 140));
    p.drawRect(lgX, lgY, kLegendW, lgH);
    p.setFont(QFont("Monospace", 7));
    p.setPen(QColor(180, 190, 220));
    p.drawText(lgX + kLegendW + 2, lgY,           40, 14, Qt::AlignLeft, "FF");
    p.drawText(lgX + kLegendW + 2, lgY + lgH / 2 - 7, 40, 14, Qt::AlignLeft, "80");
    p.drawText(lgX + kLegendW + 2, lgY + lgH - 14,    40, 14, Qt::AlignLeft, "00");

    // ── Border ────────────────────────────────────────────────────────────────
    p.setPen(QPen(QColor(60, 65, 90), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(hmLeft, hmTop, hmW, hmH);
}

// ── Mouse hover ───────────────────────────────────────────────────────────────

void CanByteHeatmapWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_cache.empty()) { m_hoverBucket = m_hoverByte = -1; return; }

    int hmLeft   = kMarginLeft;
    int hmTop    = m_heatmapTop + kMarginTop;
    int hmRight  = width()  - kMarginRight;
    int hmBottom = height() - kMarginBottom;
    int hmW      = hmRight - hmLeft;
    int hmH      = hmBottom - hmTop;

    QPoint pos = event->pos();
    int relX = pos.x() - hmLeft;
    int relY = pos.y() - hmTop;

    if (relX < 0 || relX >= hmW || relY < 0 || relY >= hmH) {
        m_hoverBucket = m_hoverByte = -1;
        update();
        return;
    }

    const int rows = static_cast<int>(m_cache.size());
    int bucket = static_cast<int>(static_cast<double>(relY) / hmH * rows);
    int byte_  = static_cast<int>(static_cast<double>(relX) / hmW * 8);
    bucket = std::clamp(bucket, 0, rows - 1);
    byte_  = std::clamp(byte_, 0, 7);

    if (bucket != m_hoverBucket || byte_ != m_hoverByte) {
        m_hoverBucket = bucket;
        m_hoverByte   = byte_;
        update();

        double val = m_cache[bucket][byte_];
        if (val >= 0) {
            QString tip = QString("Bajt B%1 | kubełek %2\nŚrednia: 0x%3 (%4 dec)")
                              .arg(byte_)
                              .arg(bucket)
                              .arg(static_cast<int>(val), 2, 16, QChar('0')).toUpper()
                              .arg(static_cast<int>(val));
            QToolTip::showText(event->globalPosition().toPoint(), tip, this);
        } else {
            QToolTip::hideText();
        }
    }
}

void CanByteHeatmapWidget::leaveEvent(QEvent *) {
    m_hoverBucket = m_hoverByte = -1;
    update();
}
