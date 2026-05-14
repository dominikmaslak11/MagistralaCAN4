#include "CanForensicsWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QHeaderView>
#include <QSplitter>
#include <QFrame>
#include <QClipboard>
#include <QApplication>
#include <algorithm>

static const char *kTabSS =
    "QTabWidget::pane { border: 1px solid #2a2a3c; background: #0a0e17; }"
    "QTabBar::tab { background: #1a1a2e; color: #c0c0c0; padding: 5px 14px; "
    "  border: 1px solid #2a2a3c; border-bottom: none; font-family: Consolas; }"
    "QTabBar::tab:selected { background: #0a0e17; color: #00ffaa; border-bottom: none; }"
    "QTabBar::tab:hover:!selected { background: #2a2a3e; }";

static const char *kTableSS =
    "QTableWidget { background: #0a0e17; color: #c0c0c0; gridline-color: #2a2a3c; "
    "  font-family: Consolas; font-size: 11px; selection-background-color: #1a2a3a; }"
    "QHeaderView::section { background: #1a1a2e; color: #ff66cc; border: 1px solid #2a2a3c; "
    "  padding: 4px; font-weight: bold; font-family: Consolas; }";

static const char *kBtnSS =
    "QPushButton { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560; "
    "  border-radius: 4px; padding: 4px 10px; font-family: Consolas; }"
    "QPushButton:hover { background: #2a2a3e; }";

static const char *kEditSS =
    "QLineEdit { background: #0a0e17; color: #00ffaa; border: 1px solid #e94560; "
    "  border-radius: 3px; padding: 3px 6px; font-family: Consolas; }";

// ── Helpers ──────────────────────────────────────────────────────────────────

static int popcount8(uint8_t v) {
    v = static_cast<uint8_t>(v - ((v >> 1) & 0x55u));
    v = static_cast<uint8_t>((v & 0x33u) + ((v >> 2) & 0x33u));
    return (v + (v >> 4)) & 0x0F;
}

static QColor byteCellBg(uint8_t varying) {
    int vc = popcount8(varying);
    if (vc == 0)  return QColor(0x0C, 0x28, 0x0C);  // stable       — dark green
    if (vc <= 2)  return QColor(0x1A, 0x22, 0x06);  // mostly stable — olive
    if (vc <= 5)  return QColor(0x28, 0x18, 0x04);  // half varying  — orange
    return            QColor(0x28, 0x06, 0x06);      // mostly varying — dark red
}

static std::vector<uint8_t> parseHexBytes(const QString &s) {
    const QString clean = QString(s).replace(" ", "").replace(",", "");
    std::vector<uint8_t> result;
    for (int i = 0; i + 1 < clean.length(); i += 2) {
        bool ok;
        result.push_back(static_cast<uint8_t>(clean.mid(i, 2).toUInt(&ok, 16)));
        if (!ok) { result.clear(); break; }
    }
    return result;
}

// Highlight matching bytes in a hex string: "AA BB [DE AD] EE FF"
static QString highlightMatch(const CanFrame &f, int offset, int len) {
    QStringList parts;
    for (int i = 0; i < f.dlc && i < 8; ++i) {
        QString hex = QString("%1").arg(f.data[i], 2, 16, QChar('0')).toUpper();
        if (i == offset)           hex = "[" + hex;
        if (i == offset + len - 1) hex = hex + "]";
        parts << hex;
    }
    return parts.join(" ");
}

// ── Constructor ───────────────────────────────────────────────────────────────

CanForensicsWidget::CanForensicsWidget(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *tabs = new QTabWidget;
    tabs->setStyleSheet(kTabSS);
    tabs->addTab(makeBitProfilerTab(),    "Profil bitów");
    tabs->addTab(makeIntervalTab(),       "Interwały");
    tabs->addTab(makePayloadSearchTab(),  "Szukaj wzorców");
    layout->addWidget(tabs);

    // Auto-refresh bit + interval tables every 2 s
    auto *refreshTimer = new QTimer(this);
    refreshTimer->setInterval(2000);
    connect(refreshTimer, &QTimer::timeout, this, &CanForensicsWidget::refreshBitProfiler);
    connect(refreshTimer, &QTimer::timeout, this, &CanForensicsWidget::refreshIntervalTable);
    refreshTimer->start();

    setStyleSheet("background-color: #0a0e17;");
}

