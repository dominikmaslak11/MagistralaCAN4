#include "CanSignalTrendWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <algorithm>

// ── CanSignalTrendWidget ──────────────────────────────────────────────────────

const QColor CanSignalTrendWidget::kColors[kMaxSignals] = {
    QColor("#00ffaa"), QColor("#ff6666"), QColor("#66aaff"), QColor("#ffcc00")
};

CanSignalTrendWidget::CanSignalTrendWidget(QWidget *parent) : QWidget(parent) {
    setupUi();
}

void CanSignalTrendWidget::setupUi() {
    auto *main = new QVBoxLayout(this);

    // ── Toolbar ──
    auto *toolbar = new QHBoxLayout;

    m_clearBtn = new QPushButton("Wyczyść");
    toolbar->addWidget(m_clearBtn);

    m_pauseChk = new QCheckBox("Pausa");
    toolbar->addWidget(m_pauseChk);

    toolbar->addWidget(new QLabel("Okno (s):"));
    m_windowSpin = new QSpinBox;
    m_windowSpin->setRange(5, 300);
    m_windowSpin->setValue(30);
    m_windowSpin->setSuffix(" s");
    toolbar->addWidget(m_windowSpin);

    toolbar->addStretch();
    m_statusLabel = new QLabel("Sygnałów aktywnych: 0");
    m_statusLabel->setStyleSheet("color: #00ffaa; font-weight: bold;");
    toolbar->addWidget(m_statusLabel);
    main->addLayout(toolbar);

    // ── Signal selection row ──
    auto *sigRow = new QHBoxLayout;
    for (int i = 0; i < kMaxSignals; ++i) {
        m_sigLabel[i] = new QLabel(QString("S%1:").arg(i + 1));
        m_sigLabel[i]->setStyleSheet(
            QString("color: %1; font-weight: bold;").arg(kColors[i].name()));
        sigRow->addWidget(m_sigLabel[i]);

        m_sigCombo[i] = new QComboBox;
        m_sigCombo[i]->addItem("--- (brak) ---");
        m_sigCombo[i]->setMinimumWidth(160);
        sigRow->addWidget(m_sigCombo[i]);

        connect(m_sigCombo[i], QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, i](int idx) { onSignalComboChanged(i, idx); });
    }
    main->addLayout(sigRow);

    // ── Chart ──
    m_chart = new QChart;
    m_chart->legend()->hide();
    m_chart->setBackgroundBrush(QColor("#0a0e17"));
    m_chart->setPlotAreaBackgroundBrush(QColor("#0f1320"));
    m_chart->setPlotAreaBackgroundVisible(true);

    m_axisX = new QValueAxis;
    m_axisX->setTitleText("Czas (s)");
    m_axisX->setLabelFormat("%.1f");
    m_axisX->setRange(0, 30);
    m_axisX->setLabelsColor(QColor("#808080"));
    m_axisX->setTitleBrush(QColor("#808080"));
    m_axisX->setGridLineColor(QColor("#2a2a3c"));

    m_axisY = new QValueAxis;
    m_axisY->setTitleText("Wartość");
    m_axisY->setRange(0, 100);
    m_axisY->setLabelsColor(QColor("#808080"));
    m_axisY->setTitleBrush(QColor("#808080"));
    m_axisY->setGridLineColor(QColor("#2a2a3c"));

    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);

    for (int i = 0; i < kMaxSignals; ++i) {
        m_series[i] = new QLineSeries;
        m_series[i]->setColor(kColors[i]);
        m_series[i]->setPen(QPen(kColors[i], 1.5));
        m_chart->addSeries(m_series[i]);
        m_series[i]->attachAxis(m_axisX);
        m_series[i]->attachAxis(m_axisY);
    }

    m_chartView = new QChartView(m_chart);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setBackgroundBrush(QColor("#0a0e17"));
    main->addWidget(m_chartView, 1);

    // ── Connections ──
    connect(m_clearBtn,   &QPushButton::clicked,             this, &CanSignalTrendWidget::onClearClicked);
    connect(m_windowSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &CanSignalTrendWidget::onWindowChanged);
    connect(m_pauseChk,   &QCheckBox::toggled, this, &CanSignalTrendWidget::onPauseToggled);

    setStyleSheet(R"(
        QWidget { background: #0a0e17; color: #c0c0c0; }
        QPushButton { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560;
                      border-radius: 4px; padding: 4px 10px; font-weight: bold; }
        QPushButton:hover { background: #e94560; color: #0a0e17; }
        QComboBox { background: #1a1a2e; color: #c0c0c0; border: 1px solid #2a2a3c;
                    padding: 2px 6px; border-radius: 3px; }
        QSpinBox { background: #1a1a2e; color: #c0c0c0; border: 1px solid #2a2a3c; padding: 2px; }
        QCheckBox { color: #c0c0c0; }
    )");
}

void CanSignalTrendWidget::rebuildSignalList() {
    m_signalNames.clear();
    if (!m_dbc) {
        for (int i = 0; i < kMaxSignals; ++i) {
            m_sigCombo[i]->clear();
            m_sigCombo[i]->addItem("--- (brak) ---");
        }
        return;
    }

    // Collect all signal names
    for (const auto &msg : m_dbc->messages()) {
        for (const auto &sig : msg.sigList)
            m_signalNames.append(sig.name);
    }
    m_signalNames.sort();

    for (int i = 0; i < kMaxSignals; ++i) {
        QString cur = m_slots[i].signalName;
        m_sigCombo[i]->blockSignals(true);
        m_sigCombo[i]->clear();
        m_sigCombo[i]->addItem("--- (brak) ---");
        for (const auto &name : m_signalNames)
            m_sigCombo[i]->addItem(name);
        // Restore previous selection if still valid
        int idx = m_signalNames.indexOf(cur);
        m_sigCombo[i]->setCurrentIndex(idx >= 0 ? idx + 1 : 0);
        m_sigCombo[i]->blockSignals(false);
    }
}

void CanSignalTrendWidget::onSignalComboChanged(int comboIdx, int sigIdx) {
    if (sigIdx == 0) {
        m_slots[comboIdx].signalName.clear();
        m_slots[comboIdx].canId = 0;
        m_slots[comboIdx].data.clear();
        m_series[comboIdx]->clear();
    } else {
        QString name = m_signalNames.value(sigIdx - 1);
        m_slots[comboIdx].signalName = name;
        m_slots[comboIdx].data.clear();
        m_series[comboIdx]->clear();

        // Find the CAN ID for this signal
        if (m_dbc) {
            for (const auto &msg : m_dbc->messages()) {
                for (const auto &sig : msg.sigList) {
                    if (sig.name == name) {
                        m_slots[comboIdx].canId = msg.id;
                        goto found;
                    }
                }
            }
        }
        found:;
    }

    int active = 0;
    for (int i = 0; i < kMaxSignals; ++i)
        if (!m_slots[i].signalName.isEmpty()) active++;
    m_statusLabel->setText(QString("Sygnałów aktywnych: %1").arg(active));
}

void CanSignalTrendWidget::processFrame(const CanFrame &frame) {
    if (m_paused || !m_dbc) return;

    double nowS = frame.timestamp / 1'000'000.0;
    if (m_startTimeS < 0) m_startTimeS = nowS;
    double relS = nowS - m_startTimeS;

    bool updated = false;
    for (int i = 0; i < kMaxSignals; ++i) {
        if (m_slots[i].signalName.isEmpty()) continue;
        if (m_slots[i].canId != frame.id)    continue;

        // Decode signal via DbcParser
        auto decoded = m_dbc->decodeSignals(frame.id, frame.data.data(), frame.dlc);
        if (!decoded.contains(m_slots[i].signalName)) continue;
        double val = decoded.value(m_slots[i].signalName);
        m_slots[i].data.addSample(relS, val);
        m_slots[i].data.trimToWindow(m_windowS);
        updated = true;
    }

    if (!updated) return;

    // Throttle chart refresh to kUpdateHz
    if ((relS - m_lastRefreshS) < (1.0 / kUpdateHz)) return;
    m_lastRefreshS = relS;
    refreshChart();
}

void CanSignalTrendWidget::refreshChart() {
    // Compute global time range
    double tMax = 0;
    for (int i = 0; i < kMaxSignals; ++i) {
        const auto &pts = m_slots[i].data.points();
        if (!pts.isEmpty()) tMax = std::max(tMax, pts.last().timeS);
    }
    double tMin = std::max(0.0, tMax - m_windowS);
    m_axisX->setRange(tMin, tMax > tMin ? tMax : tMin + 1);

    // Compute global Y range across all active signals
    double yMin =  std::numeric_limits<double>::max();
    double yMax =  std::numeric_limits<double>::lowest();
    bool   anyData = false;

    for (int i = 0; i < kMaxSignals; ++i) {
        updateSeries(i);
        if (!m_slots[i].data.isEmpty()) {
            yMin   = std::min(yMin, m_slots[i].data.minValue());
            yMax   = std::max(yMax, m_slots[i].data.maxValue());
            anyData = true;
        }
    }

    if (anyData) {
        double margin = (yMax - yMin) * 0.1;
        if (margin < 1) margin = 1;
        m_axisY->setRange(yMin - margin, yMax + margin);
    }
}

void CanSignalTrendWidget::updateSeries(int slot) {
    const auto &pts = m_slots[slot].data.points();
    QVector<QPointF> qpts;
    qpts.reserve(pts.size());
    for (const auto &p : pts)
        qpts.append({p.timeS, p.value});
    m_series[slot]->replace(qpts);
}

void CanSignalTrendWidget::onWindowChanged(int seconds) {
    m_windowS = seconds;
    m_axisX->setRange(0, seconds);
    for (int i = 0; i < kMaxSignals; ++i) {
        m_slots[i].data.trimToWindow(m_windowS);
        updateSeries(i);
    }
}

void CanSignalTrendWidget::onClearClicked() {
    for (int i = 0; i < kMaxSignals; ++i) {
        m_slots[i].data.clear();
        m_series[i]->clear();
    }
    m_startTimeS  = -1;
    m_lastRefreshS = 0;
    m_axisX->setRange(0, m_windowS);
    m_axisY->setRange(0, 100);
}

void CanSignalTrendWidget::onPauseToggled(bool paused) {
    m_paused = paused;
}
