#include "CanStatsPanel.h"
#include <QOpenGLWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <algorithm>

CanStatsPanel::CanStatsPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(2, 1, 2, 1);
    layout->setSpacing(3);

    // ── Filtr ID ──
    m_filterEdit = new QLineEdit;
    m_filterEdit->setPlaceholderText("Filtruj po CAN ID (hex)...");
    m_filterEdit->setStyleSheet(
        "QLineEdit { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560; "
        "border-radius: 3px; padding: 1px 6px; font-size: 10px; min-height: 18px; }");
    connect(m_filterEdit, &QLineEdit::textChanged, this, &CanStatsPanel::filterChanged);
    layout->addWidget(m_filterEdit, 1);

    // ── Etykieta statystyk ──
    m_statsLabel = new QLabel("Ramki: 0 | FPS: 0 | Unikalne ID: 0 | Obc: 0%");
    m_statsLabel->setStyleSheet(
        "color: #ffaa00; font-weight: bold; font-size: 10px; "
        "background: #1a1a2e; padding: 1px 6px; border-radius: 3px;");
    layout->addWidget(m_statsLabel);

    // ── Przycisk pauzy ──
    m_pauseBtn = new QPushButton("\u23F8 Pauza");
    m_pauseBtn->setFixedWidth(80);
    m_pauseBtn->setStyleSheet(
        "QPushButton { background: #1a1a2e; color: #ffaa00; border: 1px solid #e94560; "
        "border-radius: 3px; padding: 1px 8px; font-weight: bold; font-size: 10px; } "
        "QPushButton:hover { background: #e94560; color: #0a0e17; }");
    connect(m_pauseBtn, &QPushButton::clicked, this, &CanStatsPanel::togglePause);
    layout->addWidget(m_pauseBtn);

    // ── Mini-wykres FPS ──
    m_fpsChart = new QChart();
    m_fpsChart->setMargins(QMargins(0, 0, 0, 0));
    m_fpsChart->setBackgroundRoundness(0);
    m_fpsChart->legend()->hide();
    m_fpsSeries = new QLineSeries();
    m_fpsSeries->setColor(QColor("#00ffaa"));
    m_fpsChart->addSeries(m_fpsSeries);
    m_fpsChart->createDefaultAxes();
    m_fpsChart->axes(Qt::Horizontal).first()->setVisible(false);
    m_fpsChart->axes(Qt::Vertical).first()->setVisible(false);
    m_fpsChartView = new QChartView(m_fpsChart);
    m_fpsChartView->setViewport(new QOpenGLWidget());
    m_fpsChartView->setRenderHint(QPainter::Antialiasing);
    m_fpsChartView->setFixedSize(80, 20);
    m_fpsChartView->setStyleSheet("background: transparent;");
    layout->addWidget(m_fpsChartView);

    setMaximumHeight(26);

    // ── Timer (co 500ms) ──
    m_timer.setInterval(500);
    connect(&m_timer, &QTimer::timeout, this, &CanStatsPanel::updateStats);
    m_timer.start();
}

void CanStatsPanel::onNewFrame(uint32_t canId, uint64_t timestamp) {
    m_totalFrameCount++;
    m_uniqueIds.insert(canId);
    m_totalUniqueIds.insert(canId);

    auto &st = m_idStats[canId];
    if (st.lastTs > 0 && timestamp > st.lastTs) {
        double dt = double(timestamp - st.lastTs) / 1'000'000.0;
        st.avgInterval = st.count > 1 ? (st.avgInterval * static_cast<double>(st.count - 1) + dt) / static_cast<double>(st.count) : dt;
    }
    st.count++;
    st.lastTs = timestamp;
}

void CanStatsPanel::reset() {
    m_totalFrameCount = 0;
    m_lastStatsFrameCount = 0;
    m_uniqueIds.clear();
    m_totalUniqueIds.clear();
    m_idStats.clear();
    m_fpsWindow.clear();
    m_fpsHistoryCount = 0;
}

