#include "DbcAutoWidget.h"
#include "DbcEditorWidget.h"
#include <QFileDialog>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSplitter>
#include <QVBoxLayout>
#include <sstream>
#include <iomanip>

DbcAutoWidget::DbcAutoWidget(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);

    // ── Controls ───────────────────────────────────────────────────────────────
    auto *ctrlGroup  = new QGroupBox("Automatyczne generowanie DBC z obserwowanego ruchu CAN");
    auto *ctrlLayout = new QHBoxLayout(ctrlGroup);

    m_captureLabel = new QLabel("Ramki: 0");
    m_captureLabel->setMinimumWidth(120);

    ctrlLayout->addWidget(new QLabel("Przechwycono:"));
    ctrlLayout->addWidget(m_captureLabel);
    ctrlLayout->addWidget(new QLabel("Min. ramek/ID:"));
    m_minFramesSpin = new QSpinBox;
    m_minFramesSpin->setRange(3, 500);
    m_minFramesSpin->setValue(10);
    ctrlLayout->addWidget(m_minFramesSpin);

    m_analyzeBtn = new QPushButton("⚙  Analizuj i generuj DBC");
    m_analyzeBtn->setStyleSheet("font-weight: bold; color: #44aaff;");
    m_clearBtn   = new QPushButton("Wyczyść bufor");
    m_exportBtn  = new QPushButton("Zapisz .dbc");
    m_applyBtn   = new QPushButton("Zastosuj w edytorze DBC");
    m_exportBtn->setEnabled(false);
    m_applyBtn->setEnabled(false);

    ctrlLayout->addStretch(1);
    ctrlLayout->addWidget(m_analyzeBtn);
    ctrlLayout->addWidget(m_clearBtn);
    ctrlLayout->addWidget(m_exportBtn);
    ctrlLayout->addWidget(m_applyBtn);
    root->addWidget(ctrlGroup);

    m_progressBar = new QProgressBar;
    m_progressBar->setVisible(false);
    m_progressBar->setRange(0, 0); // indeterminate
    root->addWidget(m_progressBar);

    // ── Splitter: message table | detail ───────────────────────────────────────
    auto *splitter = new QSplitter(Qt::Vertical);

    m_msgTable = new QTableWidget(0, 6);
    m_msgTable->setHorizontalHeaderLabels({"CAN ID", "Nazwa", "DLC", "Ramki", "Okres [ms]", "Typ czasowy"});
    m_msgTable->horizontalHeader()->setStretchLastSection(true);
    m_msgTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_msgTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_msgTable->setAlternatingRowColors(true);
    m_msgTable->verticalHeader()->setVisible(false);
    splitter->addWidget(m_msgTable);

    m_detailView = new QTextEdit;
    m_detailView->setReadOnly(true);
    m_detailView->setFont(QFont("Courier New", 9));
    m_detailView->setPlaceholderText("Wybierz wiadomość, aby zobaczyć klasyfikację bajtów i sygnały...");
    splitter->addWidget(m_detailView);
    splitter->setSizes({280, 250});

    root->addWidget(splitter, 1);

    m_statusLabel = new QLabel(
        "Nasłuchiwanie ramek CAN... Kliknij \"Analizuj\" aby wygenerować kandydatów na sygnały DBC.");
    m_statusLabel->setStyleSheet("color: #aaaaaa; font-size: 11px;");
    root->addWidget(m_statusLabel);

    connect(m_analyzeBtn, &QPushButton::clicked, this, &DbcAutoWidget::runAnalysis);
    connect(m_clearBtn,   &QPushButton::clicked, this, &DbcAutoWidget::clearCapture);
    connect(m_exportBtn,  &QPushButton::clicked, this, &DbcAutoWidget::exportDbc);
    connect(m_applyBtn,   &QPushButton::clicked, this, &DbcAutoWidget::applyToEditor);
    connect(m_msgTable,   &QTableWidget::currentCellChanged,
            this, [this](int row, int, int, int) { onMessageSelected(row); });
}

// ── Frame ingestion ───────────────────────────────────────────────────────────

void DbcAutoWidget::processFrame(const CanFrame &frame) {
    if (static_cast<int>(m_frames.size()) >= m_maxFrames) {
        // Ring: evict oldest quarter
        m_frames.erase(m_frames.begin(), m_frames.begin() + m_maxFrames / 4);
    }
    m_frames.push_back(frame);

    if (m_frames.size() % 500 == 0)
        m_captureLabel->setText(QString("Ramki: %1").arg(m_frames.size()));
}

void DbcAutoWidget::clearCapture() {
    m_frames.clear();
    m_lastResult.clear();
    m_msgTable->setRowCount(0);
    m_detailView->clear();
    m_captureLabel->setText("Ramki: 0");
    m_exportBtn->setEnabled(false);
    m_applyBtn->setEnabled(false);
    m_statusLabel->setText("Bufor wyczyszczony. Nasłuchiwanie...");
}

// ── Analysis ──────────────────────────────────────────────────────────────────

