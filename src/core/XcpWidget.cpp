#include "XcpWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFormLayout>
#include <QMessageBox>
#include <QFont>

XcpWidget::XcpWidget(QWidget *parent) : QWidget(parent) {
    buildUi();
}

void XcpWidget::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    auto *grp  = new QGroupBox("Manual XCP Packet Decode", this);
    auto *form = new QFormLayout(grp);

    m_hexInput = new QLineEdit(this);
    m_hexInput->setPlaceholderText("e.g. FF 00  (PID + params, hex bytes)");
    m_hexInput->setFont(QFont("Courier New", 10));
    form->addRow("Bytes (hex):", m_hexInput);

    m_dirCombo = new QComboBox(this);
    m_dirCombo->addItem("Auto (heuristic)",  0);
    m_dirCombo->addItem("Command channel (master→slave)", 1);
    m_dirCombo->addItem("Response channel (slave→master)", 2);
    form->addRow("Channel:", m_dirCombo);

    auto *btnRow = new QHBoxLayout;
    m_parseBtn = new QPushButton("Decode", this);
    m_clearBtn = new QPushButton("Clear", this);
    btnRow->addWidget(m_parseBtn);
    btnRow->addWidget(m_clearBtn);
    btnRow->addStretch();
    form->addRow(btnRow);
    root->addWidget(grp);

    m_table = new QTableWidget(0, 6, this);
    m_table->setHorizontalHeaderLabels({"Type", "PID", "Command / Response", "Error Code", "Error Description", "Payload"});
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->horizontalHeader()->setDefaultSectionSize(80);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setFont(QFont("Courier New", 9));
    root->addWidget(m_table, 1);

    m_statsLabel = new QLabel("Packets: 0", this);
    root->addWidget(m_statsLabel);

    connect(m_parseBtn, &QPushButton::clicked, this, &XcpWidget::parseManual);
    connect(m_clearBtn, &QPushButton::clicked, this, &XcpWidget::clear);
    connect(m_hexInput, &QLineEdit::returnPressed, this, &XcpWidget::parseManual);
}

void XcpWidget::parseManual() {
    QString text = m_hexInput->text().simplified();
    if (text.isEmpty()) return;

    std::vector<uint8_t> buf;
    for (auto &tok : text.split(' ', Qt::SkipEmptyParts)) {
        bool ok = false;
        uint val = tok.toUInt(&ok, 16);
        if (!ok || val > 255) {
            QMessageBox::warning(this, "XCP Parser", QString("Invalid hex byte: '%1'").arg(tok));
            return;
        }
        buf.push_back(static_cast<uint8_t>(val));
    }

    XcpFrame f = XcpParser::parse(buf);
    appendFrame(f, text);
}

void XcpWidget::clear() {
    m_table->setRowCount(0);
    m_total = m_negCount = 0;
    m_statsLabel->setText("Packets: 0");
}

void XcpWidget::appendFrame(const XcpFrame &f, const QString &raw) {
    ++m_total;
    if (f.isNegative) ++m_negCount;

    int row = m_table->rowCount();
    m_table->insertRow(row);

    QColor bg;
    if (f.isNegative)  bg = QColor(0xFF, 0xCC, 0xCC);
    else if (f.isPositive) bg = QColor(0xCC, 0xFF, 0xCC);
    else if (f.isEvent)    bg = QColor(0xFF, 0xFF, 0xCC);

    auto cell = [&](int col, const QString &txt) {
        auto *item = new QTableWidgetItem(txt);
        item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        if (bg.isValid()) item->setBackground(bg);
        m_table->setItem(row, col, item);
    };

    QString typeStr;
    switch (f.dir) {
        case XcpFrame::Direction::Command:  typeStr = "CMD";    break;
        case XcpFrame::Direction::Response: typeStr = f.isPositive ? "RSP+" : "RSP−"; break;
        case XcpFrame::Direction::Event:    typeStr = "EVT";    break;
        case XcpFrame::Direction::Service:  typeStr = "SRV";    break;
    }

    char pidBuf[8];
    snprintf(pidBuf, sizeof(pidBuf), "0x%02X", f.pid);

    cell(0, typeStr);
    cell(1, pidBuf);
    cell(2, QString::fromStdString(XcpParser::commandName(f.pid)));

    if (f.isNegative) {
        char nBuf[8];
        snprintf(nBuf, sizeof(nBuf), "0x%02X", f.errorCode);
        cell(3, nBuf);
        cell(4, QString::fromStdString(XcpParser::errorDescription(f.errorCode)));
    } else {
        cell(3, "-");
        cell(4, "-");
    }

    cell(5, payloadHex(f.payload));

    m_table->scrollToBottom();
    m_statsLabel->setText(QString("Packets: %1  |  Negative: %2").arg(m_total).arg(m_negCount));
}

QString XcpWidget::payloadHex(const std::vector<uint8_t> &v) const {
    QString s;
    char buf[4];
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ' ';
        snprintf(buf, sizeof(buf), "%02X", v[i]);
        s += buf;
    }
    return s;
}
