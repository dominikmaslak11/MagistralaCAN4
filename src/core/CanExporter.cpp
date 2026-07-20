#include "CanExporter.h"
#include "CanFrameModel.h"
#include "PcapExporter.h"
#include "Logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QDataStream>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>

static constexpr uint32_t MCAN_MAGIC   = 0x4E41434D;
static constexpr uint32_t MCAN_VERSION = 1;

// ── Konstruktor / UI ─────────────────────────────────────────

CanExporter::CanExporter(CanFrameModel *model, QWidget *parent)
    : QWidget(parent), m_model(model) {
    setupUi();
    if (m_model) {
        connect(m_model, &CanFrameModel::frameBatchUpdated,
                this, &CanExporter::onFilterChanged);
    }
    onFilterChanged();
}

void CanExporter::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);

    // ── Format i ścieżka ──────────────────────────────────────
    auto *destGroup = new QGroupBox("Plik docelowy");
    destGroup->setStyleSheet(
        "QGroupBox { color: #00ffaa; font-weight: bold; border: 1px solid #e94560; "
        "border-radius: 4px; margin-top: 8px; padding-top: 14px; }");
    auto *destLayout = new QFormLayout(destGroup);
    destLayout->setRowWrapPolicy(QFormLayout::WrapAllRows);

    m_formatCombo = new QComboBox;
    m_formatCombo->addItem("CSV (.csv)",               static_cast<int>(Format::CSV));
    m_formatCombo->addItem("candump log (.log)",        static_cast<int>(Format::Candump));
    m_formatCombo->addItem("PCAP / Wireshark (.pcap)",  static_cast<int>(Format::Pcap));
    m_formatCombo->addItem("MCAN binarny (.mcan)",      static_cast<int>(Format::Mcan));
    destLayout->addRow("Format:", m_formatCombo);

    auto *pathRow = new QHBoxLayout;
    m_pathEdit  = new QLineEdit;
    m_pathEdit->setPlaceholderText("Ścieżka do pliku wyjściowego...");
    m_browseBtn = new QPushButton("...");
    m_browseBtn->setFixedWidth(36);
    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(m_browseBtn);
    destLayout->addRow("Plik:", pathRow);
    mainLayout->addWidget(destGroup);

    // ── Filtry ────────────────────────────────────────────────
    auto *filterGroup = new QGroupBox("Filtry eksportu");
    filterGroup->setStyleSheet(
        "QGroupBox { color: #ff66cc; font-weight: bold; border: 1px solid #e94560; "
        "border-radius: 4px; margin-top: 8px; padding-top: 14px; }");
    auto *filterLayout = new QFormLayout(filterGroup);

    auto *idRow = new QHBoxLayout;
    m_idMinEdit = new QLineEdit; m_idMinEdit->setPlaceholderText("0 (hex)");
    m_idMaxEdit = new QLineEdit; m_idMaxEdit->setPlaceholderText("1FFFFFFF (hex)");
    m_idMinEdit->setMaximumWidth(100);
    m_idMaxEdit->setMaximumWidth(100);
    idRow->addWidget(m_idMinEdit);
    idRow->addWidget(new QLabel("–"));
    idRow->addWidget(m_idMaxEdit);
    idRow->addStretch();
    filterLayout->addRow("Zakres ID:", idRow);

    m_timeFilterCheck = new QCheckBox("Filtruj po czasie");
    filterLayout->addRow("", m_timeFilterCheck);

    auto *timeRow = new QHBoxLayout;
    m_timeFromEdit = new QLineEdit; m_timeFromEdit->setPlaceholderText("od (μs)");
    m_timeToEdit   = new QLineEdit; m_timeToEdit->setPlaceholderText("do (μs)");
    m_timeFromEdit->setMaximumWidth(120);
    m_timeToEdit->setMaximumWidth(120);
    m_timeFromEdit->setEnabled(false);
    m_timeToEdit->setEnabled(false);
    timeRow->addWidget(m_timeFromEdit);
    timeRow->addWidget(new QLabel("–"));
    timeRow->addWidget(m_timeToEdit);
    timeRow->addStretch();
    filterLayout->addRow("Zakres czasu:", timeRow);

    connect(m_timeFilterCheck, &QCheckBox::toggled, m_timeFromEdit, &QWidget::setEnabled);
    connect(m_timeFilterCheck, &QCheckBox::toggled, m_timeToEdit,   &QWidget::setEnabled);
    mainLayout->addWidget(filterGroup);

    // ── Statystyki i eksport ──────────────────────────────────
    auto *exportGroup = new QGroupBox("Eksport");
    exportGroup->setStyleSheet(
        "QGroupBox { color: #ffaa00; font-weight: bold; border: 1px solid #e94560; "
        "border-radius: 4px; margin-top: 8px; padding-top: 14px; }");
    auto *exportLayout = new QVBoxLayout(exportGroup);

    m_statsLabel = new QLabel("Ramki do eksportu: —");
    m_statsLabel->setStyleSheet("color: #ffaa00;");
    exportLayout->addWidget(m_statsLabel);

    m_progress = new QProgressBar;
    m_progress->setRange(0, 0);
    m_progress->setVisible(false);
    exportLayout->addWidget(m_progress);

    auto *btnRow = new QHBoxLayout;
    m_exportBtn = new QPushButton("→ Eksportuj");
    m_exportBtn->setStyleSheet(
        "QPushButton { background: #1a1a2e; color: #ffaa00; border: 1px solid #ffaa00; "
        "border-radius: 4px; padding: 6px 20px; font-weight: bold; } "
        "QPushButton:hover { background: #ffaa00; color: #0a0e17; }");
    btnRow->addWidget(m_exportBtn);
    btnRow->addStretch();
    exportLayout->addLayout(btnRow);

    m_resultLabel = new QLabel;
    m_resultLabel->setStyleSheet("color: #888;");
    exportLayout->addWidget(m_resultLabel);

    mainLayout->addWidget(exportGroup);
    mainLayout->addStretch();

    // ── Połączenia ────────────────────────────────────────────
    connect(m_browseBtn,  &QPushButton::clicked, this, &CanExporter::onBrowse);
    connect(m_exportBtn,  &QPushButton::clicked, this, &CanExporter::onExport);
    connect(m_formatCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        QString p = m_pathEdit->text().trimmed();
        if (!p.isEmpty()) {
            QFileInfo fi(p);
            m_pathEdit->setText(fi.absolutePath() + "/" + fi.completeBaseName() + defaultExtension());
        }
    });
    connect(m_idMinEdit, &QLineEdit::textChanged, this, &CanExporter::onFilterChanged);
    connect(m_idMaxEdit, &QLineEdit::textChanged, this, &CanExporter::onFilterChanged);
    connect(m_timeFilterCheck, &QCheckBox::toggled, this, &CanExporter::onFilterChanged);
    connect(m_timeFromEdit, &QLineEdit::textChanged, this, &CanExporter::onFilterChanged);
    connect(m_timeToEdit,   &QLineEdit::textChanged, this, &CanExporter::onFilterChanged);

    setStyleSheet(R"(
        QLineEdit, QComboBox { background: #1a1a2e; color: #00ffaa;
            border: 1px solid #e94560; border-radius: 4px; padding: 3px 8px; }
        QLabel { color: #c0c0c0; }
        QCheckBox { color: #c0c0c0; }
    )");
}

// ── Sloty ─────────────────────────────────────────────────────

void CanExporter::onBrowse() {
    QString ext = defaultExtension();
    static const QMap<QString, QString> filters = {
        {".csv",  "CSV (*.csv)"},
        {".log",  "candump log (*.log)"},
        {".pcap", "PCAP (*.pcap)"},
        {".mcan", "MCAN binary (*.mcan)"},
    };
    QString path = QFileDialog::getSaveFileName(
        this, "Zapisz eksport", "", filters.value(ext, "Pliki (*)"));
    if (!path.isEmpty()) m_pathEdit->setText(path);
}

void CanExporter::onFilterChanged() {
    QVector<CanFrame> frames = filteredFrames();
    int total    = m_model ? m_model->rowCount() : 0;
    int filtered = static_cast<int>(frames.size());
    m_statsLabel->setText(
        QString("Ramki do eksportu: %1 / %2 wszystkich").arg(filtered).arg(total));
}

void CanExporter::onExport() {
    if (!m_model) return;
    QString path = m_pathEdit->text().trimmed();
    if (path.isEmpty()) {
        QMessageBox::warning(this, "Eksport", "Podaj ścieżkę pliku wyjściowego.");
        return;
    }

    QVector<CanFrame> frames = filteredFrames();
    if (frames.isEmpty()) {
        QMessageBox::information(this, "Eksport",
                                 "Brak ramek do eksportu (sprawdź filtry).");
        return;
    }

    Format fmt = static_cast<Format>(m_formatCombo->currentData().toInt());
    m_exportBtn->setEnabled(false);
    m_progress->setVisible(true);
    m_resultLabel->setText("Eksportowanie...");
    m_resultLabel->setStyleSheet("color: #ffaa00;");

    m_watcher = new QFutureWatcher<bool>(this);
    connect(m_watcher, &QFutureWatcher<bool>::finished,
            this, &CanExporter::onExportFinished);

    m_watcher->setFuture(QtConcurrent::run([path, frames, fmt]() -> bool {
        switch (fmt) {
        case Format::CSV:     return CanExporter::exportCsv(path, frames);
        case Format::Candump: return CanExporter::exportCandump(path, frames);
        case Format::Pcap:    return CanExporter::exportPcap(path, frames);
        case Format::Mcan:    return CanExporter::exportMcan(path, frames);
        }
        return false;
    }));
}

void CanExporter::onExportFinished() {
    bool ok = m_watcher->result();
    m_watcher->deleteLater();
    m_watcher = nullptr;

    m_exportBtn->setEnabled(true);
    m_progress->setVisible(false);

    if (ok) {
        QVector<CanFrame> frames = filteredFrames();
        m_resultLabel->setText(
            QString("✓ Wyeksportowano %1 ramek → %2")
                .arg(frames.size()).arg(m_pathEdit->text()));
        m_resultLabel->setStyleSheet("color: #00ffaa; font-weight: bold;");
        Logger::log(QString("Eksport: %1 ramek → %2")
                    .arg(frames.size()).arg(m_pathEdit->text()));
    } else {
        m_resultLabel->setText("⚠ Błąd eksportu — sprawdź ścieżkę i uprawnienia.");
        m_resultLabel->setStyleSheet("color: #e94560; font-weight: bold;");
    }
}

// ── Filtry ────────────────────────────────────────────────────

QVector<CanFrame> CanExporter::filteredFrames() const {
    if (!m_model) return {};
    const QVector<CanFrame> all = m_model->allFrames();

    bool okMin = true, okMax = true;
    uint32_t idMin = 0, idMax = 0x1FFFFFFF;
    QString minStr = m_idMinEdit ? m_idMinEdit->text().trimmed() : QString();
    QString maxStr = m_idMaxEdit ? m_idMaxEdit->text().trimmed() : QString();
    if (!minStr.isEmpty()) idMin = minStr.toUInt(&okMin, 16);
    if (!maxStr.isEmpty()) idMax = maxStr.toUInt(&okMax, 16);
    if (!okMin) idMin = 0;
    if (!okMax) idMax = 0x1FFFFFFF;

    bool useTime = m_timeFilterCheck && m_timeFilterCheck->isChecked();
    uint64_t tsFrom = 0, tsTo = UINT64_MAX;
    if (useTime && m_timeFromEdit && m_timeToEdit) {
        bool okF = true, okT = true;
        tsFrom = m_timeFromEdit->text().toULongLong(&okF);
        tsTo   = m_timeToEdit->text().toULongLong(&okT);
        if (!okF) tsFrom = 0;
        if (!okT) tsTo = UINT64_MAX;
    }

    QVector<CanFrame> result;
    result.reserve(all.size());
    for (const auto &f : all) {
        if (f.id < idMin || f.id > idMax) continue;
        if (useTime && (f.timestamp < tsFrom || f.timestamp > tsTo)) continue;
        result.append(f);
    }
    return result;
}

QString CanExporter::defaultExtension() const {
    switch (static_cast<Format>(m_formatCombo->currentData().toInt())) {
    case Format::CSV:     return ".csv";
    case Format::Candump: return ".log";
    case Format::Pcap:    return ".pcap";
    case Format::Mcan:    return ".mcan";
    }
    return ".bin";
}

// ── Statyczne metody eksportu ────────────────────────────────

bool CanExporter::exportCsv(const QString &path, const QVector<CanFrame> &frames) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&file);
    out << "Index,Timestamp_us,ID_hex,Extended,RTR,FD,DLC,Data_hex\n";
    for (int i = 0; i < frames.size(); ++i) {
        const auto &f = frames[i];
        QString data;
        int maxB = std::min(static_cast<int>(f.dlc), 64);
        for (int b = 0; b < maxB; ++b)
            data += QString("%1").arg(f.data[b], 2, 16, QChar('0')).toUpper();
        out << i << ","
            << f.timestamp << ","
            << QString("%1").arg(f.id, f.extended ? 8 : 3, 16, QChar('0')).toUpper() << ","
            << (f.extended ? 1 : 0) << ","
            << (f.rtr      ? 1 : 0) << ","
            << (f.fd       ? 1 : 0) << ","
            << f.dlc << ","
            << data << "\n";
    }
    return true;
}

