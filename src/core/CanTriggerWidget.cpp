#include "CanTriggerWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

namespace {

const char *kSS = R"(
QGroupBox {
    color: #ff66cc; border: 1px solid #2a2a3c; border-radius: 4px;
    margin-top: 8px; padding: 6px; font-family: Consolas; font-weight: bold;
}
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 4px; }
QLabel { color: #c0c0c0; font-family: Consolas; }
QLineEdit, QSpinBox, QComboBox {
    background: #0a0e17; color: #00ffaa; border: 1px solid #e94560;
    border-radius: 3px; padding: 3px 6px; font-family: Consolas;
}
QComboBox QAbstractItemView { background: #0a0e17; color: #00ffaa; selection-background-color: #e94560; }
QPushButton {
    background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560;
    border-radius: 4px; padding: 4px 12px; font-family: Consolas;
}
QPushButton:hover { background: #2a2a3e; }
QPushButton:disabled { color: #555; border-color: #333; }
QTableWidget {
    background: #0a0e17; color: #c0c0c0; gridline-color: #2a2a3c;
    font-family: Consolas; font-size: 11px; selection-background-color: #1a2a3a;
}
QHeaderView::section {
    background: #1a1a2e; color: #ff66cc; border: 1px solid #2a2a3c;
    padding: 4px; font-weight: bold; font-family: Consolas;
}
)";

} // namespace

CanTriggerWidget::CanTriggerWidget(QWidget *parent) : QWidget(parent) {
    setStyleSheet(kSS);
    auto *outerLay = new QVBoxLayout(this);
    outerLay->setContentsMargins(6, 6, 6, 6);
    outerLay->setSpacing(6);

    // ── Title ────────────────────────────────────────────────────────────────
    auto *titleLbl = new QLabel(
        "Wyzwalacz Przechwytywania CAN — zbierz ramki wokół zdarzenia diagnostycznego");
    titleLbl->setStyleSheet("color: #ff66cc; font-weight: bold; font-size: 13px; font-family: Consolas;");
    outerLay->addWidget(titleLbl);

    // ── Top row: condition + buffer boxes ────────────────────────────────────
    auto *topRow = new QHBoxLayout;

    // Condition box
    auto *condBox = new QGroupBox("Warunek wyzwalacza");
    auto *condGrid = new QGridLayout(condBox);
    condGrid->setSpacing(6);

    condGrid->addWidget(new QLabel("CAN ID (hex):"), 0, 0);
    m_idEdit = new QLineEdit("0x100");
    m_idEdit->setFixedWidth(90);
    condGrid->addWidget(m_idEdit, 0, 1);

    condGrid->addWidget(new QLabel("Tryb:"), 1, 0);
    m_modeCombo = new QComboBox;
    m_modeCombo->addItem("Dowolna ramka z ID",         0);
    m_modeCombo->addItem("Bajt == wartość (z maską)", 1);
    m_modeCombo->addItem("Dowolna ramka błędu",        2);
    condGrid->addWidget(m_modeCombo, 1, 1, 1, 3);

    condGrid->addWidget(new QLabel("Offset bajtu:"), 2, 0);
    m_byteOffsetSpin = new QSpinBox;
    m_byteOffsetSpin->setRange(0, 7);
    m_byteOffsetSpin->setFixedWidth(60);
    condGrid->addWidget(m_byteOffsetSpin, 2, 1);

    condGrid->addWidget(new QLabel("Wartość (hex):"), 2, 2);
    m_valueEdit = new QLineEdit("0x00");
    m_valueEdit->setFixedWidth(70);
    condGrid->addWidget(m_valueEdit, 2, 3);

    condGrid->addWidget(new QLabel("Maska (hex):"), 3, 0);
    m_maskEdit = new QLineEdit("0xFF");
    m_maskEdit->setFixedWidth(70);
    condGrid->addWidget(m_maskEdit, 3, 1);

    auto updateByteControls = [this]() {
        bool byteMode = (m_modeCombo->currentIndex() == 1);
        m_byteOffsetSpin->setEnabled(byteMode);
        m_valueEdit->setEnabled(byteMode);
        m_maskEdit->setEnabled(byteMode);
    };
    updateByteControls();
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [updateByteControls](int) { updateByteControls(); });

    topRow->addWidget(condBox, 2);

    // Buffer box
    auto *bufBox = new QGroupBox("Bufor pre/post");
    auto *bufGrid = new QGridLayout(bufBox);
    bufGrid->setSpacing(6);

    bufGrid->addWidget(new QLabel("Pre-trigger\n(ramki przed):"), 0, 0);
    m_preSpin = new QSpinBox;
    m_preSpin->setRange(0, 2000);
    m_preSpin->setValue(50);
    m_preSpin->setFixedWidth(80);
    bufGrid->addWidget(m_preSpin, 0, 1);

    bufGrid->addWidget(new QLabel("Post-trigger\n(ramki po):"), 1, 0);
    m_postSpin = new QSpinBox;
    m_postSpin->setRange(0, 2000);
    m_postSpin->setValue(100);
    m_postSpin->setFixedWidth(80);
    bufGrid->addWidget(m_postSpin, 1, 1);

    auto *hintLbl = new QLabel("Łącznie max:\npre + 1 + post ramek");
    hintLbl->setStyleSheet("color: #888; font-size: 10px; font-family: Consolas;");
    bufGrid->addWidget(hintLbl, 2, 0, 1, 2);

    topRow->addWidget(bufBox, 1);
    outerLay->addLayout(topRow);

    // ── Control bar ──────────────────────────────────────────────────────────
    auto *ctrlRow = new QHBoxLayout;

    m_armBtn = new QPushButton("Uzbrój wyzwalacz");
    m_armBtn->setStyleSheet(
        "QPushButton { background: #1a1a2e; color: #00ffaa; border: 2px solid #00ffaa; "
        "  border-radius: 4px; padding: 6px 18px; font-family: Consolas; font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background: #00ffaa; color: #0a0e17; }"
        "QPushButton:checked { background: #e94560; color: white; border-color: #e94560; }");
    m_armBtn->setCheckable(true);
    connect(m_armBtn, &QPushButton::clicked, this, &CanTriggerWidget::toggleArm);
    ctrlRow->addWidget(m_armBtn);

    m_statusLbl = new QLabel("Gotowy — ustaw warunek i kliknij 'Uzbrój'");
    m_statusLbl->setStyleSheet("color: #c0c0c0; font-family: Consolas; font-size: 12px; padding: 0 12px;");
    ctrlRow->addWidget(m_statusLbl, 1);

    m_exportBtn = new QPushButton("Eksportuj candump");
    m_exportBtn->setEnabled(false);
    connect(m_exportBtn, &QPushButton::clicked, this, &CanTriggerWidget::exportCapture);
    ctrlRow->addWidget(m_exportBtn);

    m_clearBtn = new QPushButton("Wyczyść");
    m_clearBtn->setEnabled(false);
    connect(m_clearBtn, &QPushButton::clicked, this, &CanTriggerWidget::clearCapture);
    ctrlRow->addWidget(m_clearBtn);

    outerLay->addLayout(ctrlRow);

    // ── Capture table ────────────────────────────────────────────────────────
    m_captureTable = new QTableWidget(0, 6);
    m_captureTable->setHorizontalHeaderLabels({"#", "Czas (s)", "Typ", "CAN ID", "DLC", "Dane"});
    m_captureTable->horizontalHeader()->setStretchLastSection(true);
    m_captureTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_captureTable->verticalHeader()->hide();
    m_captureTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_captureTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_captureTable->setSortingEnabled(false);
    outerLay->addWidget(m_captureTable, 1);

    setStyleSheet(kSS);
}

// ── Arming ────────────────────────────────────────────────────────────────────

void CanTriggerWidget::toggleArm() {
    if (m_armed) {
        // Disarm
        m_armed = false;
        m_recorder.reset();
        m_armBtn->setChecked(false);
        m_armBtn->setText("Uzbrój wyzwalacz");
        setStatus("Rozzbrojono", "#c0c0c0");
    } else {
        // Arm
        buildTrigger();
        m_armed = true;
        m_triggerFrameIdx = -1;
        m_armBtn->setChecked(true);
        m_armBtn->setText("Rozbrój");
        setStatus(
            QString("Uzbrojony — oczekiwanie na wyzwalacz  [pre=%1  post=%2]")
                .arg(m_preSpin->value()).arg(m_postSpin->value()),
            "#00ffaa");
        m_exportBtn->setEnabled(false);
        m_clearBtn->setEnabled(false);
    }
}

void CanTriggerWidget::buildTrigger() {
    m_recorder.reset();
    m_recorder.setPreCount(m_preSpin->value());
    m_recorder.setPostCount(m_postSpin->value());

    const int mode = m_modeCombo->currentIndex();
    bool ok;
    const uint32_t canId = m_idEdit->text().toUInt(&ok, 16);
    const int      offset = m_byteOffsetSpin->value();
    const uint8_t  val  = static_cast<uint8_t>(m_valueEdit->text().toUInt(&ok, 16));
    const uint8_t  mask = static_cast<uint8_t>(m_maskEdit->text().toUInt(&ok, 16));

    if (mode == 0) {
        // Any frame matching CAN ID
        m_recorder.setTrigger([canId](const CanFrame &f) {
            return f.id == canId;
        });
    } else if (mode == 1) {
        // Byte value match
        m_recorder.setTrigger([canId, offset, val, mask](const CanFrame &f) {
            return f.id == canId
                && f.dlc > offset
                && (static_cast<uint8_t>(f.data[offset] & mask) == static_cast<uint8_t>(val & mask));
        });
    } else {
        // Any error frame
        m_recorder.setTrigger([](const CanFrame &f) {
            return f.error;
        });
    }
}

// ── processFrame ──────────────────────────────────────────────────────────────

void CanTriggerWidget::processFrame(const CanFrame &frame) {
    if (!m_armed) return;

    const bool fired = m_recorder.addFrame(frame);

    if (fired && m_triggerFrameIdx < 0) {
        m_triggerFrameIdx = static_cast<int>(m_recorder.capture().size()) - 1;
        const int remaining = m_postSpin->value();
        setStatus(
            QString("WYZWOLONO!  Przechwytywanie %1 ramek post-trigger...").arg(remaining),
            "#ffaa00");
    }

    if (m_recorder.captureComplete()) {
        m_armed = false;
        onCaptureComplete();
    }
}

// ── After capture ─────────────────────────────────────────────────────────────

void CanTriggerWidget::onCaptureComplete() {
    m_armBtn->setChecked(false);
    m_armBtn->setText("Uzbrój wyzwalacz");
    m_exportBtn->setEnabled(true);
    m_clearBtn->setEnabled(true);

    const int total = static_cast<int>(m_recorder.capture().size());
    setStatus(
        QString("Gotowe — przechwycono %1 ramek  (wyzwalacz: #%2)")
            .arg(total).arg(m_triggerFrameIdx >= 0 ? m_triggerFrameIdx : 0),
        "#00aaff");

    populateCaptureTable();
}

void CanTriggerWidget::populateCaptureTable() {
    const auto &cap = m_recorder.capture();
    m_captureTable->setSortingEnabled(false);
    m_captureTable->setRowCount(static_cast<int>(cap.size()));

    for (int i = 0; i < static_cast<int>(cap.size()); ++i) {
        const auto &f = cap[i];

        const bool isTrigger = (i == m_triggerFrameIdx);
        const bool isPre     = (m_triggerFrameIdx >= 0 && i < m_triggerFrameIdx);

        const QColor bg = isTrigger ? QColor("#2e1800")   // orange trigger
                        : isPre     ? QColor("#0a0e17")   // dark pre
                                    : QColor("#0a1420");  // slightly blue post
        const QColor fg = isTrigger ? QColor("#ffaa00")
                        : isPre     ? QColor("#888888")
                                    : QColor("#c0c0c0");

        QString typeText = isTrigger ? "TRIGGER"
                         : isPre     ? "PRE"
                                     : "POST";

        QString dataHex;
        for (int b = 0; b < f.dlc && b < 8; ++b)
            dataHex += QString("%1 ").arg(f.data[b], 2, 16, QChar('0')).toUpper();
        dataHex = dataHex.trimmed();

        auto mkItem = [&](const QString &text) {
            auto *it = new QTableWidgetItem(text);
            it->setBackground(bg);
            it->setForeground(fg);
            it->setTextAlignment(Qt::AlignCenter);
            if (isTrigger) {
                QFont font = it->font();
                font.setBold(true);
                it->setFont(font);
            }
            return it;
        };

        m_captureTable->setItem(i, 0, mkItem(QString::number(i)));
        m_captureTable->setItem(i, 1, mkItem(QString::number(static_cast<double>(f.timestamp) / 1'000'000.0, 'f', 6)));
        m_captureTable->setItem(i, 2, mkItem(typeText));
        m_captureTable->setItem(i, 3, mkItem(QString("0x%1").arg(f.id, 3, 16, QChar('0')).toUpper()));
        m_captureTable->setItem(i, 4, mkItem(QString::number(f.dlc)));
        auto *dataItem = mkItem(dataHex);
        dataItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_captureTable->setItem(i, 5, dataItem);
    }
}

// ── Export / Clear ────────────────────────────────────────────────────────────

void CanTriggerWidget::exportCapture() {
    const QString path = QFileDialog::getSaveFileName(
        this, "Eksportuj przechwycone ramki", "trigger_capture.log",
        "Candump log (*.log);;Wszystkie pliki (*)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&f);
    out << "# MagistralaCAN4 trigger capture\n";
    out << "# Captured: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
    out << "# Trigger frame index: " << m_triggerFrameIdx << "\n";

    for (int i = 0; i < static_cast<int>(m_recorder.capture().size()); ++i) {
        const auto &fr = m_recorder.capture()[i];
        const QString type = (i == m_triggerFrameIdx) ? "TRIGGER"
                           : (i < m_triggerFrameIdx)  ? "PRE"
                                                       : "POST";
        QString data;
        for (int b = 0; b < fr.dlc && b < 8; ++b)
            data += QString("%1").arg(fr.data[b], 2, 16, QChar('0')).toUpper();

        out << QString("(%1) vcan0 %2#%3  ; %4\n")
               .arg(static_cast<double>(fr.timestamp) / 1'000'000.0, 0, 'f', 6)
               .arg(fr.id, 3, 16, QChar('0')).toUpper()
               .arg(data)
               .arg(type);
    }
}

void CanTriggerWidget::clearCapture() {
    m_recorder.reset();
    m_triggerFrameIdx = -1;
    m_captureTable->setRowCount(0);
    m_exportBtn->setEnabled(false);
    m_clearBtn->setEnabled(false);
    setStatus("Wyczyszczono — gotowy do uzbrojenia", "#c0c0c0");
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void CanTriggerWidget::setStatus(const QString &text, const QString &color) {
    m_statusLbl->setText(text);
    m_statusLbl->setStyleSheet(
        QString("color: %1; font-family: Consolas; font-size: 12px; padding: 0 12px;").arg(color));
}
