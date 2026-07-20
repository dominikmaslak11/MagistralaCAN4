#include "UdsWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QGroupBox>
#include <QDateTime>
#include <algorithm>

// ── UdsTableModel — timestamp naprawiony: µs → sekundy ────────────────────────

UdsTableModel::UdsTableModel(QObject *p) : QAbstractTableModel(p) {}

QVariant UdsTableModel::data(const QModelIndex &idx, int role) const {
    if (!idx.isValid() || idx.row() >= m_frames.size()) return {};
    const auto &f = m_frames.at(idx.row());

    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
        case TIME:    return QString::number(static_cast<double>(f.timestamp) / 1'000'000.0, 'f', 3);
        case CAN_ID:  return QString("0x%1").arg(f.canId, 3, 16, QChar('0')).toUpper();
        case DIR:
            switch (f.type) {
            case UdsFrame::Request:          return "REQ";
            case UdsFrame::PositiveResponse: return "POS";
            case UdsFrame::NegativeResponse: return "NEG";
            case UdsFrame::FlowControl:      return "FC";
            default: return "?";
            }
        case SID:     return QString("0x%1").arg(f.sid, 2, 16, QChar('0'));
        case SVC_NAME: return m_parser ? m_parser->serviceName(f.sid) : QString();
        case DID:     return f.did ? QString("0x%1").arg(f.did, 4, 16, QChar('0')) : "";
        case SUBFUNC: {
            if (f.type != UdsFrame::Request || !f.subFunc) return "";
            return m_parser ? UdsParser::subFunctionName(f.sid, f.subFunc)
                            : QString("0x%1").arg(f.subFunc, 2, 16, QChar('0'));
        }
        case STATUS:
            if (f.type == UdsFrame::NegativeResponse)
                return UdsParser::nrcName(f.nrc);
            if (f.type == UdsFrame::PositiveResponse) return "OK";
            return "";
        case DATA: {
            QString s;
            int show = std::min(f.dlc, (uint8_t)16);
            s.reserve(show * 3 + 8);
            if (f.multiFrame) s = "[MF] ";
            for (int i = 0; i < show; ++i)
                s += QString("%1 ").arg(f.data[i], 2, 16, QChar('0')).toUpper();
            if (f.dlc > 16) s += QString("(+%1 B)").arg(f.dlc - 16);
            return s.trimmed();
        }
        default: return {};
        }
    }

    if (role == Qt::ForegroundRole) {
        switch (f.type) {
        case UdsFrame::Request:          return QColor("#00ffaa");
        case UdsFrame::PositiveResponse: return QColor("#ffaa00");
        case UdsFrame::NegativeResponse: return QColor("#ff4444");
        default: return QColor("#c0c0c0");
        }
    }

    return {};
}

QVariant UdsTableModel::headerData(int s, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (s) {
    case TIME:    return "Czas (s)";
    case CAN_ID:  return "CAN ID";
    case DIR:     return "Kier.";
    case SID:     return "SID";
    case SVC_NAME: return "Serwis UDS";
    case DID:     return "DID";
    case SUBFUNC: return "Sub-funkcja";
    case STATUS:  return "Status";
    case DATA:    return "Dane";
    default: return {};
    }
}

void UdsTableModel::addFrame(const UdsFrame &f) {
    beginInsertRows(QModelIndex(), static_cast<int>(m_frames.size()), static_cast<int>(m_frames.size()));
    m_frames.append(f);
    if (m_frames.size() > MAX) m_frames.pop_front();
    endInsertRows();
}

void UdsTableModel::clear() { beginResetModel(); m_frames.clear(); endResetModel(); }

UdsFrame UdsTableModel::frameAt(int row) const {
    return (row >= 0 && row < m_frames.size()) ? m_frames.at(row) : UdsFrame{};
}

// ═══════════════════════════════════════════════════════════════
// UdsWidget
// ═══════════════════════════════════════════════════════════════

UdsWidget::UdsWidget(QWidget *parent) : QWidget(parent) {
    // Podpinamy callback ISO-TP reassembly
    m_tpLayer.setMessageCallback([this](const CanTpMessage &msg) {
        onTpMessage(msg);
    });

    // Co 1s usuwamy przeterminowane sesje ISO-TP
    m_purgeTimer.setInterval(1000);
    connect(&m_purgeTimer, &QTimer::timeout, this, &UdsWidget::purgeTimedOutSessions);
    m_purgeTimer.start();

    setupUi();
}

