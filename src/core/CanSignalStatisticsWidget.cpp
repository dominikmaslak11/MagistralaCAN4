#include "CanSignalStatisticsWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <algorithm>

static const char *kSS =
    "QTableWidget { background:#0a0e17; color:#c0c0c0; gridline-color:#2a2a3c; "
    "  font-family:Consolas; font-size:11px; selection-background-color:#1a2a3a; }"
    "QHeaderView::section { background:#1a1a2e; color:#ff66cc; border:1px solid #2a2a3c; "
    "  padding:4px; font-weight:bold; font-family:Consolas; }"
    "QPushButton { background:#1a1a2e; color:#00ffaa; border:1px solid #e94560; "
    "  border-radius:4px; padding:4px 10px; font-family:Consolas; }"
    "QPushButton:hover { background:#2a2a3e; }"
    "QLineEdit { background:#0a0e17; color:#00ffaa; border:1px solid #e94560; "
    "  border-radius:3px; padding:3px 6px; font-family:Consolas; }"
    "QLabel { color:#c0c0c0; font-family:Consolas; }";

// Build a mini ASCII histogram bar (10 buckets → 10-char string)
static QString histBar(const SignalStats &s) {
    if (!s.valid()) return QString(10, '-');
    static const char *blocks = " ▁▂▃▄▅▆▇█";
    uint64_t maxBucket = 1;
    for (auto v : s.histogram)
        if (v > maxBucket) maxBucket = v;
    QString bar;
    for (auto v : s.histogram) {
        int level = static_cast<int>((v * 8) / maxBucket);
        bar += QString::fromUtf8(blocks + level * 3, 3); // UTF-8 char
    }
    return bar;
}