// ── Tab builders ──────────────────────────────────────────────────────────────

QWidget *CanForensicsWidget::makeBitProfilerTab() {
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(4, 4, 4, 4);

    auto *toolbar = new QHBoxLayout;
    auto *title = new QLabel("Profil stabilności bitów CAN (widok per bajt per ID)");
    title->setStyleSheet("color: #ff66cc; font-weight: bold; font-family: Consolas;");
    toolbar->addWidget(title);
    toolbar->addStretch();

    auto *copyBtn = new QPushButton("Kopiuj raport");
    copyBtn->setStyleSheet(kBtnSS);
    connect(copyBtn, &QPushButton::clicked, this, &CanForensicsWidget::copyBitReport);
    toolbar->addWidget(copyBtn);

    auto *resetBtn = new QPushButton("Reset");
    resetBtn->setStyleSheet(kBtnSS);
    connect(resetBtn, &QPushButton::clicked, this, &CanForensicsWidget::resetBitProfiler);
    toolbar->addWidget(resetBtn);

    m_bitStats = new QLabel("Brak danych — oczekiwanie na ramki...");
    m_bitStats->setStyleSheet("color: #ffaa00; font-family: Consolas; font-size: 11px;");
    toolbar->addWidget(m_bitStats);
    lay->addLayout(toolbar);

    // Legend
    auto *legend = new QHBoxLayout;
    auto mkLeg = [](QColor bg, const QString &text) {
        auto *lbl = new QLabel(text);
        lbl->setStyleSheet(QString("background:%1; color:#c0c0c0; padding:2px 8px; "
                                   "border-radius:3px; font-family:Consolas; font-size:10px;")
                               .arg(bg.name()));
        return lbl;
    };
    legend->addWidget(mkLeg(QColor(0x0C,0x28,0x0C), "Stabilny (0 bitów zmiennych)"));
    legend->addWidget(mkLeg(QColor(0x1A,0x22,0x06), "1–2 bity zmienne"));
    legend->addWidget(mkLeg(QColor(0x28,0x18,0x04), "3–5 bitów zmiennych"));
    legend->addWidget(mkLeg(QColor(0x28,0x06,0x06), "6–8 bitów zmiennych"));
    legend->addStretch();
    lay->addLayout(legend);

    m_bitTable = new QTableWidget(0, 11);
    m_bitTable->setStyleSheet(kTableSS);
    m_bitTable->setHorizontalHeaderLabels(
        {"ID", "Ramki", "DLC", "B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7"});
    m_bitTable->horizontalHeader()->setStretchLastSection(false);
    m_bitTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_bitTable->verticalHeader()->hide();
    m_bitTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_bitTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_bitTable->setSortingEnabled(true);
    lay->addWidget(m_bitTable, 1);
    return w;
}

QWidget *CanForensicsWidget::makeIntervalTab() {
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(4, 4, 4, 4);

    auto *toolbar = new QHBoxLayout;
    auto *title = new QLabel("Analiza interwałów czasowych CAN (per ID)");
    title->setStyleSheet("color: #ff66cc; font-weight: bold; font-family: Consolas;");
    toolbar->addWidget(title);
    toolbar->addStretch();

    auto *resetBtn = new QPushButton("Reset");
    resetBtn->setStyleSheet(kBtnSS);
    connect(resetBtn, &QPushButton::clicked, this, &CanForensicsWidget::resetIntervalAnalyzer);
    toolbar->addWidget(resetBtn);

    m_intervalStats = new QLabel("Brak danych");
    m_intervalStats->setStyleSheet("color: #ffaa00; font-family: Consolas; font-size: 11px;");
    toolbar->addWidget(m_intervalStats);
    lay->addLayout(toolbar);

    m_intervalTable = new QTableWidget(0, 8);
    m_intervalTable->setStyleSheet(kTableSS);
    m_intervalTable->setHorizontalHeaderLabels(
        {"ID", "Ramki", "Śr.(ms)", "Odch.std(ms)", "Min(ms)", "Max(ms)", "Przerwy", "Typ"});
    m_intervalTable->horizontalHeader()->setStretchLastSection(false);
    m_intervalTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_intervalTable->verticalHeader()->hide();
    m_intervalTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_intervalTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_intervalTable->setSortingEnabled(true);
    lay->addWidget(m_intervalTable, 1);
    return w;
}

