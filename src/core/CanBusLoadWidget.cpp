#include "CanBusLoadWidget.h"
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

CanBusLoadWidget::CanBusLoadWidget(QWidget *parent) : QWidget(parent) {
    m_analyzer.setBaudRate(500000);
    m_analyzer.setWindowSec(1.0);
    buildUi();

    m_timer = new QTimer(this);
    m_timer->setInterval(kTickMs);
    connect(m_timer, &QTimer::timeout, this, &CanBusLoadWidget::onTick);
    m_timer->start();
}

// ── UI construction ───────────────────────────────────────────────────────────

void CanBusLoadWidget::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);

    // ── Config row ─────────────────────────────────────────────────────────────
    auto *cfgGroup  = new QGroupBox("Konfiguracja");
    auto *cfgLayout = new QHBoxLayout(cfgGroup);

    cfgLayout->addWidget(new QLabel("Przepustowość:"));
    m_baudCombo = new QComboBox;
    m_baudCombo->addItem("125 kbps",  125000);
    m_baudCombo->addItem("250 kbps",  250000);
    m_baudCombo->addItem("500 kbps",  500000);
    m_baudCombo->addItem("1 Mbps",   1000000);
    m_baudCombo->setCurrentIndex(2); // 500 kbps default
    cfgLayout->addWidget(m_baudCombo);

    cfgLayout->addWidget(new QLabel("Okno [s]:"));
    m_windowSpin = new QDoubleSpinBox;
    m_windowSpin->setRange(0.1, 10.0);
    m_windowSpin->setSingleStep(0.5);
    m_windowSpin->setValue(1.0);
    cfgLayout->addWidget(m_windowSpin);

    cfgLayout->addWidget(new QLabel("Alert [%]:"));
    m_alertSpin = new QSpinBox;
    m_alertSpin->setRange(10, 100);
    m_alertSpin->setValue(70);
    cfgLayout->addWidget(m_alertSpin);

    auto *resetBtn = new QPushButton("Resetuj");
    cfgLayout->addStretch(1);
    cfgLayout->addWidget(resetBtn);
    root->addWidget(cfgGroup);

    connect(m_baudCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CanBusLoadWidget::onBaudRateChanged);
    connect(m_windowSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &CanBusLoadWidget::onWindowChanged);
    connect(resetBtn, &QPushButton::clicked, this, &CanBusLoadWidget::reset);

    // ── Status row ─────────────────────────────────────────────────────────────
    auto *statusGroup  = new QGroupBox("Bieżące obciążenie");
    auto *statusLayout = new QHBoxLayout(statusGroup);

    m_loadBar = new QProgressBar;
    m_loadBar->setRange(0, 1000); // permille for smoothness
    m_loadBar->setValue(0);
    m_loadBar->setTextVisible(false);
    m_loadBar->setFixedHeight(28);
    m_loadBar->setStyleSheet(
        "QProgressBar { border: 1px solid #555; border-radius: 4px; background: #222; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 #22cc44, stop:0.6 #ffaa00, stop:1 #ff2222); border-radius: 3px; }");

    m_loadLabel = new QLabel("0.00 %");
    m_loadLabel->setStyleSheet("font-size: 22px; font-weight: bold; color: #44ff88;");
    m_loadLabel->setMinimumWidth(90);
    m_loadLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *statsLayout = new QVBoxLayout;
    m_peakLabel = new QLabel("Peak: 0.00 %");
    m_fpsLabel  = new QLabel("FPS: 0");
    m_idsLabel  = new QLabel("Unikalne ID: 0");
    m_peakLabel->setStyleSheet("color: #ffaa44;");
    statsLayout->addWidget(m_peakLabel);
    statsLayout->addWidget(m_fpsLabel);
    statsLayout->addWidget(m_idsLabel);

    statusLayout->addWidget(m_loadBar, 1);
    statusLayout->addWidget(m_loadLabel);
    statusLayout->addLayout(statsLayout);
    root->addWidget(statusGroup);

    m_alertLabel = new QLabel;
    m_alertLabel->setStyleSheet("color: #ff3333; font-weight: bold; font-size: 13px;");
    m_alertLabel->setAlignment(Qt::AlignCenter);
    m_alertLabel->setVisible(false);
    root->addWidget(m_alertLabel);

    // ── Split: chart | top-loaders table ──────────────────────────────────────
    auto *splitLayout = new QHBoxLayout;

    // Rolling chart
    auto *chart = new QChart;
    chart->setTitle("Obciążenie magistrali [%] — ostatnie 60 s");
    chart->setTheme(QChart::ChartThemeDark);
    chart->legend()->hide();

    m_loadSeries = new QLineSeries;
    m_loadSeries->setColor(QColor(0x44, 0xFF, 0x88));
    QPen pen(m_loadSeries->color());
    pen.setWidth(2);
    m_loadSeries->setPen(pen);
    chart->addSeries(m_loadSeries);

    m_axisX = new QValueAxis;
    m_axisX->setRange(-kChartWindowSec, 0.0);
    m_axisX->setLabelFormat("%.0f s");
    m_axisX->setTitleText("Czas [s]");

    m_axisY = new QValueAxis;
    m_axisY->setRange(0.0, 100.0);
    m_axisY->setLabelFormat("%.0f%%");
    m_axisY->setTitleText("Obciążenie");

    chart->addAxis(m_axisX, Qt::AlignBottom);
    chart->addAxis(m_axisY, Qt::AlignLeft);
    m_loadSeries->attachAxis(m_axisX);
    m_loadSeries->attachAxis(m_axisY);

    m_chartView = new QChartView(chart);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setMinimumHeight(220);

    splitLayout->addWidget(m_chartView, 2);

    // Top-loaders table
    auto *tableGroup  = new QGroupBox("Top-10 ID wg obciążenia");
    auto *tableLayout = new QVBoxLayout(tableGroup);

    m_topTable = new QTableWidget(0, 5);
    m_topTable->setHorizontalHeaderLabels({"CAN ID", "Obciążenie", "FPS", "Ramki", "Bity/okno"});
    m_topTable->horizontalHeader()->setStretchLastSection(true);
    m_topTable->verticalHeader()->setVisible(false);
    m_topTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_topTable->setAlternatingRowColors(true);
    m_topTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableLayout->addWidget(m_topTable);

    splitLayout->addWidget(tableGroup, 1);
    root->addLayout(splitLayout, 1);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void CanBusLoadWidget::processFrame(const CanFrame &frame) {
    m_analyzer.processFrame(frame);
}

