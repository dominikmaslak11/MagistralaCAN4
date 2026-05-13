#include "CanPrototypeExporterWidget.h"
#include "CanFrameModel.h"
#include "DbcParser.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QCheckBox>
#include <QFont>
#include <QFileDialog>
#include <QMessageBox>
#include <algorithm>

CanPrototypeExporterWidget::CanPrototypeExporterWidget(QWidget *parent)
    : QWidget(parent)
{
    // ── Config group ───────────────────────────────────────────────────────
    auto *cfgGroup  = new QGroupBox("Konfiguracja generatora kodu");
    auto *cfgLayout = new QGridLayout(cfgGroup);

    cfgLayout->addWidget(new QLabel("Format:"), 0, 0);
    m_formatCombo = new QComboBox;
    m_formatCombo->addItems({"Python (python-can)", "Lua", "Arduino C++ (MCP2515)"});
    cfgLayout->addWidget(m_formatCombo, 0, 1);

    cfgLayout->addWidget(new QLabel("Interfejs / typ:"), 1, 0);
    m_busTypeCombo = new QComboBox;
    m_busTypeCombo->addItems({"socketcan", "pcan", "usb2can", "kvaser", "vector", "slcan", "serial"});
    cfgLayout->addWidget(m_busTypeCombo, 1, 1);

    cfgLayout->addWidget(new QLabel("Kanał:"), 2, 0);
    m_channelEdit = new QLineEdit("can0");
    cfgLayout->addWidget(m_channelEdit, 2, 1);

    cfgLayout->addWidget(new QLabel("Prędkość (bps):"), 3, 0);
    m_bitrateBox = new QSpinBox;
    m_bitrateBox->setRange(10000, 5000000);
    m_bitrateBox->setSingleStep(125000);
    m_bitrateBox->setValue(500000);
    cfgLayout->addWidget(m_bitrateBox, 3, 1);

    m_rxCheck = new QCheckBox("Dodaj pętlę odbierania (receiver loop)");
    cfgLayout->addWidget(m_rxCheck, 4, 0, 1, 2);

    auto *btnRow = new QHBoxLayout;
    m_generateBtn = new QPushButton("⚙ Generuj kod");
    m_exportBtn   = new QPushButton("💾 Eksportuj do pliku...");
    m_exportBtn->setEnabled(false);
    btnRow->addWidget(m_generateBtn);
    btnRow->addWidget(m_exportBtn);
    btnRow->addStretch();
    cfgLayout->addLayout(btnRow, 5, 0, 1, 2);

    // ── Stats label ────────────────────────────────────────────────────────
    m_statsLabel = new QLabel("Oczekiwanie na ramki CAN...");
    m_statsLabel->setStyleSheet("font-style: italic; color: #888;");

    // ── Code preview ───────────────────────────────────────────────────────
    auto *codeGroup  = new QGroupBox("Podgląd wygenerowanego kodu");
    auto *codeLayout = new QVBoxLayout(codeGroup);
    m_codeView = new QTextEdit;
    m_codeView->setReadOnly(true);
    QFont mono("Courier New", 9);
    mono.setStyleHint(QFont::Monospace);
    m_codeView->setFont(mono);
    codeLayout->addWidget(m_codeView);

    // ── Main layout ────────────────────────────────────────────────────────
    auto *main = new QVBoxLayout(this);
    main->addWidget(cfgGroup);
    main->addWidget(m_statsLabel);
    main->addWidget(codeGroup, 1);

    // ── Connections ────────────────────────────────────────────────────────
    connect(m_generateBtn, &QPushButton::clicked, this, &CanPrototypeExporterWidget::onGenerate);
    connect(m_exportBtn,   &QPushButton::clicked, this, &CanPrototypeExporterWidget::onExport);
    connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CanPrototypeExporterWidget::onFormatChanged);

    onFormatChanged(0);
}

// ── Incoming frame ─────────────────────────────────────────────────────────────

void CanPrototypeExporterWidget::onFrame(const CanFrame &frame, uint64_t /*tsUs*/) {
    auto &hist = m_history[frame.id];
    hist.push_back(frame);
    if ((int)hist.size() > kMaxHistory)
        hist.pop_front();

    // Update stats label
    m_statsLabel->setText(
        QString("IDs śledzonych: %1  |  historia: max %2 ramek/ID")
            .arg(m_history.size()).arg(kMaxHistory));
}

// ── Counter detection ──────────────────────────────────────────────────────────

QVector<int> CanPrototypeExporterWidget::detectCounterBytes(uint32_t id) const {
    auto it = m_history.find(id);
    if (it == m_history.end() || (int)it->second.size() < 4) return {};

    const auto &hist = it->second;
    int dlc = hist.front().dlc;
    QVector<int> counters;

    for (int b = 0; b < std::min(dlc, 8); ++b) {
        int matches = 0, total = 0;
        for (size_t i = 1; i < hist.size(); ++i) {
            uint8_t prev = hist[i-1].data[b];
            uint8_t curr = hist[i  ].data[b];
            if ((uint8_t)(curr - prev) == 1) ++matches;
            ++total;
        }
        // ≥60% consecutive +1 pairs → counter byte
        if (total > 0 && matches * 100 / total >= 60)
            counters.append(b);
    }
    return counters;
}

// ── Build export info ──────────────────────────────────────────────────────────

