#include "Kwp2000Widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QGroupBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QFont>

Kwp2000Widget::Kwp2000Widget(QWidget *parent) : QWidget(parent) {
    buildUi();
}

void Kwp2000Widget::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    auto *grp = new QGroupBox("Manual KWP2000 Frame Decode", this);
    auto *form = new QFormLayout(grp);

    m_hexInput = new QLineEdit(this);
    m_hexInput->setPlaceholderText("e.g. 10 01  (hex bytes: SID + params, no framing)");
    m_hexInput->setFont(QFont("Courier New", 10));
    form->addRow("Bytes (hex):", m_hexInput);

    auto *btnRow = new QHBoxLayout;
    m_parseBtn = new QPushButton("Decode", this);
    m_clearBtn = new QPushButton("Clear", this);
    btnRow->addWidget(m_parseBtn);
    btnRow->addWidget(m_clearBtn);
    btnRow->addStretch();
    form->addRow(btnRow);
    root->addWidget(grp);

    m_table = new QTableWidget(0, 6, this);
    m_table->setHorizontalHeaderLabels({"Direction", "SID", "Service Name", "NRC / Data", "NRC Description", "Raw"});
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->horizontalHeader()->setDefaultSectionSize(80);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setFont(QFont("Courier New", 9));
    root->addWidget(m_table, 1);

    m_statsLabel = new QLabel("Frames: 0", this);
    root->addWidget(m_statsLabel);

    connect(m_parseBtn, &QPushButton::clicked, this, &Kwp2000Widget::parseManual);
    connect(m_clearBtn, &QPushButton::clicked, this, &Kwp2000Widget::clear);
    connect(m_hexInput, &QLineEdit::returnPressed, this, &Kwp2000Widget::parseManual);
}

void Kwp2000Widget::parseManual() {
    QString text = m_hexInput->text().simplified();
    if (text.isEmpty()) return;

    std::vector<uint8_t> buf;
    for (auto &tok : text.split(' ', Qt::SkipEmptyParts)) {
        bool ok = false;
        uint val = tok.toUInt(&ok, 16);
        if (!ok || val > 255) {
            QMessageBox::warning(this, "KWP2000", QString("Invalid hex byte: '%1'").arg(tok));
            return;
        }
        buf.push_back(static_cast<uint8_t>(val));
    }

    Kwp2000Frame f = Kwp2000Parser::parse(buf);
    appendFrame(f, text);
}

void Kwp2000Widget::clear() {
    m_table->setRowCount(0);
    m_total = m_errors = 0;
    m_statsLabel->setText("Frames: 0");
}

void Kwp2000Widget::appendFrame(const Kwp2000Frame &f, const QString &raw) {
    ++m_total;
    if (f.isNegative) ++m_errors;

    int row = m_table->rowCount();
    m_table->insertRow(row);

    auto cell = [&](int col, const QString &txt, QColor bg = QColor()) {
        auto *item = new QTableWidgetItem(txt);
        item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        if (bg.isValid()) item->setBackground(bg);
        m_table->setItem(row, col, item);
    };

    // Direction
    QString dir = f.isResponse ? (f.isPositive ? "Response +" : "Response −") : "Request";
    QColor dirColor = f.isNegative ? QColor(0xFF, 0xCC, 0xCC)
                    : f.isPositive ? QColor(0xCC, 0xFF, 0xCC)
                    : QColor();
    cell(0, dir, dirColor);

    // SID
    char sidBuf[8];
    snprintf(sidBuf, sizeof(sidBuf), "0x%02X", f.sid);
    cell(1, sidBuf, dirColor);

    // Service name
    cell(2, QString::fromStdString(Kwp2000Parser::serviceName(f.sid)), dirColor);

    // NRC / data
    if (f.isNegative) {
        char nrcBuf[8];
        snprintf(nrcBuf, sizeof(nrcBuf), "0x%02X", f.nrc);
        cell(3, nrcBuf, dirColor);
        cell(4, QString::fromStdString(Kwp2000Parser::nrcDescription(f.nrc)), dirColor);
    } else {
        cell(3, payloadHex(f.payload), dirColor);
        cell(4, "-", dirColor);
    }

    // Raw
    cell(5, raw);

    m_table->scrollToBottom();
    m_statsLabel->setText(QString("Frames: %1  |  Errors: %2").arg(m_total).arg(m_errors));
}

QString Kwp2000Widget::payloadHex(const std::vector<uint8_t> &v) const {
    QString s;
    char buf[4];
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ' ';
        snprintf(buf, sizeof(buf), "%02X", v[i]);
        s += buf;
    }
    return s;
}
