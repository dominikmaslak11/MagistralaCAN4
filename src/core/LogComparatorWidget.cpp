#include "LogComparatorWidget.h"
#include "Logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSplitter>
#include <QTabWidget>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QScrollBar>
#include <QMap>
#include <cmath>

static const QColor kColorIdentical  { 0x0a, 0x2e, 0x0a };
static const QColor kColorDifferent  { 0x2e, 0x0a, 0x0a };
static const QColor kColorLeftOnly   { 0x2e, 0x2a, 0x0a };
static const QColor kColorRightOnly  { 0x0a, 0x1a, 0x2e };
static const QColor kColorChangedByte{ 0x66, 0x00, 0x00 };

LogComparatorWidget::LogComparatorWidget(QWidget *parent) : QWidget(parent) {
    setupUi();
}

void LogComparatorWidget::setupUi() {
    auto *main = new QVBoxLayout(this);

    // ── Toolbar ──
    auto *toolbar = new QHBoxLayout;
    m_loadLeftBtn  = new QPushButton("Wczytaj lewy");
    m_loadRightBtn = new QPushButton("Wczytaj prawy");
    m_compareBtn   = new QPushButton("Porównaj");
    m_exportBtn    = new QPushButton("Eksportuj HTML");

    toolbar->addWidget(m_loadLeftBtn);
    toolbar->addWidget(m_loadRightBtn);

    toolbar->addWidget(new QLabel("Tryb:"));
    m_modeCombo = new QComboBox;
    m_modeCombo->addItem("Pozycyjny",    ModePositional);
    m_modeCombo->addItem("Według ID",    ModeById);
    toolbar->addWidget(m_modeCombo);

    m_highlightBytesChk = new QCheckBox("Podświetl zmienione bajty");
    m_highlightBytesChk->setChecked(true);
    toolbar->addWidget(m_highlightBytesChk);

    toolbar->addWidget(m_compareBtn);
    toolbar->addWidget(m_exportBtn);
    toolbar->addStretch();
    main->addLayout(toolbar);

    // ── Filtry ──
    auto *filterBar = new QHBoxLayout;
    filterBar->addWidget(new QLabel("Filtr ID:"));
    m_filterId = new QLineEdit;
    m_filterId->setPlaceholderText("np. 0x123");
    m_filterId->setMaximumWidth(90);
    filterBar->addWidget(m_filterId);
    filterBar->addWidget(new QLabel("Czas od (s):"));
    m_filterTimeFrom = new QLineEdit;
    m_filterTimeFrom->setMaximumWidth(60);
    filterBar->addWidget(m_filterTimeFrom);
    filterBar->addWidget(new QLabel("do (s):"));
    m_filterTimeTo = new QLineEdit;
    m_filterTimeTo->setMaximumWidth(60);
    filterBar->addWidget(m_filterTimeTo);
    m_filterApplyBtn = new QPushButton("Filtruj");
    filterBar->addWidget(m_filterApplyBtn);
    filterBar->addStretch();
    main->addLayout(filterBar);

    // ── Etykiety plików ──
    auto *labels = new QHBoxLayout;
    m_leftFileLabel  = new QLabel("Lewy: —");
    m_leftFileLabel->setStyleSheet("color: #ffaa00;");
    m_rightFileLabel = new QLabel("Prawy: —");
    m_rightFileLabel->setStyleSheet("color: #00aaff;");
    labels->addWidget(m_leftFileLabel);
    labels->addStretch();
    labels->addWidget(m_rightFileLabel);
    main->addLayout(labels);

    m_statsLabel = new QLabel("Wczytaj dwa pliki candump i kliknij Porównaj.");
    m_statsLabel->setStyleSheet("color: #ffaa00; font-weight: bold;");
    main->addWidget(m_statsLabel);

    // ── Zakładki: podgląd diff / podsumowanie per-ID ──
    auto *resultTabs = new QTabWidget;

    // Podgląd diff (side-by-side)
    auto *diffWidget  = new QWidget;
    auto *diffLayout  = new QVBoxLayout(diffWidget);
    diffLayout->setContentsMargins(0, 0, 0, 0);

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

    diffLayout->addWidget(splitter);
    resultTabs->addTab(diffWidget, "Diff ramek");

    // Podsumowanie per-ID
    m_idSummaryTable = new QTableWidget(0, 5);
    m_idSummaryTable->setHorizontalHeaderLabels({"ID", "Lewy (cnt)", "Prawy (cnt)", "Różne", "Status"});
    m_idSummaryTable->verticalHeader()->hide();
    m_idSummaryTable->horizontalHeader()->setStretchLastSection(true);
    m_idSummaryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_idSummaryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultTabs->addTab(m_idSummaryTable, "Podsumowanie per ID");

    main->addWidget(resultTabs, 1);

    // ── Scroll sync ──
    connect(m_leftTable->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &LogComparatorWidget::onScrollSync);
    connect(m_rightTable->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int v) {
        m_leftTable->verticalScrollBar()->blockSignals(true);
        m_leftTable->verticalScrollBar()->setValue(v);
        m_leftTable->verticalScrollBar()->blockSignals(false);
    });

    connect(m_loadLeftBtn,    &QPushButton::clicked, this, &LogComparatorWidget::loadLeft);
    connect(m_loadRightBtn,   &QPushButton::clicked, this, &LogComparatorWidget::loadRight);
    connect(m_compareBtn,     &QPushButton::clicked, this, &LogComparatorWidget::runCompare);
    connect(m_exportBtn,      &QPushButton::clicked, this, &LogComparatorWidget::exportReport);
    connect(m_filterApplyBtn, &QPushButton::clicked, this, &LogComparatorWidget::applyFilter);
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogComparatorWidget::onModeChanged);
}

