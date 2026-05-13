#include "CanProtocolTimelineWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <chrono>
#include <cstdio>

const QColor CanProtocolTimelineWidget::kPalette[16] = {
    {0xFF,0x44,0x44}, {0x44,0x88,0xFF}, {0x44,0xFF,0x88}, {0xFF,0xAA,0x00},
    {0xCC,0x44,0xFF}, {0x00,0xDD,0xDD}, {0xFF,0xFF,0x44}, {0xFF,0x66,0xCC},
    {0x88,0xFF,0x44}, {0x44,0xCC,0xFF}, {0xFF,0x88,0x44}, {0xAA,0xFF,0xCC},
    {0xFF,0xCC,0x88}, {0x88,0xAA,0xFF}, {0xDD,0xFF,0x88}, {0xFF,0x44,0xAA},
};

CanProtocolTimelineWidget::CanProtocolTimelineWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    m_refreshTimer.setInterval(250);
    connect(&m_refreshTimer, &QTimer::timeout, this, &CanProtocolTimelineWidget::refresh);
    m_refreshTimer.start();
}

void CanProtocolTimelineWidget::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // ── Controls ─────────────────────────────────────────────────────────────
    auto *ctrlRow = new QHBoxLayout;

    ctrlRow->addWidget(new QLabel("Window:", this));
    m_windowCombo = new QComboBox(this);
    m_windowCombo->addItems({"5 s", "10 s", "30 s", "60 s", "120 s"});
    m_windowCombo->setCurrentIndex(1);
    m_windowCombo->setMaximumWidth(80);
    ctrlRow->addWidget(m_windowCombo);

    ctrlRow->addWidget(new QLabel("Max IDs:", this));
    m_maxIdsSpin = new QSpinBox(this);
    m_maxIdsSpin->setRange(2, 16);
    m_maxIdsSpin->setValue(12);
    m_maxIdsSpin->setMaximumWidth(55);
    ctrlRow->addWidget(m_maxIdsSpin);

    m_pauseCheck = new QCheckBox("Pause", this);
    ctrlRow->addWidget(m_pauseCheck);

    m_clearBtn = new QPushButton("Clear", this);
    m_clearBtn->setMaximumWidth(60);
    ctrlRow->addWidget(m_clearBtn);

    ctrlRow->addStretch();
    m_statsLabel = new QLabel("Frames: 0  |  IDs: 0", this);
    ctrlRow->addWidget(m_statsLabel);
    root->addLayout(ctrlRow);

    // ── Chart ────────────────────────────────────────────────────────────────
    m_chart = new QChart;
    m_chart->setTitle("Protocol Timeline — Message Sequence");
    m_chart->setAnimationOptions(QChart::NoAnimation); // no animation for live data
    m_chart->legend()->setAlignment(Qt::AlignRight);
    m_chart->setBackgroundBrush(QBrush(QColor(0x1E, 0x1E, 0x1E)));
    m_chart->setTitleBrush(QBrush(Qt::white));
    m_chart->legend()->setLabelColor(Qt::white);

    m_axisX = new QValueAxis;
    m_axisX->setTitleText("Time (ms)");
    m_axisX->setLabelFormat("%.0f");
    m_axisX->setTitleBrush(QBrush(Qt::white));
    m_axisX->setLabelsBrush(QBrush(Qt::lightGray));
    m_axisX->setGridLinePen(QPen(QColor(60, 60, 60)));
    m_chart->addAxis(m_axisX, Qt::AlignBottom);

    m_axisY = new QCategoryAxis;
    m_axisY->setTitleText("CAN ID");
    m_axisY->setTitleBrush(QBrush(Qt::white));
    m_axisY->setLabelsBrush(QBrush(Qt::lightGray));
    m_axisY->setGridLinePen(QPen(QColor(60, 60, 60)));
    m_axisY->setMin(-0.5);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);

    m_chartView = new QChartView(m_chart, this);
    m_chartView->setRenderHint(QPainter::Antialiasing, false); // faster for dots
    root->addWidget(m_chartView, 1);

    connect(m_clearBtn,    &QPushButton::clicked, this, &CanProtocolTimelineWidget::clearData);
    connect(m_pauseCheck,  &QCheckBox::toggled,   this, &CanProtocolTimelineWidget::togglePause);
    connect(m_maxIdsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) { m_model.setMaxTrackedIds(v); });
}