void CanBusLoadWidget::reset() {
    m_analyzer.reset();
    m_loadSeries->clear();
    m_chartTimeSec = 0.0;
    m_topTable->setRowCount(0);
    m_loadBar->setValue(0);
    m_loadLabel->setText("0.00 %");
    m_peakLabel->setText("Peak: 0.00 %");
    m_fpsLabel->setText("FPS: 0");
    m_idsLabel->setText("Unikalne ID: 0");
    m_alertLabel->setVisible(false);
}

void CanBusLoadWidget::onBaudRateChanged() {
    int bps = m_baudCombo->currentData().toInt();
    m_analyzer.setBaudRate(static_cast<uint32_t>(bps));
}

void CanBusLoadWidget::onWindowChanged(double sec) {
    m_analyzer.setWindowSec(sec);
}

// ── Periodic tick ─────────────────────────────────────────────────────────────

void CanBusLoadWidget::onTick() {
    double load    = m_analyzer.currentLoad();
    double loadPct = load * 100.0;

    m_loadBar->setValue(static_cast<int>(loadPct * 10.0));

    QString loadStr = QString::number(loadPct, 'f', 2) + " %";
    m_loadLabel->setText(loadStr);

    // Color the label based on load
    if (loadPct >= 80.0)
        m_loadLabel->setStyleSheet("font-size: 22px; font-weight: bold; color: #ff3333;");
    else if (loadPct >= 50.0)
        m_loadLabel->setStyleSheet("font-size: 22px; font-weight: bold; color: #ffaa00;");
    else
        m_loadLabel->setStyleSheet("font-size: 22px; font-weight: bold; color: #44ff88;");

    m_peakLabel->setText(QString("Peak: %1 %").arg(m_analyzer.peakLoad() * 100.0, 0, 'f', 2));
    m_fpsLabel->setText(QString("FPS: %1").arg(m_analyzer.framesPerSec(), 0, 'f', 1));
    m_idsLabel->setText(QString("Unikalne ID: %1").arg(m_analyzer.uniqueIdCount()));

    updateChart(loadPct);
    refreshTable();
    updateAlerts(loadPct);
}