bool CanExporter::exportCandump(const QString &path, const QVector<CanFrame> &frames,
                                const QString &iface) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&file);
    for (const auto &f : frames) {
        bool isFd = f.fd || f.xl;
        double tsSec = static_cast<double>(f.timestamp) / 1'000'000.0;
        QString idStr = QString("%1").arg(f.id, f.extended ? 8 : 3, 16, QChar('0')).toUpper();
        QString line = QString("(%1) %2 %3")
            .arg(tsSec, 0, 'f', 6)
            .arg(iface)
            .arg(idStr);
        if (isFd) {
            line += "##0";
            int maxB = std::min(static_cast<int>(f.dlc), 64);
            for (int b = 0; b < maxB; ++b)
                line += QString("%1").arg(f.data[b], 2, 16, QChar('0')).toUpper();
        } else {
            line += QString("#%1").arg(f.dlc);
            int maxB = std::min(static_cast<int>(f.dlc), 8);
            for (int b = 0; b < maxB; ++b)
                line += QString("%1").arg(f.data[b], 2, 16, QChar('0')).toUpper();
        }
        out << line << "\n";
    }
    return true;
}

bool CanExporter::exportPcap(const QString &path, const QVector<CanFrame> &frames) {
    std::vector<CanFrame> vec(frames.cbegin(), frames.cend());
    std::vector<uint64_t> ts;
    ts.reserve(frames.size());
    for (const auto &f : frames) ts.push_back(f.timestamp);
    return PcapExporter::exportFrames(path, vec, ts);
}

bool CanExporter::exportMcan(const QString &path, const QVector<CanFrame> &frames) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << (quint32)MCAN_MAGIC << (quint32)MCAN_VERSION;
    for (const auto &f : frames) {
        quint8 flags = 0;
        if (f.extended) flags |= 0x01;
        if (f.rtr)      flags |= 0x02;
        if (f.error)    flags |= 0x04;
        if (f.fd)       flags |= 0x08;
        if (f.xl)       flags |= 0x10;
        stream << (quint64)f.timestamp << (quint32)f.id << flags;
        if (f.xl) {
            stream << (quint16)f.dlc;
        } else {
            stream << (quint8)f.dlc;
        }
        int dataLen = std::min(static_cast<int>(f.dlc), 64);
        stream.writeRawData(reinterpret_cast<const char *>(f.data.data()), dataLen);
    }
    return true;
}
