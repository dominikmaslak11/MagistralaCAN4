#include "SignalPlotterWidget.h"
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPixmap>
#include <QSplitter>
#include <QVBoxLayout>
#include <QtCharts/QValueAxis>
#include <algorithm>
#include <cmath>

// ── Static data ───────────────────────────────────────────────────────────────

const QColor SignalPlotterWidget::kColors[kMaxChannels] = {
    QColor(0xFF, 0x44, 0x44), // red
    QColor(0x44, 0xAA, 0xFF), // blue
    QColor(0x44, 0xFF, 0x88), // green
    QColor(0xFF, 0xAA, 0x44), // orange
};

// ── Constructor ───────────────────────────────────────────────────────────────

SignalPlotterWidget::SignalPlotterWidget(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);

    // ── Toolbar ───────────────────────────────────────────────────────────────
    auto *toolbar = new QHBoxLayout;

    m_windowCombo = new QComboBox;
    m_windowCombo->addItems({"5 s", "10 s", "30 s", "60 s", "120 s"});
    m_windowCombo->setCurrentIndex(1); // 10 s default

    m_normalizeCheck = new QCheckBox("Normalizuj [0–1]");
    m_clearBtn       = new QPushButton("Wyczyść");
    m_exportBtn      = new QPushButton("Eksportuj PNG");
    m_refreshBtn     = new QPushButton("Odśwież DBC");

    toolbar->addWidget(new QLabel("Okno:"));
    toolbar->addWidget(m_windowCombo);
    toolbar->addWidget(m_normalizeCheck);
    toolbar->addStretch(1);
    toolbar->addWidget(m_refreshBtn);
    toolbar->addWidget(m_clearBtn);
    toolbar->addWidget(m_exportBtn);
    root->addLayout(toolbar);

    // ── Channel strip ─────────────────────────────────────────────────────────
    auto *chBar = new QHBoxLayout;
    chBar->addWidget(new QLabel("Kanały:"));
    for (int i = 0; i < kMaxChannels; ++i) {
        auto *box = new QWidget;
        auto *hl  = new QHBoxLayout(box);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(2);

        m_chLabel[i] = new QLabel(QString("CH%1: —").arg(i + 1));
        m_chLabel[i]->setStyleSheet(
            QString("color: %1; font-weight: bold; min-width: 160px;")
            .arg(kColors[i].name()));

        m_removeBtn[i] = new QPushButton("✕");
        m_removeBtn[i]->setFixedSize(20, 20);
        m_removeBtn[i]->setEnabled(false);
        m_removeBtn[i]->setStyleSheet("color: #ff4444; font-weight: bold; border: none;");

        hl->addWidget(m_chLabel[i]);
        hl->addWidget(m_removeBtn[i]);
        chBar->addWidget(box);

        connect(m_removeBtn[i], &QPushButton::clicked, this,
                [this, i]() { removeChannel(i); });
    }
    chBar->addStretch(1);
    root->addLayout(chBar);

    // ── Splitter: signal tree | chart ─────────────────────────────────────────
    auto *splitter = new QSplitter(Qt::Horizontal);

    m_signalTree = new QTreeWidget;
    m_signalTree->setHeaderLabel("Sygnały DBC");
    m_signalTree->setMaximumWidth(260);
    m_signalTree->setMinimumWidth(140);
    m_signalTree->setToolTip("Kliknij 2× aby dodać do wykresu");
    splitter->addWidget(m_signalTree);

    // Chart
    m_chart = new QChart;
    m_chart->legend()->setVisible(false);
    m_chart->setTitle("Przebiegi sygnałów DBC");
    m_chart->setTheme(QChart::ChartThemeDark);
    m_chart->setMargins(QMargins(4, 4, 4, 4));

    m_timeAxis = new QValueAxis;
    m_timeAxis->setTitleText("Czas [s]");
    m_timeAxis->setRange(0, m_windowSec);
    m_timeAxis->setTickCount(6);
    m_chart->addAxis(m_timeAxis, Qt::AlignBottom);

    m_valueAxis = new QValueAxis;
    m_valueAxis->setTitleText("Wartość");
    m_valueAxis->setRange(-1, 1);
    m_chart->addAxis(m_valueAxis, Qt::AlignLeft);

    for (int i = 0; i < kMaxChannels; ++i) {
        auto *s = new QLineSeries;
        s->setColor(kColors[i]);
        s->setPen(QPen(kColors[i], 1.5));
        m_chart->addSeries(s);
        s->attachAxis(m_timeAxis);
        s->attachAxis(m_valueAxis);
        m_channels[i].series = s;
    }

    m_chartView = new QChartView(m_chart);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    splitter->addWidget(m_chartView);
    splitter->setSizes({200, 800});

    root->addWidget(splitter, 1);

    // ── Stats bar ─────────────────────────────────────────────────────────────
    m_statsLabel = new QLabel("Brak aktywnych kanałów — załaduj DBC i kliknij 2× sygnał");
    m_statsLabel->setStyleSheet("color: #aaaaaa; font-size: 11px;");
    root->addWidget(m_statsLabel);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_signalTree, &QTreeWidget::itemDoubleClicked,
            this, &SignalPlotterWidget::onSignalDoubleClicked);
    connect(m_windowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SignalPlotterWidget::onWindowChanged);
    connect(m_normalizeCheck, &QCheckBox::toggled,
            this, &SignalPlotterWidget::onNormalizeToggled);
    connect(m_clearBtn,   &QPushButton::clicked, this, &SignalPlotterWidget::clearPlot);
    connect(m_exportBtn,  &QPushButton::clicked, this, &SignalPlotterWidget::exportPng);
    connect(m_refreshBtn, &QPushButton::clicked, this, &SignalPlotterWidget::refreshSignalTree);
}