// ── Ładowanie candump ────────────────────────────────────────────────────────

QVector<CanFrame> LogComparatorWidget::parseCandumpContent(const QString &content) {
    QVector<CanFrame> frames;
    for (const QString &rawLine : content.split('\n')) {
        QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith("//")) continue;

        int p1 = line.indexOf('(');
        int p2 = line.indexOf(')');
        if (p1 < 0 || p2 < 0) continue;

        double tsSec = line.mid(p1 + 1, p2 - p1 - 1).toDouble();

        int hashPos = line.indexOf('#', p2);
        if (hashPos < 0) continue;

        // Format: (ts) INTERFACE ID#data — ID is the token immediately before '#'
        int idEnd = hashPos;
        int idStart = idEnd - 1;
        while (idStart > p2 && !line[idStart].isSpace()) idStart--;
        idStart++; // skip leading space

        bool ok;
        uint32_t id = line.mid(idStart, idEnd - idStart).trimmed().toUInt(&ok, 16);
        if (!ok) continue;

        QString dataStr = line.mid(hashPos + 1).trimmed();
        CanFrame f;
        f.id        = id;
        f.timestamp = static_cast<uint64_t>(tsSec * 1'000'000.0);
        f.dlc       = 0;

        for (int i = 0; i + 1 < dataStr.length() && f.dlc < 64; i += 2) {
            f.data[f.dlc] = static_cast<uint8_t>(dataStr.mid(i, 2).toUInt(&ok, 16));
            if (ok) f.dlc++;
        }
        frames.append(f);
    }
    return frames;
}

QVector<CanFrame> LogComparatorWidget::loadCandump(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return parseCandumpContent(QString::fromUtf8(file.readAll()));
}

// ── Porównywanie ─────────────────────────────────────────────────────────────

void LogComparatorWidget::runCompare() {
    m_leftTable->setRowCount(0);
    m_rightTable->setRowCount(0);
    m_idSummaryTable->setRowCount(0);
    m_leftStatus.clear();
    m_rightStatus.clear();
    m_leftChangedBytes.clear();
    m_rightChangedBytes.clear();
    m_idSummary.clear();
    m_identical = m_different = m_leftOnly = m_rightOnly = 0;

    if (m_leftFrames.isEmpty() && m_rightFrames.isEmpty()) {
        m_statsLabel->setText("Brak danych — wczytaj pliki.");
        return;
    }

    CompareMode mode = static_cast<CompareMode>(m_modeCombo->currentData().toInt());
    if (mode == ModeById)
        runById();
    else
        runPositional();

    fillTable(m_leftTable,  m_leftFrames,  m_leftStatus,  m_leftChangedBytes);
    fillTable(m_rightTable, m_rightFrames, m_rightStatus, m_rightChangedBytes);
    fillIdSummaryTable();
    updateStats();
}