QWidget *CanForensicsWidget::makePayloadSearchTab() {
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(4, 4, 4, 4);

    // Query form
    auto *form = new QHBoxLayout;
    form->addWidget(new QLabel("Wzorzec hex:"));
    m_patternEdit = new QLineEdit;
    m_patternEdit->setPlaceholderText("np. DE AD BE EF");
    m_patternEdit->setStyleSheet(kEditSS);
    m_patternEdit->setMinimumWidth(200);
    form->addWidget(m_patternEdit);

    form->addWidget(new QLabel("Maska:"));
    m_maskEdit = new QLineEdit;
    m_maskEdit->setPlaceholderText("opcjonalna (FF = exact)");
    m_maskEdit->setStyleSheet(kEditSS);
    m_maskEdit->setMinimumWidth(160);
    form->addWidget(m_maskEdit);

    form->addWidget(new QLabel("CAN ID:"));
    m_searchIdEdit = new QLineEdit;
    m_searchIdEdit->setPlaceholderText("opcjonalny hex");
    m_searchIdEdit->setStyleSheet(kEditSS);
    m_searchIdEdit->setFixedWidth(80);
    form->addWidget(m_searchIdEdit);

    form->addWidget(new QLabel("Maks.:"));
    m_maxResEdit = new QLineEdit("1000");
    m_maxResEdit->setStyleSheet(kEditSS);
    m_maxResEdit->setFixedWidth(60);
    form->addWidget(m_maxResEdit);

    auto *searchBtn = new QPushButton("Szukaj");
    searchBtn->setStyleSheet(
        "QPushButton { background: #1a1a2e; color: #00ffaa; border: 1px solid #00ffaa; "
        "  border-radius: 4px; padding: 4px 14px; font-family: Consolas; font-weight: bold; }"
        "QPushButton:hover { background: #00ffaa; color: #0a0e17; }");
    connect(searchBtn, &QPushButton::clicked, this, &CanForensicsWidget::runPayloadSearch);
    form->addWidget(searchBtn);

    auto *clearBtn = new QPushButton("Wyczyść bufor");
    clearBtn->setStyleSheet(kBtnSS);
    connect(clearBtn, &QPushButton::clicked, this, &CanForensicsWidget::clearFrameStore);
    form->addWidget(clearBtn);
    form->addStretch();
    lay->addLayout(form);

    m_searchStats = new QLabel("Bufor: 0 ramek | Gotowy do wyszukiwania");
    m_searchStats->setStyleSheet("color: #ffaa00; font-family: Consolas; font-size: 11px;");
    lay->addWidget(m_searchStats);

    m_searchTable = new QTableWidget(0, 5);
    m_searchTable->setStyleSheet(kTableSS);
    m_searchTable->setHorizontalHeaderLabels({"#ramka", "Czas(s)", "CAN ID", "Offset", "Dane [dopasowanie]"});
    m_searchTable->horizontalHeader()->setStretchLastSection(true);
    m_searchTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_searchTable->verticalHeader()->hide();
    m_searchTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_searchTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    lay->addWidget(m_searchTable, 1);

    for (auto *lbl : w->findChildren<QLabel*>())
        if (lbl->styleSheet().isEmpty())
            lbl->setStyleSheet("color: #c0c0c0; font-family: Consolas;");

    return w;
}

// ── processFrame ──────────────────────────────────────────────────────────────

void CanForensicsWidget::processFrame(const CanFrame &frame) {
    m_bitAnalyzer.add(frame);
    m_intervalAnalyzer.add(frame);

    if (m_frameStore.size() < kMaxStore)
        m_frameStore.append(frame);
    else {
        // Evict oldest quarter to make room
        m_frameStore.remove(0, kMaxStore / 4);
        m_frameStore.append(frame);
    }
}