void CanProtocolTimelineWidget::setDbcParser(const DbcParser *dbc) {
    m_dbc = dbc;
}

void CanProtocolTimelineWidget::processFrame(const CanFrame &frame, uint64_t tsUs) {
    if (m_paused) return;
    if (tsUs == 0) {
        using namespace std::chrono;
        tsUs = static_cast<uint64_t>(
            duration_cast<microseconds>(system_clock::now().time_since_epoch()).count());
    }
    m_model.addFrame(frame, tsUs);
}

void CanProtocolTimelineWidget::togglePause(bool paused) {
    m_paused = paused;
}

void CanProtocolTimelineWidget::clearData() {
    m_model.clear();
    rebuildSeries();
}

QString CanProtocolTimelineWidget::labelForId(uint32_t id) const {
    if (m_dbc) {
        for (const auto &msg : m_dbc->messages()) {
            if (msg.id == id)
                return msg.name + QString(" (0x%1)").arg(id, 0, 16);
        }
    }
    return QString("0x%1").arg(id, 0, 16);
}

void CanProtocolTimelineWidget::refresh() {
    if (m_paused) return;
    rebuildSeries();
}

void CanProtocolTimelineWidget::rebuildSeries() {
    // Pick time window
    static const int kWindowSecs[] = {5, 10, 30, 60, 120};
    int windowSec = kWindowSecs[std::min(m_windowCombo->currentIndex(), 4)];
    uint64_t windowUs = static_cast<uint64_t>(windowSec) * 1'000'000ULL;

    auto [minTs, maxTs] = m_model.timeRange();
    if (m_model.totalEventCount() == 0) {
        m_chart->removeAllSeries();
        m_statsLabel->setText("Frames: 0  |  IDs: 0");
        return;
    }

    uint64_t viewEnd   = maxTs;
    uint64_t viewStart = (viewEnd > windowUs) ? (viewEnd - windowUs) : 0;
    double   viewEndMs   = static_cast<double>(viewEnd   - viewStart) / 1000.0;

    auto ids = m_model.trackedIds();

    // Remove old series
    m_chart->removeAllSeries();
    for (const auto &lbl : m_axisY->categoriesLabels())
        m_axisY->remove(lbl);

    double yMax = static_cast<double>(ids.size()) - 0.5;
    m_axisY->setMax(yMax);
    m_axisY->setMin(-0.5);

    // Rebuild Y axis labels
    for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
        QString lbl = labelForId(ids[i]);
        m_axisY->append(lbl, static_cast<double>(i) + 0.5);
    }

    // Rebuild X axis
    m_axisX->setRange(0.0, viewEndMs);

    // Build one QScatterSeries per ID
    for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
        uint32_t id  = ids[i];
        auto    *ser = new QScatterSeries;
        ser->setName(labelForId(id));
        ser->setColor(kPalette[i % 16]);
        ser->setMarkerSize(6.0);
        ser->setMarkerShape(QScatterSeries::MarkerShapeCircle);

        auto evs = m_model.eventsInRange(id, viewStart, viewEnd);
        for (const auto &ev : evs) {
            double xMs = static_cast<double>(ev.timestampUs - viewStart) / 1000.0;
            ser->append(xMs, static_cast<double>(i));
        }

        m_chart->addSeries(ser);
        ser->attachAxis(m_axisX);
        ser->attachAxis(m_axisY);
    }

    m_statsLabel->setText(
        QString("Frames: %1  |  IDs: %2  |  Window: %3s")
            .arg(m_model.totalEventCount())
            .arg(ids.size())
            .arg(windowSec));
}
