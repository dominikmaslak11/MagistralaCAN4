#include "LinWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QGroupBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QFont>
#include <cstdio>

LinWidget::LinWidget(QWidget *parent) : QWidget(parent) {
    buildUi();
}

void LinWidget::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // ── Manual parse group ──────────────────────────────────────────────────
    auto *manGroup = new QGroupBox("Manual Parse", this);
    auto *manForm  = new QFormLayout(manGroup);

    m_hexInput = new QLineEdit(this);
    m_hexInput->setPlaceholderText("e.g. 55 D9 01 02 24  (hex bytes, space-separated)");
    m_hexInput->setFont(QFont("Courier New", 10));
    manForm->addRow("Bytes (hex):", m_hexInput);

    m_csTypeCombo = new QComboBox(this);
    m_csTypeCombo->addItem("Enhanced (LIN 2.x)", static_cast<int>(LinFrame::ChecksumType::Enhanced));
    m_csTypeCombo->addItem("Classic  (LIN 1.x)", static_cast<int>(LinFrame::ChecksumType::Classic));
    manForm->addRow("Checksum type:", m_csTypeCombo);

    auto *btnRow = new QHBoxLayout;
    m_parseBtn = new QPushButton("Parse", this);
    m_clearBtn = new QPushButton("Clear", this);
    btnRow->addWidget(m_parseBtn);
    btnRow->addWidget(m_clearBtn);
    btnRow->addStretch();
    manForm->addRow(btnRow);

    root->addWidget(manGroup);

    // ── Frame table ─────────────────────────────────────────────────────────
    m_table = new QTableWidget(0, 8, this);
    m_table->setHorizontalHeaderLabels({
        "Frame ID", "PID", "DLC", "Data (hex)", "Checksum", "CS Type", "PID Valid", "Status"
    });
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setDefaultSectionSize(90);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setFont(QFont("Courier New", 9));
    root->addWidget(m_table, 1);

    // ── Stats bar ───────────────────────────────────────────────────────────
    m_statsLabel = new QLabel("Frames: 0  |  Invalid: 0", this);
    root->addWidget(m_statsLabel);

    // ── Connections ─────────────────────────────────────────────────────────
    connect(m_parseBtn, &QPushButton::clicked, this, &LinWidget::parseManual);
    connect(m_clearBtn, &QPushButton::clicked, this, &LinWidget::clear);
    connect(m_hexInput, &QLineEdit::returnPressed, this, &LinWidget::parseManual);
}

void LinWidget::feedBytes(const QByteArray &raw) {
    std::vector<uint8_t> buf(reinterpret_cast<const uint8_t*>(raw.constData()),
                              reinterpret_cast<const uint8_t*>(raw.constData()) + raw.size());
    auto csType = static_cast<LinFrame::ChecksumType>(m_csTypeCombo->currentData().toInt());
    auto frames = m_parser.feed(buf, 0 /*dlc auto*/, csType);
    for (auto &f : frames) appendFrame(f);
}

void LinWidget::parseManual() {
    QString text = m_hexInput->text().simplified();
    if (text.isEmpty()) return;

    std::vector<uint8_t> buf;
    for (auto &tok : text.split(' ', Qt::SkipEmptyParts)) {
        bool ok = false;
        uint val = tok.toUInt(&ok, 16);
        if (!ok || val > 255) {
            QMessageBox::warning(this, "LIN Parser", QString("Invalid hex byte: '%1'").arg(tok));
            return;
        }
        buf.push_back(static_cast<uint8_t>(val));
    }

    auto csType = static_cast<LinFrame::ChecksumType>(m_csTypeCombo->currentData().toInt());
    LinFrame f = LinParser::parse(buf, csType);
    appendFrame(f);
    emit frameDecoded(f);
}

void LinWidget::clear() {
    m_table->setRowCount(0);
    m_parser.reset();
    m_totalFrames  = 0;
    m_invalidCount = 0;
    m_statsLabel->setText("Frames: 0  |  Invalid: 0");
}

void LinWidget::appendFrame(const LinFrame &f) {
    if (m_table->rowCount() >= kMaxRows)
        m_table->removeRow(0);

    ++m_totalFrames;
    if (!f.checksumValid) ++m_invalidCount;

    int row = m_table->rowCount();
    m_table->insertRow(row);

    auto cell = [&](int col, const QString &txt) {
        auto *item = new QTableWidgetItem(txt);
        item->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, col, item);
    };

    char buf[8];
    snprintf(buf, sizeof(buf), "0x%02X", f.frameId);
    cell(0, buf);
    snprintf(buf, sizeof(buf), "0x%02X", f.pid);
    cell(1, buf);
    cell(2, QString::number(f.dlc));
    cell(3, dataHex(f));
    snprintf(buf, sizeof(buf), "0x%02X", f.checksum);
    cell(4, buf);
    cell(5, f.csType == LinFrame::ChecksumType::Enhanced ? "Enhanced" : "Classic");
    cell(6, LinFrame::pidValid(f.pid) ? "OK" : "FAIL");

    auto *statusItem = new QTableWidgetItem(f.checksumValid ? "OK" : "FAIL");
    statusItem->setTextAlignment(Qt::AlignCenter);
    statusItem->setForeground(f.checksumValid ? QColor(Qt::darkGreen) : QColor(Qt::red));
    QFont bold; bold.setBold(true);
    if (!f.checksumValid) statusItem->setFont(bold);
    m_table->setItem(row, 7, statusItem);

    m_table->scrollToBottom();
    m_statsLabel->setText(QString("Frames: %1  |  Invalid: %2").arg(m_totalFrames).arg(m_invalidCount));
}

QString LinWidget::dataHex(const LinFrame &f) const {
    QString s;
    for (int i = 0; i < f.dlc; ++i) {
        if (i) s += ' ';
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X", f.data[i]);
        s += buf;
    }
    return s;
}