// ── Chart update ──────────────────────────────────────────────────────────────

void CanBusLoadWidget::updateChart(double loadPct) {
    m_chartTimeSec += kTickMs / 1000.0;

    m_loadSeries->append(m_chartTimeSec, loadPct);

    // Trim points older than kChartWindowSec
    double cutoff = m_chartTimeSec - kChartWindowSec;
    while (m_loadSeries->count() > 1 &&
           m_loadSeries->at(0).x() < cutoff) {
        m_loadSeries->remove(0);
    }

    m_axisX->setRange(m_chartTimeSec - kChartWindowSec, m_chartTimeSec);

    // Auto-scale Y: at least 0–20%, expand if needed
    double yMax = 20.0;
    for (int i = 0; i < m_loadSeries->count(); ++i)
        yMax = std::max(yMax, m_loadSeries->at(i).y());
    m_axisY->setRange(0.0, std::min(yMax * 1.1, 100.0));
}

// ── Table refresh ─────────────────────────────────────────────────────────────

void CanBusLoadWidget::refreshTable() {
    auto loaders = m_analyzer.topLoaders(10);
    m_topTable->setRowCount(static_cast<int>(loaders.size()));

    for (int r = 0; r < static_cast<int>(loaders.size()); ++r) {
        const auto &il = loaders[static_cast<size_t>(r)];
        auto *idItem   = new QTableWidgetItem(QString("0x%1").arg(il.id, 3, 16, QChar('0')).toUpper());
        auto *loadItem = new QTableWidgetItem(QString("%1 %").arg(il.loadFraction * 100.0, 0, 'f', 2));
        auto *fpsItem  = new QTableWidgetItem(QString::number(il.fps, 'f', 1));
        auto *cntItem  = new QTableWidgetItem(QString::number(il.framesInWindow));
        auto *bitsItem = new QTableWidgetItem(QString::number(il.bitsInWindow));

        // Color top loader prominently
        if (r == 0) {
            QColor hi(0x44, 0xFF, 0x88, 40);
            for (auto *item : {idItem, loadItem, fpsItem, cntItem, bitsItem})
                item->setBackground(hi);
        }

        m_topTable->setItem(r, 0, idItem);
        m_topTable->setItem(r, 1, loadItem);
        m_topTable->setItem(r, 2, fpsItem);
        m_topTable->setItem(r, 3, cntItem);
        m_topTable->setItem(r, 4, bitsItem);
    }
}

// ── Alert ─────────────────────────────────────────────────────────────────────

void CanBusLoadWidget::updateAlerts(double loadPct) {
    int threshold = m_alertSpin->value();
    if (loadPct >= static_cast<double>(threshold)) {
        m_alertLabel->setText(
            QString("⚠  UWAGA: Obciążenie magistrali %1% przekracza próg %2%!")
            .arg(loadPct, 0, 'f', 1).arg(threshold));
        m_alertLabel->setVisible(true);
    } else {
        m_alertLabel->setVisible(false);
    }
}