void LogComparatorWidget::runPositional() {
    int lSize = m_leftFrames.size();
    int rSize = m_rightFrames.size();

    m_leftStatus.resize(lSize,  Skipped);
    m_rightStatus.resize(rSize, Skipped);
    m_leftChangedBytes.resize(lSize);
    m_rightChangedBytes.resize(rSize);

    QMap<uint32_t, IdSummary> sumMap;

    int li = 0, ri = 0;
    while (li < lSize || ri < rSize) {
        const CanFrame *lf = (li < lSize) ? &m_leftFrames[li]  : nullptr;
        const CanFrame *rf = (ri < rSize) ? &m_rightFrames[ri] : nullptr;

        if (!lf && !rf) break;

        if (lf && rf) {
            double tDiff = std::abs(static_cast<double>(lf->timestamp) -
                                    static_cast<double>(rf->timestamp)) / 1'000'000.0;

            if (lf->id == rf->id && tDiff < 0.1) {
                // Porównaj bajt po bajcie
                int maxDlc = std::max(static_cast<int>(lf->dlc), static_cast<int>(rf->dlc));
                QVector<bool> lb(maxDlc, false), rb(maxDlc, false);
                bool anyDiff = (lf->dlc != rf->dlc);
                for (int b = 0; b < maxDlc; ++b) {
                    uint8_t lv = (b < lf->dlc) ? lf->data[b] : 0xFF;
                    uint8_t rv = (b < rf->dlc) ? rf->data[b] : 0xFF;
                    if (lv != rv) { lb[b] = rb[b] = true; anyDiff = true; }
                }
                if (anyDiff) {
                    m_leftStatus[li]  = Different;
                    m_rightStatus[ri] = Different;
                    m_leftChangedBytes[li]  = lb;
                    m_rightChangedBytes[ri] = rb;
                    m_different++;
                    sumMap[lf->id].diffCount++;
                } else {
                    m_leftStatus[li]  = Identical;
                    m_rightStatus[ri] = Identical;
                    m_identical++;
                }
                sumMap[lf->id].leftCount++;
                sumMap[lf->id].rightCount++;
                sumMap[lf->id].id = lf->id;
                li++; ri++;
            } else if (lf->timestamp <= rf->timestamp) {
                m_leftStatus[li] = LeftOnly;
                m_leftOnly++;
                sumMap[lf->id].leftCount++;
                sumMap[lf->id].id = lf->id;
                li++;
            } else {
                m_rightStatus[ri] = RightOnly;
                m_rightOnly++;
                sumMap[rf->id].rightCount++;
                sumMap[rf->id].id = rf->id;
                ri++;
            }
        } else if (lf) {
            m_leftStatus[li] = LeftOnly;
            m_leftOnly++;
            sumMap[lf->id].leftCount++;
            sumMap[lf->id].id = lf->id;
            li++;
        } else {
            m_rightStatus[ri] = RightOnly;
            m_rightOnly++;
            sumMap[rf->id].rightCount++;
            sumMap[rf->id].id = rf->id;
            ri++;
        }
    }

    for (auto &s : sumMap) m_idSummary.append(s);
}