// ── Bit Profiler ──────────────────────────────────────────────────────────────

void CanForensicsWidget::refreshBitProfiler() {
    const auto profiles = m_bitAnalyzer.profiles();
    if (profiles.isEmpty()) return;

    m_bitTable->setSortingEnabled(false);
    m_bitTable->setRowCount(profiles.size());

    for (int row = 0; row < profiles.size(); ++row) {
        const auto &p = profiles[row];
        const auto var = p.varying();

        auto setCell = [&](int col, const QString &text, QColor bg = QColor("#1a1a2e")) {
            auto *it = m_bitTable->item(row, col);
            if (!it) { it = new QTableWidgetItem; m_bitTable->setItem(row, col, it); }
            it->setText(text);
            it->setBackground(bg);
            it->setForeground(QColor("#d0d0d0"));
            it->setTextAlignment(Qt::AlignCenter);
        };

        setCell(0, QString("0x%1").arg(p.id, 3, 16, QChar('0')).toUpper());
        setCell(1, QString::number(p.frameCount));
        setCell(2, QString::number(p.maxDlc));

        for (int b = 0; b < 8; ++b) {
            if (b >= p.maxDlc) {
                setCell(3 + b, "", QColor("#0a0e17"));
                continue;
            }
            const uint8_t v = var[b];
            const QColor bg = byteCellBg(v);
            const QString text = (v == 0)
                ? QString("%1").arg(p.alwaysOne[b], 2, 16, QChar('0')).toUpper()
                : (v == 0xFF ? "--" : "?~");
            auto *it = m_bitTable->item(row, 3 + b);
            if (!it) { it = new QTableWidgetItem; m_bitTable->setItem(row, 3 + b, it); }
            it->setText(text);
            it->setBackground(bg);
            it->setForeground(v == 0 ? QColor("#00ffaa") : QColor("#ffaa00"));
            it->setTextAlignment(Qt::AlignCenter);

            // Tooltip: binary breakdown
            QString tip = QString("B%1 (0x%2): ").arg(b).arg(b);
            for (int bit = 7; bit >= 0; --bit) {
                uint8_t mask = static_cast<uint8_t>(1 << bit);
                if (v & mask)            tip += '?';
                else if (p.alwaysOne[b] & mask) tip += '1';
                else                     tip += '0';
            }
            it->setToolTip(tip);
        }
    }

    m_bitTable->setSortingEnabled(true);
    m_bitStats->setText(
        QString("IDs: %1 | Ramki skumulowane: %2")
            .arg(profiles.size()).arg(profiles[0].frameCount > 0 ? profiles.last().frameCount : 0));
}

void CanForensicsWidget::resetBitProfiler() {
    m_bitAnalyzer.reset();
    m_bitTable->setRowCount(0);
    m_bitStats->setText("Zresetowano — oczekiwanie na ramki...");
}

void CanForensicsWidget::copyBitReport() {
    QApplication::clipboard()->setText(m_bitAnalyzer.report());
}

// ── Interval Analyzer ─────────────────────────────────────────────────────────