// ── Public API ────────────────────────────────────────────────────────────────

void SignalPlotterWidget::setDbcParser(const DbcParser *dbc) {
    m_dbc = dbc;
    refreshSignalTree();
}

void SignalPlotterWidget::processFrame(const CanFrame &frame) {
    if (!m_dbc) return;

    // Timestamp → seconds from first frame
    if (m_firstTs == 0 && frame.timestamp > 0)
        m_firstTs = frame.timestamp;
    double tSec = (frame.timestamp >= m_firstTs)
                ? static_cast<double>(frame.timestamp - m_firstTs) / 1e6
                : 0.0;

    // Decode signals for this CAN ID
    bool anyActive = false;
    for (int i = 0; i < kMaxChannels; ++i)
        if (m_channels[i].active && m_channels[i].msgId == frame.id)
            anyActive = true;
    if (!anyActive) return;

    QHash<QString, double> decoded =
        m_dbc->decodeSignals(frame.id, frame.data.data(), frame.dlc);

    for (int i = 0; i < kMaxChannels; ++i) {
        auto &ch = m_channels[i];
        if (!ch.active || ch.msgId != frame.id) continue;
        if (!decoded.contains(ch.name)) continue;

        double val = decoded[ch.name];
        ch.lastVal = val;
        if (val < ch.minVal) ch.minVal = val;
        if (val > ch.maxVal) ch.maxVal = val;

        double plotVal = m_normalize
            ? (ch.maxVal > ch.minVal
                ? (val - ch.minVal) / (ch.maxVal - ch.minVal) * (double)i + (double)i
                : (double)i)
            : val;

        ch.series->append(tSec, plotVal);
        trimSeries(i, tSec);
    }

    // Update X axis
    double xMin = std::max(0.0, tSec - m_windowSec);
    m_timeAxis->setRange(xMin, xMin + m_windowSec);

    rescaleYAxis();
    updateStatsLabel();
}

void SignalPlotterWidget::clearPlot() {
    m_firstTs = 0;
    for (int i = 0; i < kMaxChannels; ++i) {
        m_channels[i].series->clear();
        m_channels[i].minVal =  1e18;
        m_channels[i].maxVal = -1e18;
        m_channels[i].lastVal = 0.0;
    }
    m_timeAxis->setRange(0, m_windowSec);
    m_valueAxis->setRange(-1, 1);
    updateStatsLabel();
}

void SignalPlotterWidget::refreshSignalTree() {
    m_signalTree->clear();
    if (!m_dbc) return;

    for (const auto &msg : m_dbc->messages()) {
        auto *msgItem = new QTreeWidgetItem(m_signalTree);
        msgItem->setText(0, QString("0x%1  %2")
                         .arg(msg.id, 3, 16, QChar('0')).toUpper()
                         .arg(msg.name));
        msgItem->setData(0, Qt::UserRole, 0u); // mark as message node

        for (const auto &sig : msg.sigList) {
            auto *sigItem = new QTreeWidgetItem(msgItem);
            QString label = sig.name;
            if (!sig.unit.isEmpty()) label += "  [" + sig.unit + "]";
            sigItem->setText(0, label);
            sigItem->setData(0, Qt::UserRole + 0, sig.name);
            sigItem->setData(0, Qt::UserRole + 1, msg.id);
        }
        msgItem->setExpanded(true);
    }

    if (m_signalTree->topLevelItemCount() == 0) {
        auto *placeholder = new QTreeWidgetItem(m_signalTree);
        placeholder->setText(0, "(brak DBC — załaduj plik)");
        placeholder->setDisabled(true);
    }
}

void SignalPlotterWidget::exportPng() {
    QString path = QFileDialog::getSaveFileName(this, "Eksportuj wykres",
        "signal_plot.png", "PNG (*.png)");
    if (path.isEmpty()) return;
    QPixmap px = m_chartView->grab();
    if (px.save(path))
        m_statsLabel->setText("Zapisano: " + path);
}

// ── Private slots ─────────────────────────────────────────────────────────────