void LogComparatorWidget::runById() {
    // Grupuj każdy plik wg ID → lista ramek per ID
    QMap<uint32_t, QVector<int>> leftByIdIdx, rightByIdIdx;
    for (int i = 0; i < m_leftFrames.size();  ++i) leftByIdIdx[m_leftFrames[i].id].append(i);
    for (int i = 0; i < m_rightFrames.size(); ++i) rightByIdIdx[m_rightFrames[i].id].append(i);

    m_leftStatus.resize(m_leftFrames.size(),   Skipped);
    m_rightStatus.resize(m_rightFrames.size(), Skipped);
    m_leftChangedBytes.resize(m_leftFrames.size());
    m_rightChangedBytes.resize(m_rightFrames.size());

    // Zbiór wszystkich ID
    QSet<uint32_t> allIds;
    for (auto id : leftByIdIdx.keys())  allIds.insert(id);
    for (auto id : rightByIdIdx.keys()) allIds.insert(id);

    for (uint32_t id : allIds) {
        auto &lIdxs = leftByIdIdx[id];
        auto &rIdxs = rightByIdIdx[id];

        IdSummary sum;
        sum.id         = id;
        sum.leftCount  = lIdxs.size();
        sum.rightCount = rIdxs.size();

        if (lIdxs.isEmpty()) {
            for (int ri : rIdxs) { m_rightStatus[ri] = RightOnly; m_rightOnly++; }
        } else if (rIdxs.isEmpty()) {
            for (int li : lIdxs) { m_leftStatus[li] = LeftOnly; m_leftOnly++; }
        } else {
            // Paruj ramki tego samego ID pozycyjnie
            int pairs = std::min(lIdxs.size(), rIdxs.size());
            for (int p = 0; p < pairs; ++p) {
                int li = lIdxs[p], ri = rIdxs[p];
                const auto &lf = m_leftFrames[li];
                const auto &rf = m_rightFrames[ri];

                int maxDlc = std::max(static_cast<int>(lf.dlc), static_cast<int>(rf.dlc));
                QVector<bool> lb(maxDlc, false), rb(maxDlc, false);
                bool anyDiff = (lf.dlc != rf.dlc);
                for (int b = 0; b < maxDlc; ++b) {
                    uint8_t lv = (b < lf.dlc) ? lf.data[b] : 0xFF;
                    uint8_t rv = (b < rf.dlc) ? rf.data[b] : 0xFF;
                    if (lv != rv) { lb[b] = rb[b] = true; anyDiff = true; }
                }
                if (anyDiff) {
                    m_leftStatus[li]  = Different;
                    m_rightStatus[ri] = Different;
                    m_leftChangedBytes[li]  = lb;
                    m_rightChangedBytes[ri] = rb;
                    m_different++;
                    sum.diffCount++;
                } else {
                    m_leftStatus[li]  = Identical;
                    m_rightStatus[ri] = Identical;
                    m_identical++;
                }
            }
            // Nadmiarowe ramki
            for (int p = pairs; p < lIdxs.size(); ++p) {
                m_leftStatus[lIdxs[p]] = LeftOnly; m_leftOnly++;
            }
            for (int p = pairs; p < rIdxs.size(); ++p) {
                m_rightStatus[rIdxs[p]] = RightOnly; m_rightOnly++;
            }
        }
        m_idSummary.append(sum);
    }

    // Posortuj podsumowanie wg ID
    std::sort(m_idSummary.begin(), m_idSummary.end(),
              [](const IdSummary &a, const IdSummary &b){ return a.id < b.id; });
}

// ── Wypełnianie tabeli ───────────────────────────────────────────────────────

void LogComparatorWidget::fillTable(QTableWidget *tbl,
                                    const QVector<CanFrame> &frames,
                                    const QVector<DiffStatus> &status,
                                    const QVector<QVector<bool>> &changedBytes) {
    tbl->setRowCount(0);
    bool highlight = m_highlightBytesChk->isChecked();

    for (int i = 0; i < frames.size(); ++i) {
        if (i >= status.size() || status[i] == Skipped) continue;

        const auto &f = frames[i];
        int row = tbl->rowCount();
        tbl->insertRow(row);

        tbl->setItem(row, 0, new QTableWidgetItem(QString::number(i + 1)));
        tbl->setItem(row, 1, new QTableWidgetItem(
            QString::number(f.timestamp / 1'000'000.0, 'f', 6)));
        tbl->setItem(row, 2, new QTableWidgetItem(
            QString("0x%1").arg(f.id, 3, 16, QChar('0')).toUpper()));
        tbl->setItem(row, 3, new QTableWidgetItem(QString::number(f.dlc)));

        // Dane z opcjonalnym podświetleniem zmienionych bajtów
        QString dataStr;
        if (highlight && status[i] == Different && i < changedBytes.size()) {
            const auto &cb = changedBytes[i];
            for (int b = 0; b < f.dlc && b < 64; ++b) {
                if (b < cb.size() && cb[b])
                    dataStr += QString("[%1]").arg(f.data[b], 2, 16, QChar('0')).toUpper();
                else
                    dataStr += QString("%1").arg(f.data[b], 2, 16, QChar('0')).toUpper();
            }
        } else {
            for (int b = 0; b < f.dlc && b < 64; ++b)
                dataStr += QString("%1").arg(f.data[b], 2, 16, QChar('0')).toUpper();
        }
        tbl->setItem(row, 4, new QTableWidgetItem(dataStr));

        QColor bg;
        switch (status[i]) {
        case Identical:  bg = kColorIdentical;  break;
        case Different:  bg = kColorDifferent;  break;
        case LeftOnly:   bg = kColorLeftOnly;   break;
        case RightOnly:  bg = kColorRightOnly;  break;
        default: break;
        }
        for (int c = 0; c < 5; ++c)
            if (auto *it = tbl->item(row, c)) it->setBackground(bg);
    }
}