void CanForensicsWidget::refreshIntervalTable() {
    const auto statsList = m_intervalAnalyzer.stats();
    if (statsList.isEmpty()) return;

    m_intervalTable->setSortingEnabled(false);
    m_intervalTable->setRowCount(statsList.size());

    int periodicCount = 0;

    for (int row = 0; row < statsList.size(); ++row) {
        const auto &s = statsList[row];
        const bool periodic = s.isPeriodic();
        if (periodic) ++periodicCount;

        const QColor rowBg = periodic ? QColor("#0a2a0a") : QColor("#0a0e17");

        auto setCell = [&](int col, const QString &text) {
            auto *it = m_intervalTable->item(row, col);
            if (!it) { it = new QTableWidgetItem; m_intervalTable->setItem(row, col, it); }
            it->setText(text);
            it->setBackground(rowBg);
            it->setForeground(QColor("#c0c0c0"));
            it->setTextAlignment(Qt::AlignCenter);
        };

        setCell(0, QString("0x%1").arg(s.id, 3, 16, QChar('0')).toUpper());
        setCell(1, QString::number(s.frameCount));
        setCell(2, QString::number(s.meanUs   / 1000.0, 'f', 3));
        setCell(3, QString::number(s.stdDevUs / 1000.0, 'f', 3));
        setCell(4, s.minUs == std::numeric_limits<uint64_t>::max()
                    ? "—" : QString::number(s.minUs / 1000.0, 'f', 3));
        setCell(5, QString::number(s.maxUs / 1000.0, 'f', 3));
        setCell(6, QString::number(s.gapCount));
        setCell(7, periodic ? "Cykliczny" : "Sporadyczny");
        auto *typeIt = m_intervalTable->item(row, 7);
        if (typeIt) typeIt->setForeground(periodic ? QColor("#00ffaa") : QColor("#ffaa00"));
    }

    m_intervalTable->setSortingEnabled(true);
    m_intervalStats->setText(
        QString("IDs: %1 | Cyklicznych: %2 | Sporadycznych: %3")
            .arg(statsList.size()).arg(periodicCount).arg(statsList.size() - periodicCount));
}

void CanForensicsWidget::resetIntervalAnalyzer() {
    m_intervalAnalyzer.reset();
    m_intervalTable->setRowCount(0);
    m_intervalStats->setText("Zresetowano");
}

// ── Payload Search ────────────────────────────────────────────────────────────

void CanForensicsWidget::runPayloadSearch() {
    m_searchTable->setRowCount(0);

    const auto pattern = parseHexBytes(m_patternEdit->text());
    if (pattern.empty()) {
        m_searchStats->setText("Błąd: wprowadź prawidłowy wzorzec hex (np. DE AD BE EF)");
        return;
    }

    CanPayloadSearch::Query query;
    query.pattern = pattern;

    const auto mask = parseHexBytes(m_maskEdit->text());
    if (!mask.empty()) query.mask = mask;

    const QString idStr = m_searchIdEdit->text().trimmed();
    if (!idStr.isEmpty()) {
        bool ok;
        query.canId      = idStr.toUInt(&ok, 16);
        query.filterById = ok;
    }

    bool ok;
    int maxR = m_maxResEdit->text().toInt(&ok);
    query.maxResults = (ok && maxR > 0) ? maxR : 0;

    // Convert QVector to std::vector for the search API
    std::vector<CanFrame> frames(m_frameStore.begin(), m_frameStore.end());
    const auto matches = CanPayloadSearch::search(frames, query);

    m_searchTable->setSortingEnabled(false);
    m_searchTable->setRowCount(static_cast<int>(matches.size()));

    for (int row = 0; row < static_cast<int>(matches.size()); ++row) {
        const auto &m = matches[row];
        const auto &f = frames[m.frameIndex];

        auto mkItem = [](const QString &text) {
            auto *it = new QTableWidgetItem(text);
            it->setForeground(QColor("#c0c0c0"));
            it->setTextAlignment(Qt::AlignCenter);
            return it;
        };

        m_searchTable->setItem(row, 0, mkItem(QString::number(m.frameIndex)));
        m_searchTable->setItem(row, 1, mkItem(QString::number(f.timestamp / 1'000'000.0, 'f', 6)));
        m_searchTable->setItem(row, 2, mkItem(QString("0x%1").arg(m.canId, 3, 16, QChar('0')).toUpper()));
        m_searchTable->setItem(row, 3, mkItem(QString::number(m.byteOffset)));
        m_searchTable->setItem(row, 4, mkItem(highlightMatch(f, m.byteOffset, static_cast<int>(pattern.size()))));
    }

    m_searchTable->setSortingEnabled(true);
    m_searchStats->setText(
        QString("Bufor: %1 ramek | Znaleziono: %2 dopasowań (wzorzec: %3 bajtów)")
            .arg(m_frameStore.size()).arg(matches.size()).arg(pattern.size()));
}

void CanForensicsWidget::clearFrameStore() {
    m_frameStore.clear();
    m_searchTable->setRowCount(0);
    m_searchStats->setText("Bufor wyczyszczony — 0 ramek");
}