void SignalPlotterWidget::onSignalDoubleClicked(QTreeWidgetItem *item, int /*column*/) {
    if (!item || !item->parent()) return; // clicked a message node
    if (!m_dbc) return;

    QString sigName = item->data(0, Qt::UserRole + 0).toString();
    uint32_t msgId  = item->data(0, Qt::UserRole + 1).toUInt();
    if (sigName.isEmpty()) return;

    // Find signal definition
    DbcMessage msg = m_dbc->messageForId(msgId);
    for (const auto &sig : msg.sigList) {
        if (sig.name == sigName) {
            addToChannel(sigName, msgId, sig);
            return;
        }
    }
}

void SignalPlotterWidget::onWindowChanged(int idx) {
    static const double kWindows[] = {5, 10, 30, 60, 120};
    if (idx >= 0 && idx < 5)
        m_windowSec = kWindows[idx];
}

void SignalPlotterWidget::onNormalizeToggled(bool on) {
    m_normalize = on;
    // Replot from existing series data with new mode
    if (on)
        m_valueAxis->setTitleText("Wartość (znorm.)");
    else
        m_valueAxis->setTitleText("Wartość");
    clearPlot();
}

void SignalPlotterWidget::removeChannel(int ch) {
    if (ch < 0 || ch >= kMaxChannels) return;
    m_channels[ch].active = false;
    m_channels[ch].name.clear();
    m_channels[ch].series->clear();
    m_channels[ch].minVal =  1e18;
    m_channels[ch].maxVal = -1e18;
    m_chLabel[ch]->setText(QString("CH%1: —").arg(ch + 1));
    m_removeBtn[ch]->setEnabled(false);
    updateStatsLabel();
}

// ── Private helpers ───────────────────────────────────────────────────────────

void SignalPlotterWidget::addToChannel(const QString &sigName, uint32_t msgId,
                                       const DbcSignal &sig) {
    // Find a free slot
    for (int i = 0; i < kMaxChannels; ++i) {
        if (m_channels[i].active && m_channels[i].name == sigName &&
            m_channels[i].msgId == msgId)
            return; // already plotting

        if (!m_channels[i].active) {
            auto &ch      = m_channels[i];
            ch.active     = true;
            ch.name       = sigName;
            ch.msgId      = msgId;
            ch.scale      = sig.scale;
            ch.offset     = sig.offset;
            ch.minVal     =  1e18;
            ch.maxVal     = -1e18;
            ch.lastVal    = 0.0;
            ch.series->clear();

            QString label = QString("CH%1: %2").arg(i + 1).arg(sigName);
            if (!sig.unit.isEmpty()) label += " [" + sig.unit + "]";
            m_chLabel[i]->setText(label);
            m_removeBtn[i]->setEnabled(true);

            m_statsLabel->setText(
                QString("CH%1 ← %2  (ID 0x%3)")
                .arg(i + 1).arg(sigName).arg(msgId, 3, 16, QChar('0')).toUpper());
            return;
        }
    }
    m_statsLabel->setText("Wszystkie 4 kanały zajęte — usuń jeden (✕) przed dodaniem nowego");
}

void SignalPlotterWidget::trimSeries(int ch, double latestSec) {
    auto *s = m_channels[ch].series;
    double cutoff = latestSec - m_windowSec;
    // Remove points older than cutoff — QLineSeries.remove(index) is O(N),
    // so batch-remove from front
    const auto &pts = s->points();
    int removeCount = 0;
    for (const auto &p : pts) {
        if (p.x() < cutoff) ++removeCount;
        else break;
    }
    for (int j = 0; j < removeCount; ++j)
        s->remove(0);
}

void SignalPlotterWidget::rescaleYAxis() {
    double lo =  1e18;
    double hi = -1e18;
    for (int i = 0; i < kMaxChannels; ++i) {
        if (!m_channels[i].active) continue;
        const auto &pts = m_channels[i].series->points();
        for (const auto &p : pts) {
            if (p.y() < lo) lo = p.y();
            if (p.y() > hi) hi = p.y();
        }
    }
    if (lo >= hi) { lo = -1.0; hi = 1.0; }
    double margin = (hi - lo) * 0.08;
    if (margin < 0.001) margin = 0.1;
    m_valueAxis->setRange(lo - margin, hi + margin);
}

void SignalPlotterWidget::updateStatsLabel() {
    bool any = false;
    QString s;
    for (int i = 0; i < kMaxChannels; ++i) {
        if (!m_channels[i].active) continue;
        const auto &ch = m_channels[i];
        any = true;
        QString minStr = (ch.minVal >  1e17) ? "—" : QString::number(ch.minVal, 'f', 3);
        QString maxStr = (ch.maxVal < -1e17) ? "—" : QString::number(ch.maxVal, 'f', 3);
        s += QString("  CH%1 %2: akt=%3  min=%4  max=%5  |")
             .arg(i + 1).arg(ch.name)
             .arg(ch.lastVal, 0, 'f', 3)
             .arg(minStr).arg(maxStr);
    }
    if (!any)
        m_statsLabel->setText("Brak aktywnych kanałów");
    else
        m_statsLabel->setText(s.left(s.length() - 2));
}