void LogComparatorWidget::fillIdSummaryTable() {
    m_idSummaryTable->setRowCount(0);
    for (const auto &s : m_idSummary) {
        int row = m_idSummaryTable->rowCount();
        m_idSummaryTable->insertRow(row);
        m_idSummaryTable->setItem(row, 0, new QTableWidgetItem(
            QString("0x%1").arg(s.id, 3, 16, QChar('0')).toUpper()));
        m_idSummaryTable->setItem(row, 1, new QTableWidgetItem(QString::number(s.leftCount)));
        m_idSummaryTable->setItem(row, 2, new QTableWidgetItem(QString::number(s.rightCount)));
        m_idSummaryTable->setItem(row, 3, new QTableWidgetItem(QString::number(s.diffCount)));

        QString statusStr;
        QColor  statusCol;
        if (s.leftCount == 0)            { statusStr = "Tylko prawy"; statusCol = kColorRightOnly; }
        else if (s.rightCount == 0)      { statusStr = "Tylko lewy";  statusCol = kColorLeftOnly; }
        else if (s.diffCount == 0)       { statusStr = "Identyczne";  statusCol = kColorIdentical; }
        else                             { statusStr = "Różne dane";  statusCol = kColorDifferent; }

        auto *stItem = new QTableWidgetItem(statusStr);
        m_idSummaryTable->setItem(row, 4, stItem);
        for (int c = 0; c < 5; ++c)
            if (auto *it = m_idSummaryTable->item(row, c)) it->setBackground(statusCol);
    }
}

// ── Statystyki ───────────────────────────────────────────────────────────────

void LogComparatorWidget::updateStats() {
    int total = m_identical + m_different + m_leftOnly + m_rightOnly;
    double pct = total > 0 ? (100.0 * m_identical / total) : 0;

    m_statsLabel->setText(QString(
        "Identyczne: %1% | Różne: %2 | Tylko lewy: %3 | Tylko prawy: %4 | ID unikatowych: %5 | Razem: %6")
        .arg(pct, 0, 'f', 1).arg(m_different).arg(m_leftOnly)
        .arg(m_rightOnly).arg(m_idSummary.size()).arg(total));

    m_statsLabel->setStyleSheet(
        pct > 95 ? "color: #00ffaa; font-weight: bold;" :
        pct > 70 ? "color: #ffaa00; font-weight: bold;" :
                   "color: #ff4444; font-weight: bold;");
}

// ── Ładowanie plików ─────────────────────────────────────────────────────────

void LogComparatorWidget::loadLeft() {
    QString p = QFileDialog::getOpenFileName(this, "Wczytaj lewy plik candump",
                    "", "Logi (*.log *.txt);;Wszystkie (*)");
    if (p.isEmpty()) return;
    m_leftFrames = loadCandump(p);
    m_leftFileLabel->setText(QString("Lewy: %1 (%2 ramek)")
        .arg(p.section('/', -1)).arg(m_leftFrames.size()));
}

