#include "CanIdStatsWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QFont>

CanIdStatsWidget::CanIdStatsWidget(QWidget *parent) : QWidget(parent) {
    buildUi();
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &CanIdStatsWidget::onTick);
    m_timer->start(kTickMs);
}

void CanIdStatsWidget::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);

    // ── Toolbar ─────────────────────────────────────────────────────────────
    auto *bar = new QHBoxLayout;
    bar->addWidget(new QLabel("Filter ID:", this));
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText("e.g. 0x1A0");
    m_filterEdit->setMaximumWidth(140);
    bar->addWidget(m_filterEdit);
    bar->addStretch();
    m_exportBtn = new QPushButton("Export CSV…", this);
    m_resetBtn  = new QPushButton("Reset", this);
    bar->addWidget(m_exportBtn);
    bar->addWidget(m_resetBtn);
    root->addLayout(bar);

    // ── Table ────────────────────────────────────────────────────────────────
    // Columns: ID | Ext | FD | BRS | ESI | Frames | Avg Interval (ms) | Freq (Hz) |
    //          B0 min/max/avg | … | B7 min/max/avg
    static const QStringList hdr = []() {
        QStringList h = {"ID", "Ext", "FD", "BRS", "ESI", "Frames", "Avg Interval (ms)", "Freq (Hz)"};
        for (int i = 0; i < 8; ++i)
            h << QString("B%1 min").arg(i) << QString("B%1 max").arg(i) << QString("B%1 avg").arg(i);
        return h;
    }();

    m_table = new QTableWidget(0, hdr.size(), this);
    m_table->setHorizontalHeaderLabels(hdr);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->setColumnWidth(0, 80);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(true);
    m_table->setFont(QFont("Courier New", 9));
    root->addWidget(m_table, 1);

    m_infoLabel = new QLabel("IDs: 0  |  Frames: 0", this);
    root->addWidget(m_infoLabel);

    connect(m_exportBtn,  &QPushButton::clicked, this, &CanIdStatsWidget::exportCsv);
    connect(m_resetBtn,   &QPushButton::clicked, this, &CanIdStatsWidget::reset);
    connect(m_filterEdit, &QLineEdit::returnPressed, this, &CanIdStatsWidget::applyFilter);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &CanIdStatsWidget::applyFilter);
}

void CanIdStatsWidget::processFrame(const CanFrame &frame) {
    m_stats.feed(frame);
}

void CanIdStatsWidget::reset() {
    m_stats.reset();
    m_table->setRowCount(0);
    m_infoLabel->setText("IDs: 0  |  Frames: 0");
}

void CanIdStatsWidget::onTick() {
    refreshTable();
}

void CanIdStatsWidget::applyFilter() {
    refreshTable();
}

void CanIdStatsWidget::refreshTable() {
    bool sortingWasEnabled = m_table->isSortingEnabled();
    m_table->setSortingEnabled(false);

    auto profiles = m_stats.profiles();
    QString filt = m_filterEdit->text().trimmed().toLower();

    // Remove rows for IDs no longer present (after reset) and avoid full rebuild
    m_table->setRowCount(0);

    for (auto &p : profiles) {
        char idBuf[12];
        snprintf(idBuf, sizeof(idBuf), "0x%X", p.id);
        QString idStr = QString(idBuf).toLower();
        if (!filt.isEmpty() && !idStr.contains(filt)) continue;

        int row = m_table->rowCount();
        m_table->insertRow(row);

        auto cell = [&](int col, const QString &txt) {
            auto *item = new QTableWidgetItem(txt);
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(row, col, item);
        };

        cell(0, idBuf);
        cell(1, p.extended ? "Y" : "N");
        cell(2, p.fdFrameCount > 0 ? QString::number(p.fdFrameCount) : "-");
        cell(3, p.brsCount     > 0 ? QString::number(p.brsCount)     : "-");
        cell(4, p.esiCount     > 0 ? QString::number(p.esiCount)     : "-");
        cell(5, QString::number(p.frameCount));
        cell(6, QString::number(p.avgIntervalMs(), 'f', 2));
        cell(7, QString::number(p.freqHz(), 'f', 1));

        for (int i = 0; i < 8; ++i) {
            if (i < p.maxDlc) {
                cell(8 + i*3,     QString::number(p.bytes[i].minVal));
                cell(8 + i*3 + 1, QString::number(p.bytes[i].maxVal));
                cell(8 + i*3 + 2, QString::number(p.byteAvg(i), 'f', 1));
            } else {
                cell(8 + i*3,     "-");
                cell(8 + i*3 + 1, "-");
                cell(8 + i*3 + 2, "-");
            }
        }
    }

    if (sortingWasEnabled) m_table->setSortingEnabled(true);
    m_infoLabel->setText(QString("IDs: %1  |  Frames: %2")
        .arg(m_stats.uniqueIdCount()).arg(m_stats.totalFrameCount()));
}

void CanIdStatsWidget::exportCsv() {
    QString path = QFileDialog::getSaveFileName(this, "Export Statistics", "", "CSV files (*.csv)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Export", "Cannot write to: " + path);
        return;
    }
    QTextStream out(&f);
    out << QString::fromStdString(m_stats.toCsv());
    f.close();
}