void DbcAutoWidget::runAnalysis() {
    m_captureLabel->setText(QString("Ramki: %1").arg(m_frames.size()));

    if (m_frames.size() < static_cast<size_t>(m_minFramesSpin->value())) {
        m_statusLabel->setText(
            QString("Za mało ramek (%1). Poczekaj na więcej ruchu lub obniż próg.")
            .arg(m_frames.size()));
        return;
    }

    m_progressBar->setVisible(true);
    m_analyzeBtn->setEnabled(false);
    qApp->processEvents();

    DbcAutoGenerator gen;
    m_lastResult = gen.analyze(m_frames);

    // Apply minimum frames filter from spinbox
    int minF = m_minFramesSpin->value();
    m_lastResult.erase(
        std::remove_if(m_lastResult.begin(), m_lastResult.end(),
            [minF](const AutoMessage &m) { return static_cast<int>(m.frameCount) < minF; }),
        m_lastResult.end());

    m_progressBar->setVisible(false);
    m_analyzeBtn->setEnabled(true);

    populateTable(m_lastResult);

    bool any = !m_lastResult.empty();
    m_exportBtn->setEnabled(any);
    m_applyBtn->setEnabled(any);

    m_statusLabel->setText(
        QString("Znaleziono %1 wiadomości CAN. Kliknij wiersz, aby zobaczyć szczegóły sygnałów.")
        .arg(m_lastResult.size()));
}

// ── Table population ──────────────────────────────────────────────────────────

void DbcAutoWidget::populateTable(const std::vector<AutoMessage> &msgs) {
    m_msgTable->setRowCount(0);

    for (const auto &msg : msgs) {
        int row = m_msgTable->rowCount();
        m_msgTable->insertRow(row);

        auto col = [&](int c, const QString &t) {
            m_msgTable->setItem(row, c, new QTableWidgetItem(t));
        };

        col(0, QString("0x%1").arg(msg.id, 3, 16, QChar('0')).toUpper());
        col(1, QString("MSG_%1").arg(msg.id, 3, 16, QChar('0')).toUpper());
        col(2, QString::number(msg.dlc));
        col(3, QString::number(msg.frameCount));
        col(4, msg.periodMs > 0 ? QString::number(msg.periodMs, 'f', 1) : "—");
        col(5, QString::fromStdString(msg.timing));

        if (msg.timing == "periodic")
            m_msgTable->item(row, 5)->setForeground(QColor(0x44, 0xFF, 0x88));
        else if (msg.timing == "sporadic")
            m_msgTable->item(row, 5)->setForeground(QColor(0xFF, 0xAA, 0x44));
    }
}

// ── Detail view ───────────────────────────────────────────────────────────────

void DbcAutoWidget::onMessageSelected(int row) {
    if (row < 0 || row >= static_cast<int>(m_lastResult.size())) return;
    showMessageDetail(m_lastResult[static_cast<size_t>(row)]);
}

void DbcAutoWidget::showMessageDetail(const AutoMessage &msg) {
    std::ostringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');
    ss << "=== CAN ID: 0x" << std::setw(3) << msg.id
       << "  DLC=" << std::dec << msg.dlc
       << "  Ramki=" << msg.frameCount
       << "  Typ=" << msg.timing;
    if (msg.periodMs > 0)
        ss << "  Okres=" << std::fixed << std::setprecision(1) << msg.periodMs << " ms";
    ss << " ===\n\n";

    // Per-byte classification
    ss << "Bajt | Rola       | Min  | Max  | Uniq | Entropia\n";
    ss << "-----|------------|------|------|------|----------\n";
    for (size_t b = 0; b < msg.byteStats.size(); ++b) {
        const auto &bs = msg.byteStats[b];
        ss << std::dec << std::setw(4) << b << " | "
           << std::left << std::setw(10) << DbcAutoGenerator::roleName(bs.role) << " | "
           << std::right << std::hex << std::setw(4) << (int)bs.minVal << " | "
           << std::setw(4) << (int)bs.maxVal << " | "
           << std::dec << std::setw(4) << bs.uniqueValues.size() << " | "
           << std::fixed << std::setprecision(2) << bs.entropy << "\n";
    }

    // Discovered signals
    ss << "\nSygnały (" << msg.sigs.size() << "):\n";
    ss << "Bit  | Len | Rola       | Min        | Max        | Scale      | Nazwa\n";
    ss << "-----|-----|------------|------------|------------|------------|------------------\n";
    for (const auto &s : msg.sigs) {
        ss << std::dec << std::setw(4) << s.startBit << " | "
           << std::setw(3) << s.bitLength << " | "
           << std::left << std::setw(10) << DbcAutoGenerator::roleName(s.role) << " | "
           << std::right << std::setw(10) << std::fixed << std::setprecision(3) << s.minimum << " | "
           << std::setw(10) << s.maximum << " | "
           << std::setw(10) << s.scale << " | "
           << s.name << "\n";
    }

    m_detailView->setPlainText(QString::fromStdString(ss.str()));
}

// ── Export ────────────────────────────────────────────────────────────────────

void DbcAutoWidget::exportDbc() {
    if (m_lastResult.empty()) return;

    QString path = QFileDialog::getSaveFileName(this, "Zapisz wygenerowany DBC",
        "auto_generated.dbc", "DBC (*.dbc)");
    if (path.isEmpty()) return;

    auto stdMsgs = DbcAutoGenerator::toDbcMessages(m_lastResult);
    QVector<DbcMessage> dbcMsgs(stdMsgs.begin(), stdMsgs.end());
    QString content = DbcParser::serialize(dbcMsgs);

    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << content;
        m_statusLabel->setText("Zapisano: " + path);
    } else {
        m_statusLabel->setText("Błąd zapisu: " + path);
    }
}

void DbcAutoWidget::applyToEditor() {
    if (!m_dbcEditor || m_lastResult.empty()) return;
    auto stdMsgs = DbcAutoGenerator::toDbcMessages(m_lastResult);
    QVector<DbcMessage> dbcMsgs(stdMsgs.begin(), stdMsgs.end());
    m_dbcEditor->setMessages(dbcMsgs);
    m_statusLabel->setText(
        QString("Zastosowano %1 wiadomości w edytorze DBC").arg(dbcMsgs.size()));
}