CanSignalStatisticsWidget::CanSignalStatisticsWidget(QWidget *parent) : QWidget(parent) {
    setStyleSheet(kSS);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    // ── Header ───────────────────────────────────────────────────────────────
    auto *header = new QHBoxLayout;

    auto *title = new QLabel("Statystyki sygnałów DBC — live (min/max/μ/σ + histogram)");
    title->setStyleSheet("color:#ff66cc; font-weight:bold; font-size:13px; font-family:Consolas;");
    header->addWidget(title);
    header->addStretch();

    header->addWidget(new QLabel("Filtr:"));
    m_filterEdit = new QLineEdit;
    m_filterEdit->setPlaceholderText("nazwa sygnału...");
    m_filterEdit->setFixedWidth(160);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &CanSignalStatisticsWidget::applyFilter);
    header->addWidget(m_filterEdit);

    auto *resetBtn = new QPushButton("Reset");
    connect(resetBtn, &QPushButton::clicked, this, &CanSignalStatisticsWidget::resetStats);
    header->addWidget(resetBtn);

    auto *exportBtn = new QPushButton("CSV");
    connect(exportBtn, &QPushButton::clicked, this, &CanSignalStatisticsWidget::exportCsv);
    header->addWidget(exportBtn);

    lay->addLayout(header);

    m_statsLbl = new QLabel("Wczytaj plik DBC i uruchom przechwytywanie");
    m_statsLbl->setStyleSheet("color:#ffaa00; font-family:Consolas; font-size:11px;");
    lay->addWidget(m_statsLbl);

    // ── Table ────────────────────────────────────────────────────────────────
    // Columns: Sygnał | Jedn. | Próbki | Min | Max | Średnia | Odch.std | CV% | Histogram
    m_table = new QTableWidget(0, 9);
    m_table->setHorizontalHeaderLabels(
        {"Sygnał", "Jedn.", "Próbki", "Min", "Max", "Średnia", "Odch.std", "CV%", "Histogram"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(8, QHeaderView::Stretch);
    m_table->horizontalHeader()->setDefaultSectionSize(80);
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSortingEnabled(true);
    lay->addWidget(m_table, 1);

    // Auto-refresh every 2 s
    auto *timer = new QTimer(this);
    timer->setInterval(2000);
    connect(timer, &QTimer::timeout, this, &CanSignalStatisticsWidget::refreshTable);
    timer->start();
}

void CanSignalStatisticsWidget::setDbcParser(const DbcParser *parser) {
    m_dbc = parser;
    m_stats.setDbc(parser);
    m_stats.reset();
    m_table->setRowCount(0);
    m_statsLbl->setText(parser ? "DBC załadowany — oczekiwanie na ramki..." : "Brak DBC");
}

void CanSignalStatisticsWidget::processFrame(const CanFrame &frame) {
    if (!m_dbc) return;
    m_stats.add(frame);
}

void CanSignalStatisticsWidget::refreshTable() {
    if (!m_dbc) return;
    auto allStats = m_stats.allStats();

    // Filter
    if (!m_filter.isEmpty()) {
        allStats.erase(std::remove_if(allStats.begin(), allStats.end(),
            [this](const SignalStats &s) {
                return !s.signalName.contains(m_filter, Qt::CaseInsensitive);
            }), allStats.end());
    }

    m_table->setSortingEnabled(false);
    m_table->setRowCount(static_cast<int>(allStats.size()));

    for (int row = 0; row < static_cast<int>(allStats.size()); ++row)
        updateRow(row, allStats[row]);

    m_table->setSortingEnabled(true);
    m_statsLbl->setText(
        QString("Sygnałów: %1 | Próbek łącznie: %2 | DBC załadowany")
            .arg(allStats.size())
            .arg(allStats.isEmpty() ? 0 : allStats[0].sampleCount));
}

void CanSignalStatisticsWidget::updateRow(int row, const SignalStats &s) {
    // Color coding based on CV (coefficient of variation)
    const double cv = (s.valid() && s.mean != 0) ? (s.stdDev / std::abs(s.mean) * 100.0) : 0.0;
    const QColor bg = (cv < 5)  ? QColor("#0a1a0a")   // very stable — dark green
                    : (cv < 20) ? QColor("#1a180a")   // moderate — dark yellow
                                : QColor("#1a0a0a");  // high variance — dark red

    auto setCell = [&](int col, const QString &text, Qt::Alignment align = Qt::AlignCenter) {
        auto *it = m_table->item(row, col);
        if (!it) { it = new QTableWidgetItem; m_table->setItem(row, col, it); }
        it->setText(text);
        it->setBackground(bg);
        it->setForeground(QColor("#c0c0c0"));
        it->setTextAlignment(align);
    };

    setCell(0, s.signalName, Qt::AlignLeft | Qt::AlignVCenter);
    setCell(1, s.unit);
    setCell(2, QString::number(s.sampleCount));
    setCell(3, s.valid() ? QString::number(s.minVal, 'g', 5) : "—");
    setCell(4, s.valid() ? QString::number(s.maxVal, 'g', 5) : "—");
    setCell(5, s.valid() ? QString::number(s.mean,   'g', 5) : "—");
    setCell(6, s.valid() ? QString::number(s.stdDev, 'g', 4) : "—");
    setCell(7, s.valid() ? QString::number(cv, 'f', 1) + "%" : "—");

    // Histogram column — color-coded bar
    auto *histIt = m_table->item(row, 8);
    if (!histIt) { histIt = new QTableWidgetItem; m_table->setItem(row, 8, histIt); }
    histIt->setText(histBar(s));
    histIt->setBackground(bg);
    histIt->setForeground(QColor("#00ffaa"));
    histIt->setFont(QFont("Consolas", 11));
    histIt->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // Tooltip: full stats
    if (s.valid()) {
        const QString tip = QString(
            "Signal: %1\nSamples: %2\nMin: %3\nMax: %4\nMean: %5\nStdDev: %6\nCV: %7%")
            .arg(s.signalName).arg(s.sampleCount)
            .arg(s.minVal, 0, 'g').arg(s.maxVal, 0, 'g')
            .arg(s.mean, 0, 'g').arg(s.stdDev, 0, 'g')
            .arg(cv, 0, 'f', 2);
        for (int c = 0; c < 9; ++c)
            if (auto *it = m_table->item(row, c)) it->setToolTip(tip);
    }
}

void CanSignalStatisticsWidget::resetStats() {
    m_stats.reset();
    m_table->setRowCount(0);
    m_statsLbl->setText("Zresetowano");
}

void CanSignalStatisticsWidget::applyFilter(const QString &text) {
    m_filter = text;
    refreshTable();
}

void CanSignalStatisticsWidget::exportCsv() {
    const QString path = QFileDialog::getSaveFileName(
        this, "Eksportuj statystyki sygnałów", "signal_stats.csv",
        "CSV (*.csv);;Wszystkie pliki (*)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&f);

    out << "Signal,Unit,Samples,Min,Max,Mean,StdDev,CV%\n";
    for (const auto &s : m_stats.allStats()) {
        if (!s.valid()) continue;
        const double cv = (s.mean != 0) ? (s.stdDev / std::abs(s.mean) * 100.0) : 0.0;
        out << QString("%1,%2,%3,%4,%5,%6,%7,%8\n")
               .arg(s.signalName).arg(s.unit).arg(s.sampleCount)
               .arg(s.minVal).arg(s.maxVal).arg(s.mean).arg(s.stdDev)
               .arg(cv, 0, 'f', 2);
    }
}
