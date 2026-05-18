#include "CanStatsDashboard.h"
#include <QDateTime>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QGroupBox>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QBarCategoryAxis>
#include <algorithm>

CanStatsDashboard::CanStatsDashboard(QWidget *parent)
    : QWidget(parent)
{
    m_dlcHist.fill(0);
    m_loadSamples.fill(0.0, kLoadHistory);
    setupUi();

    m_timer = new QTimer(this);
    m_timer->setInterval(kTickMs);
    connect(m_timer, &QTimer::timeout, this, &CanStatsDashboard::onTick);
    m_timer->start();
}

// ── Incoming data ──────────────────────────────────────────────────────────────

void CanStatsDashboard::processFrame(const CanFrame &frame) {
    ++m_totalFrames;
    m_totalBytes += frame.dlc;

    auto &entry = m_idMap[frame.id];
    ++entry.frames;
    entry.bytes += frame.dlc;

    uint8_t dlc = std::min(frame.dlc, (uint8_t)64);
    ++m_dlcHist[dlc];

    m_tsRing.push(frame.timestamp ? frame.timestamp : (uint64_t)QDateTime::currentMSecsSinceEpoch() * 1000);
}

void CanStatsDashboard::reset() {
    m_idMap.clear();
    m_dlcHist.fill(0);
    m_totalFrames = m_totalBytes = 0;
    m_tsRing.head = m_tsRing.count = 0;
    m_loadSamples.fill(0.0);
    updateCounterLabels();
    refreshTopTables();
    refreshDlcChart();
    refreshLoadChart(0.0, 0.0);
}

// ── Timer tick ─────────────────────────────────────────────────────────────────

void CanStatsDashboard::onTick() {
    // FPS: frames in last 1s (1000 ms = 1 000 000 μs)
    double fps = m_tsRing.countIn(1'000'000);

    // Simple bus load: assume 125 kbps baseline → fps * (avg_dlc*8 + 47 bits overhead) / 125000
    // Just show fps for now; load % from fps * (avg bits per frame) / baudrate
    // We don't know baudrate here, so show fps as the main metric
    double avgBits = (m_totalFrames > 0)
        ? (double)m_totalBytes * 8.0 / m_totalFrames + 47.0
        : 55.0;
    double loadPct = std::min(fps * avgBits / 1250.0, 100.0);  // assumes 125kbps

    m_loadSamples.removeFirst();
    m_loadSamples.append(loadPct);

    updateCounterLabels();
    refreshTopTables();
    refreshDlcChart();
    refreshLoadChart(fps, loadPct);
}

// ── UI Setup ───────────────────────────────────────────────────────────────────

