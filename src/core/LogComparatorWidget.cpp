#include "LogComparatorWidget.h"
#include "Logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSplitter>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QScrollBar>
#include <QFileDialog>
#include <cmath>

LogComparatorWidget::LogComparatorWidget(QWidget *parent) : QWidget(parent) {
    setupUi();
}

void LogComparatorWidget::setupUi() {
    auto *main = new QVBoxLayout(this);

    // ── Toolbar ──
    auto *toolbar = new QHBoxLayout;
    m_loadLeftBtn = new QPushButton("Wczytaj lewy plik");
    m_loadRightBtn = new QPushButton("Wczytaj prawy plik");
    m_compareBtn = new QPushButton("Porównaj");
    m_exportBtn = new QPushButton("Eksportuj raport HTML");

    toolbar->addWidget(m_loadLeftBtn);
    toolbar->addWidget(m_loadRightBtn);
    toolbar->addWidget(m_compareBtn);
    toolbar->addWidget(m_exportBtn);
    toolbar->addStretch();

    m_statsLabel = new QLabel("Wczytaj dwa pliki candump i kliknij Porównaj.");
    m_statsLabel->setStyleSheet("color: #ffaa00; font-weight: bold;");
    toolbar->addWidget(m_statsLabel);
    main->addLayout(toolbar);

    // ── Pasek filtrów ──
    auto *filterBar = new QHBoxLayout;
    filterBar->addWidget(new QLabel("Filtr ID:"));
    m_filterId = new QLineEdit; m_filterId->setPlaceholderText("np. 0x123"); m_filterId->setMaximumWidth(90);
    filterBar->addWidget(m_filterId);
    filterBar->addWidget(new QLabel("Czas od (s):"));
    m_filterTimeFrom = new QLineEdit; m_filterTimeFrom->setMaximumWidth(60);
    filterBar->addWidget(m_filterTimeFrom);
    filterBar->addWidget(new QLabel("do (s):"));
    m_filterTimeTo = new QLineEdit; m_filterTimeTo->setMaximumWidth(60);
    filterBar->addWidget(m_filterTimeTo);
    m_filterApplyBtn = new QPushButton("Zastosuj filtr");
    filterBar->addWidget(m_filterApplyBtn);
    filterBar->addStretch();
    main->addLayout(filterBar);

    // ── Etykiety plików ──
    auto *labels = new QHBoxLayout;
    m_leftFileLabel = new QLabel("Lewy: —");
    m_leftFileLabel->setStyleSheet("color: #ffaa00;");
    m_rightFileLabel = new QLabel("Prawy: —");
    m_rightFileLabel->setStyleSheet("color: #00aaff;");
    labels->addWidget(m_leftFileLabel);
    labels->addStretch();
    labels->addWidget(m_rightFileLabel);
    main->addLayout(labels);

    // ── Podwójna tabela (QSplitter) ──
    auto *splitter = new QSplitter(Qt::Horizontal);

    m_leftTable = new QTableWidget(0, 5);
    m_leftTable->setHorizontalHeaderLabels({"#", "Czas (s)", "ID", "DLC", "Dane"});
    m_leftTable->verticalHeader()->hide();
    m_leftTable->horizontalHeader()->setStretchLastSection(true);
    m_leftTable->setShowGrid(true);
    m_leftTable->setAlternatingRowColors(false);
    m_leftTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_leftTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    splitter->addWidget(m_leftTable);

    m_rightTable = new QTableWidget(0, 5);
    m_rightTable->setHorizontalHeaderLabels({"#", "Czas (s)", "ID", "DLC", "Dane"});
    m_rightTable->verticalHeader()->hide();
    m_rightTable->horizontalHeader()->setStretchLastSection(true);
    m_rightTable->setShowGrid(true);
    m_rightTable->setAlternatingRowColors(false);
    m_rightTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_rightTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    splitter->addWidget(m_rightTable);

    main->addWidget(splitter, 1);

    // ── Synchroniczne przewijanie ──
    connect(m_leftTable->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &LogComparatorWidget::onScrollSync);
    connect(m_rightTable->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int v) {
        m_leftTable->verticalScrollBar()->blockSignals(true);
        m_leftTable->verticalScrollBar()->setValue(v);
        m_leftTable->verticalScrollBar()->blockSignals(false);
    });

    // ── Sygnały ──
    connect(m_loadLeftBtn, &QPushButton::clicked, this, &LogComparatorWidget::loadLeft);
    connect(m_loadRightBtn, &QPushButton::clicked, this, &LogComparatorWidget::loadRight);
    connect(m_compareBtn, &QPushButton::clicked, this, &LogComparatorWidget::runCompare);
    connect(m_exportBtn, &QPushButton::clicked, this, &LogComparatorWidget::exportReport);
    connect(m_filterApplyBtn, &QPushButton::clicked, this, &LogComparatorWidget::applyFilter);

    setStyleSheet(R"(
        QPushButton { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560; border-radius: 4px; padding: 5px 12px; font-weight: bold; }
        QPushButton:hover { background: #e94560; color: #0a0e17; }
        QLineEdit { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560; border-radius: 4px; padding: 3px 6px; }
        QTableWidget { background-color: #0a0e17; color: #c0c0c0; gridline-color: #2a2a3c; font-family: Consolas, monospace; font-size: 11px; }
        QHeaderView::section { background-color: #1a1a2e; color: #ff66cc; font-weight: bold; padding: 4px; border: none; border-bottom: 2px solid #e94560; }
        QLabel { color: #c0c0c0; }
    )");
}

// ── Ładowanie candump ────────────────────────────────────────

QVector<CanFrame> LogComparatorWidget::loadCandump(const QString &path) {
    QVector<CanFrame> frames;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return frames;

    QTextStream in(&file);
    QString line;
    while (in.readLineInto(&line)) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith("//")) continue;

        // Format: (timestamp) interface id#data
        // np: (1715123456.789123) vcan0 123#AABBCCDD
        int p1 = line.indexOf('(');
        int p2 = line.indexOf(')');
        if (p1 < 0 || p2 < 0) continue;

        QString tsStr = line.mid(p1 + 1, p2 - p1 - 1);
        double tsSec = tsStr.toDouble();

        int hashPos = line.indexOf('#', p2);
        if (hashPos < 0) continue;

        // ID przed #
        int idStart = p2 + 1;
        while (idStart < hashPos && line[idStart].isSpace()) idStart++;
        QString idStr = line.mid(idStart, hashPos - idStart).trimmed();
        bool ok;
        uint32_t id = idStr.toUInt(&ok, 16);
        if (!ok) continue;

        // Dane po #
        QString dataStr = line.mid(hashPos + 1).trimmed();
        CanFrame f;
        f.id = id;
        f.timestamp = static_cast<uint64_t>(tsSec * 1'000'000.0);
        f.dlc = 0;

        for (int i = 0; i < dataStr.length() && f.dlc < 64; i += 2) {
            if (i + 1 >= dataStr.length()) break;
            f.data[f.dlc] = static_cast<uint8_t>(dataStr.mid(i, 2).toUInt(&ok, 16));
            if (ok) f.dlc++;
        }

        frames.append(f);
    }
    return frames;
}

void LogComparatorWidget::loadLeft() {
    QString p = QFileDialog::getOpenFileName(this, "Wczytaj lewy plik candump", "", "Logi (*.log *.txt);;Wszystkie (*)");
    if (p.isEmpty()) return;
    m_leftFrames = loadCandump(p);
    m_leftFileLabel->setText(QString("Lewy: %1 (%2 ramek)").arg(p.section('/', -1)).arg(m_leftFrames.size()));
}

void LogComparatorWidget::loadRight() {
    QString p = QFileDialog::getOpenFileName(this, "Wczytaj prawy plik candump", "", "Logi (*.log *.txt);;Wszystkie (*)");
    if (p.isEmpty()) return;
    m_rightFrames = loadCandump(p);
    m_rightFileLabel->setText(QString("Prawy: %1 (%2 ramek)").arg(p.section('/', -1)).arg(m_rightFrames.size()));
}

// ── Porównywanie ─────────────────────────────────────────────

void LogComparatorWidget::runCompare() {
    m_leftTable->setRowCount(0);
    m_rightTable->setRowCount(0);
    m_leftStatus.clear();
    m_rightStatus.clear();
    m_identical = m_different = m_leftOnly = m_rightOnly = 0;

    int maxRows = std::max(m_leftFrames.size(), m_rightFrames.size());

    // Algorytm: porównanie pozycyjne + timestamp matching dla nadmiarowych
    auto frameStr = [](const CanFrame &f) {
        QString d;
        for (int i = 0; i < f.dlc && i < 8; ++i)
            d += QString("%1").arg(f.data[i], 2, 16, QChar('0')).toUpper();
        return d;
    };

    m_leftStatus.resize(maxRows);
    m_rightStatus.resize(maxRows);

    int li = 0, ri = 0; // indeksy w plikach

    for (int pos = 0; pos < maxRows; ++pos) {
        const CanFrame *lf = (li < m_leftFrames.size()) ? &m_leftFrames[li] : nullptr;
        const CanFrame *rf = (ri < m_rightFrames.size()) ? &m_rightFrames[ri] : nullptr;

        if (!lf && !rf) break;

        if (lf && rf) {
            if (lf->id == rf->id && frameStr(*lf) == frameStr(*rf)) {
                m_leftStatus[li] = Identical;
                m_rightStatus[ri] = Identical;
                m_identical++;
                li++; ri++;
            } else {
                // Różne – spróbuj timestamp matching
                double tDiff = std::abs(static_cast<double>(lf->timestamp - rf->timestamp)) / 1'000'000.0;
                if (tDiff < 0.1 && lf->id == rf->id) {
                    // Ten sam ID w oknie czasowym – oznacz jako różne dane
                    m_leftStatus[li] = Different;
                    m_rightStatus[ri] = Different;
                    m_different++;
                    li++; ri++;
                } else {
                    // Decyzja: które ID jest wcześniej timestampem?
                    if (lf->timestamp <= rf->timestamp) {
                        m_leftStatus[li] = LeftOnly;
                        m_leftOnly++;
                        li++;
                    } else {
                        m_rightStatus[ri] = RightOnly;
                        m_rightOnly++;
                        ri++;
                    }
                }
            }
        } else if (lf) {
            m_leftStatus[li] = LeftOnly;
            m_leftOnly++;
            li++;
        } else if (rf) {
            m_rightStatus[ri] = RightOnly;
            m_rightOnly++;
            ri++;
        }
    }

    // Wypełnij tabele
    int leftRow = 0, rightRow = 0;

    for (int i = 0; i < m_leftFrames.size(); ++i) {
        if (i >= m_leftStatus.size()) break;
        if (m_leftStatus[i] == Skipped) continue;

        const auto &f = m_leftFrames[i];
        m_leftTable->insertRow(leftRow);

        auto *idxItem = new QTableWidgetItem(QString::number(i + 1));
        m_leftTable->setItem(leftRow, 0, idxItem);
        m_leftTable->setItem(leftRow, 1, new QTableWidgetItem(QString::number(f.timestamp / 1'000'000.0, 'f', 6)));
        m_leftTable->setItem(leftRow, 2, new QTableWidgetItem(QString("0x%1").arg(f.id, 3, 16, QChar('0')).toUpper()));
        m_leftTable->setItem(leftRow, 3, new QTableWidgetItem(QString::number(f.dlc)));
        QString d; for (int b = 0; b < f.dlc && b < 8; ++b) d += QString("%1").arg(f.data[b], 2, 16, QChar('0')).toUpper();
        m_leftTable->setItem(leftRow, 4, new QTableWidgetItem(d));

        QColor bg;
        switch (m_leftStatus[i]) {
        case Identical:  bg = QColor("#0a2e0a"); break; // ciemnozielony
        case Different:  bg = QColor("#2e0a0a"); break; // ciemnoczerwony
        case LeftOnly:   bg = QColor("#2e2a0a"); break; // ciemnożółty
        default: break;
        }
        for (int c = 0; c < 5; ++c)
            if (auto *it = m_leftTable->item(leftRow, c)) it->setBackground(bg);

        leftRow++;
    }

    for (int i = 0; i < m_rightFrames.size(); ++i) {
        if (i >= m_rightStatus.size()) break;
        if (m_rightStatus[i] == Skipped) continue;

        const auto &f = m_rightFrames[i];
        m_rightTable->insertRow(rightRow);

        m_rightTable->setItem(rightRow, 0, new QTableWidgetItem(QString::number(i + 1)));
        m_rightTable->setItem(rightRow, 1, new QTableWidgetItem(QString::number(f.timestamp / 1'000'000.0, 'f', 6)));
        m_rightTable->setItem(rightRow, 2, new QTableWidgetItem(QString("0x%1").arg(f.id, 3, 16, QChar('0')).toUpper()));
        m_rightTable->setItem(rightRow, 3, new QTableWidgetItem(QString::number(f.dlc)));
        QString d; for (int b = 0; b < f.dlc && b < 8; ++b) d += QString("%1").arg(f.data[b], 2, 16, QChar('0')).toUpper();
        m_rightTable->setItem(rightRow, 4, new QTableWidgetItem(d));

        QColor bg;
        switch (m_rightStatus[i]) {
        case Identical:  bg = QColor("#0a2e0a"); break;
        case Different:  bg = QColor("#2e0a0a"); break;
        case RightOnly:  bg = QColor("#0a1a2e"); break; // ciemnoniebieski
        default: break;
        }
        for (int c = 0; c < 5; ++c)
            if (auto *it = m_rightTable->item(rightRow, c)) it->setBackground(bg);

        rightRow++;
    }

    updateStats();
}

void LogComparatorWidget::updateStats() {
    int total = m_identical + m_different + m_leftOnly + m_rightOnly;
    double pct = total > 0 ? (100.0 * m_identical / total) : 0;

    m_statsLabel->setText(QString(
        "✅ %1% identycznych | ❌ %2 różnych | ⬅ %3 tylko lewy | ➡ %4 tylko prawy | Łącznie: %5")
        .arg(pct, 0, 'f', 1)
        .arg(m_different).arg(m_leftOnly).arg(m_rightOnly).arg(total));
    m_statsLabel->setStyleSheet(pct > 95 ? "color: #00ffaa; font-weight: bold;" :
                                pct > 70 ? "color: #ffaa00; font-weight: bold;" :
                                           "color: #ff4444; font-weight: bold;");
}

// ── Filtr ────────────────────────────────────────────────────

void LogComparatorWidget::applyFilter() {
    if (m_leftStatus.isEmpty()) return;

    QString fid = m_filterId->text().trimmed();
    double tFrom = m_filterTimeFrom->text().toDouble();
    double tTo   = m_filterTimeTo->text().toDouble();
    bool hasTimeFilt = (tFrom > 0 || tTo > 0);

    for (int i = 0; i < m_leftTable->rowCount(); ++i) {
        auto *idItem = m_leftTable->item(i, 2);
        auto *tsItem = m_leftTable->item(i, 1);
        if (!idItem || !tsItem) continue;

        bool show = true;
        if (!fid.isEmpty()) {
            uint32_t id = idItem->text().toUInt(nullptr, 16);
            uint32_t filterId = fid.toUInt(nullptr, 16);
            show = (id == filterId);
        }
        if (hasTimeFilt) {
            double ts = tsItem->text().toDouble();
            if (tFrom > 0 && ts < tFrom) show = false;
            if (tTo > 0 && ts > tTo) show = false;
        }
        m_leftTable->setRowHidden(i, !show);
    }

    for (int i = 0; i < m_rightTable->rowCount(); ++i) {
        auto *idItem = m_rightTable->item(i, 2);
        auto *tsItem = m_rightTable->item(i, 1);
        if (!idItem || !tsItem) continue;

        bool show = true;
        if (!fid.isEmpty()) {
            uint32_t id = idItem->text().toUInt(nullptr, 16);
            uint32_t filterId = fid.toUInt(nullptr, 16);
            show = (id == filterId);
        }
        if (hasTimeFilt) {
            double ts = tsItem->text().toDouble();
            if (tFrom > 0 && ts < tFrom) show = false;
            if (tTo > 0 && ts > tTo) show = false;
        }
        m_rightTable->setRowHidden(i, !show);
    }
}

// ── Scroll sync ──────────────────────────────────────────────

void LogComparatorWidget::onScrollSync(int value) {
    m_rightTable->verticalScrollBar()->blockSignals(true);
    m_rightTable->verticalScrollBar()->setValue(value);
    m_rightTable->verticalScrollBar()->blockSignals(false);
}

// ── Eksport raportu ──────────────────────────────────────────

void LogComparatorWidget::exportReport() {
    QString path = QFileDialog::getSaveFileName(this, "Eksportuj raport porównania", "diff_report.html", "HTML (*.html)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    out << "<!DOCTYPE html><html><head><meta charset='utf-8'><title>CAN Diff Report</title>"
        << "<style>body{background:#0a0e17;color:#c0c0c0;font-family:Consolas,monospace;padding:20px;}"
        << "h1{color:#00ffaa;} .stats{color:#ffaa00;} table{border-collapse:collapse;width:100%;margin:10px 0;}"
        << "th{background:#1a1a2e;color:#ff66cc;padding:6px;text-align:left;}"
        << "td{padding:4px;border-bottom:1px solid #2a2a3c;}"
        << ".green{background:#0a2e0a} .red{background:#2e0a0a} .yellow{background:#2e2a0a} .blue{background:#0a1a2e}"
        << "</style></head><body>";
    out << "<h1>CAN Diff Report</h1>";

    int total = m_identical + m_different + m_leftOnly + m_rightOnly;
    double pct = total > 0 ? (100.0 * m_identical / total) : 0;
    out << "<p class='stats'>Identical: " << m_identical << " (" << QString::number(pct, 'f', 1)
        << "%) | Different: " << m_different << " | Left only: " << m_leftOnly
        << " | Right only: " << m_rightOnly << "</p>";

    out << "<h2>Differences</h2><table><tr><th># Left</th><th># Right</th><th>Status</th><th>ID</th><th>Data</th></tr>";
    for (int i = 0; i < m_leftFrames.size(); ++i) {
        if (i >= m_leftStatus.size()) break;
        if (m_leftStatus[i] == Identical || m_leftStatus[i] == Skipped) continue;
        const auto &f = m_leftFrames[i];
        QString cls = (m_leftStatus[i] == Different) ? "red" :
                      (m_leftStatus[i] == LeftOnly)  ? "yellow" : "";
        QString st = (m_leftStatus[i] == Different) ? "DIFF" : "LEFT ONLY";
        out << "<tr class='" << cls << "'><td>" << (i+1) << "</td><td>—</td><td>" << st << "</td><td>0x"
            << QString::number(f.id, 16).toUpper() << "</td><td>";
        for (int b = 0; b < f.dlc && b < 8; ++b) out << QString("%1").arg(f.data[b], 2, 16, QChar('0')).toUpper();
        out << "</td></tr>\n";
    }
    for (int i = 0; i < m_rightFrames.size(); ++i) {
        if (i >= m_rightStatus.size()) break;
        if (m_rightStatus[i] != RightOnly) continue;
        const auto &f = m_rightFrames[i];
        out << "<tr class='blue'><td>—</td><td>" << (i+1) << "</td><td>RIGHT ONLY</td><td>0x"
            << QString::number(f.id, 16).toUpper() << "</td><td>";
        for (int b = 0; b < f.dlc && b < 8; ++b) out << QString("%1").arg(f.data[b], 2, 16, QChar('0')).toUpper();
        out << "</td></tr>\n";
    }
    out << "</table></body></html>";
    file.close();
    Logger::log(QString("Wyeksportowano raport diff: %1").arg(path));
}