void LogComparatorWidget::loadRight() {
    QString p = QFileDialog::getOpenFileName(this, "Wczytaj prawy plik candump",
                    "", "Logi (*.log *.txt);;Wszystkie (*)");
    if (p.isEmpty()) return;
    m_rightFrames = loadCandump(p);
    m_rightFileLabel->setText(QString("Prawy: %1 (%2 ramek)")
        .arg(p.section('/', -1)).arg(m_rightFrames.size()));
}

// ── Filtry ───────────────────────────────────────────────────────────────────

void LogComparatorWidget::applyFilter() {
    if (m_leftStatus.isEmpty()) return;

    QString fid  = m_filterId->text().trimmed();
    double tFrom = m_filterTimeFrom->text().toDouble();
    double tTo   = m_filterTimeTo->text().toDouble();
    bool hasTime = (tFrom > 0 || tTo > 0);

    auto applyToTable = [&](QTableWidget *tbl) {
        for (int i = 0; i < tbl->rowCount(); ++i) {
            auto *idItem = tbl->item(i, 2);
            auto *tsItem = tbl->item(i, 1);
            if (!idItem || !tsItem) continue;
            bool show = true;
            if (!fid.isEmpty())
                show = (idItem->text().toUInt(nullptr, 16) == fid.toUInt(nullptr, 16));
            if (hasTime) {
                double ts = tsItem->text().toDouble();
                if (tFrom > 0 && ts < tFrom) show = false;
                if (tTo   > 0 && ts > tTo)   show = false;
            }
            tbl->setRowHidden(i, !show);
        }
    };
    applyToTable(m_leftTable);
    applyToTable(m_rightTable);
}

// ── Scroll sync ──────────────────────────────────────────────────────────────

void LogComparatorWidget::onScrollSync(int value) {
    m_rightTable->verticalScrollBar()->blockSignals(true);
    m_rightTable->verticalScrollBar()->setValue(value);
    m_rightTable->verticalScrollBar()->blockSignals(false);
}

void LogComparatorWidget::onModeChanged(int) {
    // Nic nie rób automatycznie — użytkownik musi kliknąć Porównaj
}

// ── Eksport HTML ─────────────────────────────────────────────────────────────