QVector<FrameExportInfo> CanPrototypeExporterWidget::buildExportInfo() const {
    // Collect and sort tracked IDs
    QVector<uint32_t> ids;
    for (const auto &[id, _] : m_history)
        ids.append(id);
    std::sort(ids.begin(), ids.end());

    // Also include frames from CanFrameModel if available (covers IDs not yet in history)
    if (m_model) {
        for (const auto &f : m_model->allFrames()) {
            if (!ids.contains(f.id))
                ids.append(f.id);
        }
        std::sort(ids.begin(), ids.end());
    }

    QVector<FrameExportInfo> infos;

    for (uint32_t id : ids) {
        FrameExportInfo info;
        info.id = id;

        // Payload + DLC from history or model
        auto histIt = m_history.find(id);
        if (histIt != m_history.end() && !histIt->second.empty()) {
            const CanFrame &last = histIt->second.back();
            info.extended = last.extended;
            info.dlc      = last.dlc;
            for (int b = 0; b < last.dlc; ++b)
                info.sampleData.append(last.data[b]);
        } else if (m_model) {
            // Fall back to model
            for (const auto &f : m_model->allFrames()) {
                if (f.id == id) {
                    info.extended = f.extended;
                    info.dlc      = f.dlc;
                    for (int b = 0; b < f.dlc; ++b)
                        info.sampleData.append(f.data[b]);
                    break;
                }
            }
        }
        if (info.dlc == 0) info.dlc = 8;

        // Name + signals from DBC
        if (m_dbcParser) {
            DbcMessage msg = m_dbcParser->messageForId(id);
            if (!msg.name.isEmpty()) {
                info.name    = msg.name;
                info.sigList = msg.sigList;
                info.dlc     = msg.dlc;
            }
        }
        if (info.name.isEmpty())
            info.name = QString("0x%1").arg(id, 0, 16).toUpper();

        // Period from module profiles
        for (const auto &prof : m_profiles) {
            for (const auto &pid : prof.periodicIds) {
                if (pid.id == id) {
                    info.periodMs = pid.avgIntervalMs;
                    info.jitterMs = pid.jitterMs;
                    break;
                }
            }
            if (info.periodMs > 0.0) break;
        }

        // Counter byte detection (needs enough history)
        info.counterBytes = detectCounterBytes(id);

        infos.append(info);
    }

    return infos;
}

// ── Generate ───────────────────────────────────────────────────────────────────

void CanPrototypeExporterWidget::onGenerate() {
    auto infos = buildExportInfo();
    if (infos.isEmpty()) {
        m_codeView->setPlainText("// Brak danych — najpierw uruchom sniffer CAN.");
        return;
    }

    CanPrototypeExporter::Config cfg;
    switch (m_formatCombo->currentIndex()) {
        case 0: cfg.format = CanPrototypeExporter::Format::Python;  break;
        case 1: cfg.format = CanPrototypeExporter::Format::Lua;     break;
        case 2: cfg.format = CanPrototypeExporter::Format::Arduino; break;
        default: break;
    }
    cfg.busType      = m_busTypeCombo->currentText();
    cfg.channel      = m_channelEdit->text().trimmed();
    cfg.bitrate      = m_bitrateBox->value();
    cfg.withReceiver = m_rxCheck->isChecked();

    QString code = CanPrototypeExporter::generate(infos, cfg);
    m_codeView->setPlainText(code);
    m_exportBtn->setEnabled(true);

    int periodic = 0;
    for (const auto &f : infos) if (f.periodMs > 0.0) ++periodic;
    int withSigs    = 0;
    for (const auto &f : infos) if (!f.sigList.isEmpty()) ++withSigs;
    int withCounter = 0;
    for (const auto &f : infos) if (!f.counterBytes.isEmpty()) ++withCounter;

    m_statsLabel->setText(
        QString("Wygenerowano: %1 ramek  |  heartbeat: %2  |  sygnały DBC: %3  |  liczniki: %4")
            .arg(infos.size()).arg(periodic).arg(withSigs).arg(withCounter));
}

// ── Export to file ─────────────────────────────────────────────────────────────

void CanPrototypeExporterWidget::onExport() {
    QString code = m_codeView->toPlainText();
    if (code.isEmpty()) return;

    QString filter;
    QString defaultExt;
    switch (m_formatCombo->currentIndex()) {
        case 0: filter = "Python (*.py)";       defaultExt = ".py";  break;
        case 1: filter = "Lua (*.lua)";         defaultExt = ".lua"; break;
        case 2: filter = "C++ Arduino (*.ino)"; defaultExt = ".ino"; break;
        default: filter = "Text (*.txt)"; defaultExt = ".txt"; break;
    }

    QString path = QFileDialog::getSaveFileName(
        this, "Eksportuj prototyp modułu CAN",
        "can_prototype" + defaultExt, filter);
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Błąd", "Nie można zapisać: " + path);
        return;
    }
    QTextStream ts(&f);
    ts << code;
    m_statsLabel->setText("Zapisano: " + path);
}

// ── Format changed ─────────────────────────────────────────────────────────────

void CanPrototypeExporterWidget::onFormatChanged(int index) {
    bool isArduino = (index == 2);
    bool isPython  = (index == 0);
    m_busTypeCombo->setEnabled(isPython);
    m_channelEdit->setEnabled(!isArduino);
    m_channelEdit->setToolTip(isArduino ? "Kanał ustawiany w szkicu Arduino" : "");
}