void CanStatsPanel::togglePause() {
    m_paused = !m_paused;
    if (m_paused) {
        m_pauseBtn->setText("\u25B6 Wznów");
        m_pauseBtn->setStyleSheet(
            "QPushButton { background: #e94560; color: #0a0e17; border: 1px solid #e94560; "
            "border-radius: 3px; padding: 1px 8px; font-weight: bold; font-size: 10px; }");
    } else {
        m_pauseBtn->setText("\u23F8 Pauza");
        m_pauseBtn->setStyleSheet(
            "QPushButton { background: #1a1a2e; color: #ffaa00; border: 1px solid #e94560; "
            "border-radius: 3px; padding: 1px 8px; font-weight: bold; font-size: 10px; } "
            "QPushButton:hover { background: #e94560; color: #0a0e17; }");
    }
    emit pauseToggled(m_paused);
}

void CanStatsPanel::updateStats() {
    uint64_t delta = m_totalFrameCount - m_lastStatsFrameCount;
    double fps = static_cast<double>(delta) / 0.5;
    int uniqueIds = static_cast<int>(m_uniqueIds.size());
    double busLoad = (static_cast<double>(delta) * 8 * 8.0) / (500'000 * 0.5) * 100.0;
    if (busLoad > 100) busLoad = 100;

    // Per-ID top 3
    QVector<QPair<uint32_t, uint64_t>> sortedIds;
    sortedIds.reserve(m_idStats.size());
    for (auto it = m_idStats.begin(); it != m_idStats.end(); ++it)
        sortedIds.append({it.key(), it->count});
    std::sort(sortedIds.begin(), sortedIds.end(),
              [](auto &a, auto &b) { return a.second > b.second; });

    QString topIds;
    for (int i = 0; i < 3 && i < sortedIds.size(); ++i)
        topIds += QString("%1 0x%2(%3) ")
                     .arg(i > 0 ? "|" : "")
                     .arg(sortedIds[i].first, 3, 16, QChar('0'))
                     .arg(sortedIds[i].second);

    // Burst detection
    m_fpsWindow.append(fps);
    if (m_fpsWindow.size() > BURST_WINDOW)
        m_fpsWindow.removeFirst();

    bool burst = false;
    if (m_fpsWindow.size() >= BURST_WINDOW) {
        double sum = 0, sq = 0;
        for (double f : m_fpsWindow) { sum += f; sq += f * f; }
        m_fpsMean = sum / static_cast<double>(m_fpsWindow.size());
        m_fpsStd = std::sqrt(sq / static_cast<double>(m_fpsWindow.size()) - m_fpsMean * m_fpsMean);
        if (m_fpsStd < 0.1) m_fpsStd = 0.1;
        burst = (fps > m_fpsMean + BURST_SIGMA * m_fpsStd);
    }

    QString burstMarker = burst ? " \u26A1BURST" : "";

    m_statsLabel->setText(
        QString("Ramki: %1 | FPS: %2 | Unikalne ID: %3/%4 | Obc.: %5%%6 | Top: %7")
            .arg(m_totalFrameCount).arg(fps, 0, 'f', 0)
            .arg(uniqueIds).arg(m_totalUniqueIds.size())
            .arg(busLoad, 0, 'f', 1).arg(burstMarker).arg(topIds));
    m_statsLabel->setToolTip(burst ? "Wykryto wybuch ruchu CAN — FPS powyżej 3σ od średniej!" : "");

    if (burst) {
        m_statsLabel->setStyleSheet(
            "color: #ff4444; font-weight: bold; font-size: 10px; "
            "background: #1a1a2e; padding: 1px 6px; border-radius: 3px;");
        QTimer::singleShot(1500, this, [this]() {
            m_statsLabel->setStyleSheet(
                "color: #ffaa00; font-weight: bold; font-size: 10px; "
                "background: #1a1a2e; padding: 1px 6px; border-radius: 3px;");
        });
    }

    m_lastStatsFrameCount = m_totalFrameCount;
    m_uniqueIds.clear();

    emit statsUpdated(fps, uniqueIds);

    // Mini-wykres FPS
    m_fpsSeries->append(m_fpsHistoryCount, fps);
    m_fpsHistoryCount++;
    if (m_fpsSeries->count() > 60)
        m_fpsSeries->removePoints(0, m_fpsSeries->count() - 60);
    m_fpsChart->axes(Qt::Vertical).first()->setRange(0, std::max(100.0, fps * 1.5));
}