void CanStatsDashboard::setupUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // ── Pasek górny: liczniki ─────────────────────────────────────────────────
    auto *hdr = new QHBoxLayout;
    auto makeLabel = [](const QString &title) -> QLabel * {
        auto *l = new QLabel(title);
        l->setStyleSheet("font-weight: bold; font-size: 13px; padding: 4px 8px;"
                         "border-radius: 4px; background: #1a2035; color: #00ffaa;");
        return l;
    };
    m_lblTotal = makeLabel("Ramki: 0");
    m_lblIds   = makeLabel("Unikalne ID: 0");
    m_lblBytes = makeLabel("Bajty: 0");
    m_lblFps   = makeLabel("FPS: 0");
    auto *resetBtn = new QPushButton("Reset");
    resetBtn->setMaximumWidth(70);
    connect(resetBtn, &QPushButton::clicked, this, &CanStatsDashboard::reset);

    hdr->addWidget(m_lblTotal);
    hdr->addWidget(m_lblIds);
    hdr->addWidget(m_lblBytes);
    hdr->addWidget(m_lblFps);
    hdr->addStretch();
    hdr->addWidget(resetBtn);
    root->addLayout(hdr);

    // ── Główny splitter pionowy ────────────────────────────────────────────────
    auto *vSplit = new QSplitter(Qt::Vertical);

    // Górny rząd: bus load + DLC histogram
    auto *topWidget = new QWidget;
    auto *topLayout = new QHBoxLayout(topWidget);
    topLayout->setContentsMargins(0, 0, 0, 0);

    // Bus load sparkline (60s)
    {
        auto *chart = new QChart;
        chart->setTitle("Bus Load % (ostatnie 60s, 125 kbps)");
        chart->setAnimationOptions(QChart::NoAnimation);
        chart->legend()->hide();
        chart->setBackgroundBrush(QColor("#0d1117"));
        chart->setTitleBrush(QColor("#cccccc"));

        m_loadSeries = new QLineSeries;
        m_loadSeries->setColor(QColor("#e94560"));
        m_loadSeries->setPen(QPen(QColor("#e94560"), 2));
        for (int i = 0; i < kLoadHistory; ++i)
            m_loadSeries->append(i, 0.0);
        chart->addSeries(m_loadSeries);

        m_loadAxisX = new QValueAxis;
        m_loadAxisX->setRange(0, kLoadHistory - 1);
        m_loadAxisX->setLabelFormat("%d");
        m_loadAxisX->setTitleText("s");
        m_loadAxisX->setLabelsColor(QColor("#888"));
        m_loadAxisX->setGridLineColor(QColor("#2a2a3c"));
        chart->addAxis(m_loadAxisX, Qt::AlignBottom);
        m_loadSeries->attachAxis(m_loadAxisX);

        m_loadAxisY = new QValueAxis;
        m_loadAxisY->setRange(0, 100);
        m_loadAxisY->setTitleText("%");
        m_loadAxisY->setLabelsColor(QColor("#888"));
        m_loadAxisY->setGridLineColor(QColor("#2a2a3c"));
        chart->addAxis(m_loadAxisY, Qt::AlignLeft);
        m_loadSeries->attachAxis(m_loadAxisY);

        m_loadChart = new QChartView(chart);
        m_loadChart->setRenderHint(QPainter::Antialiasing);
        m_loadChart->setMinimumHeight(180);
    }

    // DLC Histogram
    {
        auto *chart = new QChart;
        chart->setTitle("Histogram DLC");
        chart->setAnimationOptions(QChart::NoAnimation);
        chart->legend()->hide();
        chart->setBackgroundBrush(QColor("#0d1117"));
        chart->setTitleBrush(QColor("#cccccc"));

        m_dlcSet    = new QBarSet("");
        m_dlcSeries = new QBarSeries;
        m_dlcSet->setColor(QColor("#569CD6"));
        m_dlcSet->setBorderColor(QColor("#3a7aaa"));

        QStringList cats;
        for (int i = 0; i <= 8; ++i) {
            *m_dlcSet << 0.0;
            cats << QString::number(i);
        }
        m_dlcSeries->append(m_dlcSet);
        chart->addSeries(m_dlcSeries);

        m_dlcAxisX = new QBarCategoryAxis;
        m_dlcAxisX->append(cats);
        m_dlcAxisX->setLabelsColor(QColor("#888"));
        chart->addAxis(m_dlcAxisX, Qt::AlignBottom);
        m_dlcSeries->attachAxis(m_dlcAxisX);

        m_dlcAxisY = new QValueAxis;
        m_dlcAxisY->setRange(0, 1);
        m_dlcAxisY->setLabelFormat("%d");
        m_dlcAxisY->setLabelsColor(QColor("#888"));
        m_dlcAxisY->setGridLineColor(QColor("#2a2a3c"));
        chart->addAxis(m_dlcAxisY, Qt::AlignLeft);
        m_dlcSeries->attachAxis(m_dlcAxisY);

        m_dlcChart = new QChartView(chart);
        m_dlcChart->setRenderHint(QPainter::Antialiasing);
        m_dlcChart->setMinimumHeight(180);
    }

    topLayout->addWidget(m_loadChart, 3);
    topLayout->addWidget(m_dlcChart,  2);
    vSplit->addWidget(topWidget);

    // Dolny rząd: top-10 tabele
    auto *botWidget = new QWidget;
    auto *botLayout = new QHBoxLayout(botWidget);
    botLayout->setContentsMargins(0, 0, 0, 0);

    auto makeTable = [](const QString &title) -> QTableWidget * {
        auto *t = new QTableWidget(10, 2);
        t->setHorizontalHeaderLabels({title == "frames" ? "CAN ID" : "CAN ID",
                                      title == "frames" ? "Ramki" : "Bajty"});
        t->horizontalHeader()->setStretchLastSection(true);
        t->verticalHeader()->hide();
        t->setEditTriggers(QAbstractItemView::NoEditTriggers);
        t->setSelectionBehavior(QAbstractItemView::SelectRows);
        t->setAlternatingRowColors(true);
        t->setMaximumHeight(230);
        return t;
    };

    auto *gFrames = new QGroupBox("Top-10 po liczbie ramek");
    auto *gBytes  = new QGroupBox("Top-10 po liczbie bajtów");
    m_topFrames = makeTable("frames");
    m_topBytes  = makeTable("bytes");
    (new QVBoxLayout(gFrames))->addWidget(m_topFrames);
    (new QVBoxLayout(gBytes)) ->addWidget(m_topBytes);

    botLayout->addWidget(gFrames);
    botLayout->addWidget(gBytes);
    vSplit->addWidget(botWidget);

    vSplit->setStretchFactor(0, 3);
    vSplit->setStretchFactor(1, 2);
    root->addWidget(vSplit, 1);
}