void LogComparatorWidget::exportReport() {
    QString path = QFileDialog::getSaveFileName(this, "Eksportuj raport",
                    "can_diff_report.html", "HTML (*.html)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    out << "<!DOCTYPE html><html><head><meta charset='utf-8'><title>CAN Diff Report</title>"
        << "<style>"
        << "body{background:#0a0e17;color:#c0c0c0;font-family:Consolas,monospace;padding:20px;}"
        << "h1,h2{color:#00ffaa;} .stats{color:#ffaa00;margin-bottom:16px;}"
        << "table{border-collapse:collapse;width:100%;margin-bottom:24px;}"
        << "th{background:#1a1a2e;color:#ff66cc;padding:6px;text-align:left;}"
        << "td{padding:4px;border-bottom:1px solid #2a2a3c;}"
        << ".green{background:#0a2e0a} .red{background:#2e0a0a} .yellow{background:#2e2a0a} .blue{background:#0a1a2e}"
        << "span.diff{background:#660000;color:#fff;padding:0 2px;border-radius:2px;}"
        << "</style></head><body>";

    out << "<h1>CAN Diff Report</h1>";
    int total = m_identical + m_different + m_leftOnly + m_rightOnly;
    double pct = total > 0 ? (100.0 * m_identical / total) : 0;
    out << "<p class='stats'>Identyczne: " << m_identical
        << " (" << QString::number(pct, 'f', 1) << "%) | Różne: " << m_different
        << " | Tylko lewy: " << m_leftOnly << " | Tylko prawy: " << m_rightOnly << "</p>";

    // Podsumowanie per-ID
    out << "<h2>Podsumowanie per ID</h2><table>"
        << "<tr><th>ID</th><th>Lewy</th><th>Prawy</th><th>Różne</th><th>Status</th></tr>\n";
    for (const auto &s : m_idSummary) {
        QString cls = (s.leftCount == 0) ? "blue" : (s.rightCount == 0) ? "yellow" :
                      (s.diffCount  > 0) ? "red"  : "green";
        QString st  = (s.leftCount == 0) ? "Tylko prawy" : (s.rightCount == 0) ? "Tylko lewy" :
                      (s.diffCount  > 0) ? "Różne" : "Identyczne";
        out << "<tr class='" << cls << "'><td>0x"
            << QString::number(s.id, 16).toUpper() << "</td><td>" << s.leftCount
            << "</td><td>" << s.rightCount << "</td><td>" << s.diffCount
            << "</td><td>" << st << "</td></tr>\n";
    }
    out << "</table>";

    // Różnice ramkowe
    out << "<h2>Różnice ramkowe</h2><table>"
        << "<tr><th>#L</th><th>#R</th><th>Status</th><th>ID</th><th>Dane (lewy)</th><th>Dane (prawy)</th></tr>\n";

    int maxRows = std::max(m_leftFrames.size(), m_rightFrames.size());
    int li = 0, ri = 0;

    for (int i = 0; i < maxRows; ++i) {
        if (li >= m_leftStatus.size() && ri >= m_rightStatus.size()) break;

        DiffStatus ls = (li < m_leftStatus.size())  ? m_leftStatus[li]  : Skipped;
        DiffStatus rs = (ri < m_rightStatus.size()) ? m_rightStatus[ri] : Skipped;

        if (ls == Identical && rs == Identical) { li++; ri++; continue; }
        if (ls == Skipped && rs == Skipped) break;

        QString cls, st;
        QString lData, rData;

        if (ls == Different && rs == Different) {
            cls = "red"; st = "DIFF";
            const auto &lf = m_leftFrames[li];
            const auto &rf = m_rightFrames[ri];
            // Podświetl zmienione bajty
            auto mkData = [](const CanFrame &f, const QVector<bool> &cb) {
                QString d;
                for (int b = 0; b < f.dlc && b < 64; ++b) {
                    QString hex = QString("%1").arg(f.data[b], 2, 16, QChar('0')).toUpper();
                    d += (b < cb.size() && cb[b])
                        ? "<span class='diff'>" + hex + "</span>"
                        : hex;
                }
                return d;
            };
            QVector<bool> emptyL(lf.dlc, false), emptyR(rf.dlc, false);
            const auto &lcb = (li < m_leftChangedBytes.size())  ? m_leftChangedBytes[li]  : emptyL;
            const auto &rcb = (ri < m_rightChangedBytes.size()) ? m_rightChangedBytes[ri] : emptyR;
            lData = mkData(lf, lcb);
            rData = mkData(rf, rcb);
            out << "<tr class='" << cls << "'><td>" << (li+1) << "</td><td>" << (ri+1)
                << "</td><td>" << st << "</td><td>0x"
                << QString::number(lf.id, 16).toUpper()
                << "</td><td>" << lData << "</td><td>" << rData << "</td></tr>\n";
            li++; ri++;
        } else if (ls == LeftOnly) {
            const auto &lf = m_leftFrames[li];
            for (int b = 0; b < lf.dlc && b < 64; ++b)
                lData += QString("%1").arg(lf.data[b], 2, 16, QChar('0')).toUpper();
            out << "<tr class='yellow'><td>" << (li+1)
                << "</td><td>—</td><td>TYLKO LEWY</td><td>0x"
                << QString::number(lf.id, 16).toUpper()
                << "</td><td>" << lData << "</td><td>—</td></tr>\n";
            li++;
        } else if (rs == RightOnly) {
            const auto &rf = m_rightFrames[ri];
            for (int b = 0; b < rf.dlc && b < 64; ++b)
                rData += QString("%1").arg(rf.data[b], 2, 16, QChar('0')).toUpper();
            out << "<tr class='blue'><td>—</td><td>" << (ri+1)
                << "</td><td>TYLKO PRAWY</td><td>0x"
                << QString::number(rf.id, 16).toUpper()
                << "</td><td>—</td><td>" << rData << "</td></tr>\n";
            ri++;
        } else {
            if (li < m_leftStatus.size()) li++;
            if (ri < m_rightStatus.size()) ri++;
        }
    }
    out << "</table></body></html>";
    file.close();
    Logger::log(QString("Wyeksportowano raport diff: %1").arg(path));
    QMessageBox::information(this, "Eksport", QString("Raport zapisany:\n%1").arg(path));
}