void UdsWidget::setupUi() {
    auto *lay = new QVBoxLayout(this);

    // Toolbar z filtrem SID
    auto *tool = new QHBoxLayout;
    m_filterEnabled = new QCheckBox("Filtruj SID:");
    m_filterEnabled->setStyleSheet("color: #ff66cc; font-weight: bold;");
    tool->addWidget(m_filterEnabled);
    m_sidFilter = new QComboBox; m_sidFilter->setMinimumWidth(280);
    m_sidFilter->addItem("-- wszystkie --", 0xFFFF);
    for (uint8_t s : m_parser.knownSids())
        m_sidFilter->addItem(QString("0x%1 – %2").arg(s, 2, 16, QChar('0')).toUpper().arg(m_parser.serviceName(s)), s);
    m_sidFilter->setEditable(true);
    tool->addWidget(m_sidFilter);
    m_clearBtn = new QPushButton("Wyczyść"); tool->addWidget(m_clearBtn);
    tool->addStretch();
    m_statusLabel = new QLabel("Ramki UDS: 0");
    m_statusLabel->setStyleSheet("color: #00ffaa; font-weight: bold;");
    tool->addWidget(m_statusLabel);
    lay->addLayout(tool);

    // Tabela
    m_model = new UdsTableModel(this); m_model->setParser(&m_parser);
    m_table = new QTableView; m_table->setModel(m_model);
    m_table->verticalHeader()->hide(); m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setShowGrid(false); m_table->setAlternatingRowColors(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setMinimumHeight(300);
    lay->addWidget(m_table, 1);

    // Panel szczegółów
    auto *grp = new QGroupBox("Szczegóły ramki UDS");
    grp->setStyleSheet("QGroupBox { color: #ff66cc; font-weight: bold; border: 1px solid #e94560; border-radius: 4px; margin-top: 8px; padding-top: 16px; }");
    auto *glay = new QVBoxLayout(grp);
    m_detailLabel = new QLabel("Kliknij ramkę UDS aby zobaczyć szczegóły.");
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setStyleSheet("color: #c0c0c0; font-family: Consolas, monospace; font-size: 13px; background-color: #1a1a2e; padding: 10px; border-radius: 4px;");
    m_detailLabel->setMinimumHeight(100);
    glay->addWidget(m_detailLabel);
    lay->addWidget(grp);

    // Połączenia
    connect(m_table, &QTableView::clicked, this, &UdsWidget::onRowSelected);
    connect(m_clearBtn, &QPushButton::clicked, this, [this]() { m_model->clear(); m_statusLabel->setText("Ramki UDS: 0"); });

    setStyleSheet(R"(
        QPushButton { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560; border-radius: 4px; padding: 5px 15px; font-weight: bold; }
        QPushButton:hover { background: #e94560; color: #0a0e17; }
        QComboBox { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560; border-radius: 4px; padding: 3px 8px; }
        QComboBox QAbstractItemView { background: #1a1a2e; color: #00ffaa; selection-background-color: #e94560; }
        QTableView { background-color: #0a0e17; color: #c0c0c0; gridline-color: #2a2a3c; selection-background-color: #e94560; selection-color: #fff; font-family: Consolas, monospace; font-size: 11px; }
        QHeaderView::section { background-color: #1a1a2e; color: #ff66cc; font-weight: bold; padding: 4px; border: none; border-bottom: 2px solid #e94560; }
        QLabel { color: #c0c0c0; }
    )");
}

void UdsWidget::processFrame(const CanFrame &frame) {
    if (!UdsFrame::looksLikeUds(frame)) return;
    // Każda ramka wędruje przez ISO-TP — callback onTpMessage wywoła się
    // gdy payload będzie kompletny (SF od razu, multi-frame po ostatnim CF)
    m_tpLayer.processFrame(frame);
}

void UdsWidget::onTpMessage(const CanTpMessage &msg) {
    if (!msg.complete || msg.payload.empty()) return;

    bool multi = msg.payload.size() > 7;  // SF ≤ 7 bajtów
    UdsFrame uf = UdsFrame::fromPayload(
        msg.payload.data(),
        static_cast<int>(msg.payload.size()),
        msg.canId,
        msg.timestamp,
        multi
    );

    if (uf.type == UdsFrame::Unknown) return;

    // Filtr SID
    if (m_filterEnabled->isChecked()) {
        QVariant fdata = m_sidFilter->currentData();
        if (fdata.isValid() && fdata.toUInt() != 0xFFFF) {
            if (uf.sid != fdata.toUInt()) return;
        }
    }

    m_model->addFrame(uf);
    m_statusLabel->setText(QString("Ramki UDS: %1").arg(m_model->rowCount()));
}

void UdsWidget::purgeTimedOutSessions() {
    uint64_t nowUs = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch()) * 1000ULL;
    m_tpLayer.purgeTimedOut(nowUs, 2'000'000ULL);  // 2s timeout na sesję
}

void UdsWidget::onRowSelected(const QModelIndex &idx) {
    if (!idx.isValid()) return;
    UdsFrame f = m_model->frameAt(idx.row());

    QStringList lines;
    lines.append(QString("<b style='color:#ff66cc;'>%1</b>").arg(f.toString()));
    lines.append(QString("<span style='color:#00ffaa;'>Serwis:</span> %1").arg(m_parser.serviceName(f.sid)));

    if (f.type == UdsFrame::Request) {
        if (f.subFunc) lines.append(QString("<span style='color:#00ffaa;'>Sub-funkcja:</span> %1")
                                    .arg(UdsParser::subFunctionName(f.sid, f.subFunc)));
        if (f.did) lines.append(QString("<span style='color:#00ffaa;'>DID:</span> %1 (0x%2)")
                                .arg(m_parser.didName(f.did)).arg(f.did, 4, 16, QChar('0')));
    }

    if (f.type == UdsFrame::PositiveResponse) {
        if (f.did) lines.append(QString("<span style='color:#00ffaa;'>DID:</span> %1").arg(m_parser.didName(f.did)));
        QString dec = UdsParser::decodeResponseData(f.sid, f.data.data(), f.dlc);
        lines.append(QString("<span style='color:#ffaa00;'>Dane:</span> %1").arg(dec));
        if (f.multiFrame) {
            // Pokaż pełny payload hex dla multi-frame
            QString hex;
            for (int i = 0; i < f.dlc; ++i)
                hex += QString("%1 ").arg(f.data[i], 2, 16, QChar('0')).toUpper();
            lines.append(QString("<span style='color:#66aaff;'>Pełny payload (%1 B):</span><br><tt>%2</tt>")
                .arg(f.dlc).arg(hex.trimmed()));
        }
    }

    if (f.type == UdsFrame::NegativeResponse) {
        lines.append(QString("<span style='color:#ff4444;'>Błąd NRC 0x%1:</span> %2")
                     .arg(f.nrc, 2, 16, QChar('0')).arg(UdsParser::nrcName(f.nrc)));
    }

    m_detailLabel->setText(lines.join("<br>"));
}