// ── Refresh helpers ────────────────────────────────────────────────────────────

void CanStatsDashboard::updateCounterLabels() {
    m_lblTotal->setText(QString("Ramki: %1").arg(m_totalFrames));
    m_lblIds  ->setText(QString("Unikalne ID: %1").arg(m_idMap.size()));
    m_lblBytes->setText(QString("Bajty: %1").arg(m_totalBytes));
    double fps = m_tsRing.countIn(1'000'000);
    m_lblFps  ->setText(QString("FPS: %1").arg((int)fps));
}

void CanStatsDashboard::refreshTopTables() {
    // Build sorted lists
    struct Entry { uint32_t id; uint64_t val; };
    QVector<Entry> byFrames, byBytes;
    byFrames.reserve(m_idMap.size());
    byBytes .reserve(m_idMap.size());
    for (auto it = m_idMap.cbegin(); it != m_idMap.cend(); ++it) {
        byFrames.append({it.key(), it.value().frames});
        byBytes .append({it.key(), it.value().bytes});
    }
    auto cmp = [](const Entry &a, const Entry &b){ return a.val > b.val; };
    std::partial_sort(byFrames.begin(), byFrames.begin() + std::min(10, (int)byFrames.size()), byFrames.end(), cmp);
    std::partial_sort(byBytes .begin(), byBytes .begin() + std::min(10, (int)byBytes .size()), byBytes .end(), cmp);

    auto fillTable = [](QTableWidget *t, const QVector<Entry> &vec) {
        int n = std::min(10, (int)vec.size());
        for (int i = 0; i < 10; ++i) {
            if (i < n) {
                t->setItem(i, 0, new QTableWidgetItem(
                    QString("0x%1").arg(vec[i].id, 3, 16, QChar('0')).toUpper()));
                t->setItem(i, 1, new QTableWidgetItem(
                    QString::number(vec[i].val)));
            } else {
                t->setItem(i, 0, new QTableWidgetItem("—"));
                t->setItem(i, 1, new QTableWidgetItem(""));
            }
        }
    };
    fillTable(m_topFrames, byFrames);
    fillTable(m_topBytes,  byBytes);
}

void CanStatsDashboard::refreshDlcChart() {
    uint64_t maxVal = 1;
    for (int i = 0; i <= 8; ++i)
        maxVal = std::max(maxVal, m_dlcHist[i]);

    m_dlcSet->remove(0, m_dlcSet->count());
    for (int i = 0; i <= 8; ++i)
        *m_dlcSet << (double)m_dlcHist[i];

    m_dlcAxisY->setRange(0, (double)maxVal * 1.1 + 1);
}

void CanStatsDashboard::refreshLoadChart(double /*fps*/, double loadPct) {
    Q_UNUSED(loadPct);
    // Update sparkline with current m_loadSamples
    m_loadSeries->clear();
    for (int i = 0; i < m_loadSamples.size(); ++i)
        m_loadSeries->append(i, m_loadSamples[i]);

    double maxLoad = *std::max_element(m_loadSamples.begin(), m_loadSamples.end());
    m_loadAxisY->setRange(0, std::max(10.0, maxLoad * 1.1));
}
