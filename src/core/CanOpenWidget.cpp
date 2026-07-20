#include "CanOpenWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTabWidget>
#include <QDateTime>

// ── CanOpenTableModel ─────────────────────────────────────────────────────────

CanOpenTableModel::CanOpenTableModel(QObject *p) : QAbstractTableModel(p) {}

QVariant CanOpenTableModel::data(const QModelIndex &idx, int role) const {
    if (!idx.isValid() || idx.row() >= m_frames.size()) return {};
    const auto &f = m_frames.at(idx.row());

    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
        case TIME:      return QString::number(static_cast<double>(f.timestamp) / 1'000'000.0, 'f', 3);
        case CAN_ID:    return QString("0x%1").arg(f.canId, 3, 16, QChar('0')).toUpper();
        case NODE:      return f.nodeId ? QString::number(f.nodeId) : "ALL";
        case TYPE:      return QString("0x%1").arg(f.funcCode, 1, 16).toUpper();
        case TYPE_NAME: return m_parser ? m_parser->typeName(f.type) : "?";
        case DETAIL: {
            if (!m_parser) return "";
            switch (f.type) {
            case CanOpenFrame::NMT:
                return m_parser->nmtCommand(f.dlc >= 2 ? f.data[1] : 0);
            case CanOpenFrame::EMCY: {
                uint16_t ec = f.dlc >= 2 ? (static_cast<uint16_t>(f.data[1]) << 8) | f.data[0] : 0;
                return m_parser->emcyCode(ec);
            }
            case CanOpenFrame::SDO_TX:
            case CanOpenFrame::SDO_RX: {
                if (f.dlc < 3) return m_parser->sdoCommand(f.dlc > 0 ? f.data[0] : 0);
                uint16_t idx2 = f.dlc >= 3 ?
                    (static_cast<uint16_t>(f.data[2]) << 8) | f.data[1] : 0;
                uint8_t  sub  = f.dlc >= 4 ? f.data[3] : 0;
                return QString("%1 idx=0x%2 sub=%3")
                    .arg(m_parser->sdoCommand(f.data[0]))
                    .arg(idx2, 4, 16, QChar('0')).toUpper()
                    .arg(sub);
            }
            case CanOpenFrame::HEARTBEAT:
                return m_parser->heartbeatState(f.dlc > 0 ? f.data[0] : 0xFF);
            default: return "";
            }
        }
        case DATA: {
            QString s;
            for (int i = 0; i < f.dlc && i < 8; ++i)
                s += QString("%1 ").arg(f.data[i], 2, 16, QChar('0')).toUpper();
            return s.trimmed();
        }
        }
    }

    if (role == Qt::ForegroundRole) {
        switch (f.type) {
        case CanOpenFrame::EMCY:      return QColor("#ff4444");
        case CanOpenFrame::NMT:       return QColor("#ff8800");
        case CanOpenFrame::HEARTBEAT: return QColor("#00ffaa");
        case CanOpenFrame::SDO_TX:
        case CanOpenFrame::SDO_RX:    return QColor("#66aaff");
        default:                      return QColor("#ffaa00");
        }
    }
    return {};
}

QVariant CanOpenTableModel::headerData(int s, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (s) {
    case TIME:      return "Czas (s)";
    case CAN_ID:    return "CAN ID";
    case NODE:      return "Węzeł";
    case TYPE:      return "FuncCode";
    case TYPE_NAME: return "Typ";
    case DETAIL:    return "Szczegóły";
    case DATA:      return "Dane";
    default:        return {};
    }
}

void CanOpenTableModel::addFrame(const CanOpenFrame &f) {
    if (m_filterNode >= 0 && f.nodeId != static_cast<uint8_t>(m_filterNode)) return;
    beginInsertRows({}, static_cast<int>(m_frames.size()), static_cast<int>(m_frames.size()));
    m_frames.append(f);
    if (m_frames.size() > MAX) m_frames.removeFirst();
    endInsertRows();
}

void CanOpenTableModel::clear() {
    beginResetModel();
    m_frames.clear();
    endResetModel();
}

// ── CanOpenWidget ─────────────────────────────────────────────────────────────

CanOpenWidget::CanOpenWidget(QWidget *parent) : QWidget(parent) {
    setupUi();
}

void CanOpenWidget::setupUi() {
    auto *main = new QVBoxLayout(this);

    // ── Toolbar ──
    auto *tool = new QHBoxLayout;
    m_clearBtn = new QPushButton("Wyczyść");
    tool->addWidget(m_clearBtn);

    tool->addWidget(new QLabel("Węzeł:"));
    m_nodeFilter = new QLineEdit;
    m_nodeFilter->setPlaceholderText("np. 5 (puste=wszystkie)");
    m_nodeFilter->setMaximumWidth(160);
    tool->addWidget(m_nodeFilter);

    tool->addStretch();
    m_statusLabel = new QLabel("Ramki CANopen: 0");
    m_statusLabel->setStyleSheet("color: #00ffaa; font-weight: bold;");
    tool->addWidget(m_statusLabel);
    main->addLayout(tool);

    // ── Zakładki ──
    auto *tabs = new QTabWidget;

    // Zakładka 1: tabela ramek
    m_model = new CanOpenTableModel(this);
    m_model->setParser(&m_parser);
    m_table = new QTableView;
    m_table->setModel(m_model);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    tabs->addTab(m_table, "Ramki CANopen");

    // Zakładka 2: heartbeat monitor
    m_nodeTable = new QTableWidget(0, 5);
    m_nodeTable->setHorizontalHeaderLabels({"Węzeł", "Stan", "Ostatnio widziany (s)", "HB count", "Timeout?"});
    m_nodeTable->verticalHeader()->hide();
    m_nodeTable->horizontalHeader()->setStretchLastSection(true);
    m_nodeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_nodeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    tabs->addTab(m_nodeTable, "Stan węzłów");

    main->addWidget(tabs, 1);

    // ── Heartbeat watchdog — co 1s sprawdź timeout ──
    m_hbTimer.setInterval(1000);
    connect(&m_hbTimer, &QTimer::timeout, this, &CanOpenWidget::checkHeartbeatTimeouts);
    m_hbTimer.start();

    connect(m_clearBtn, &QPushButton::clicked, this, [this]() {
        m_model->clear();
        m_nodes.clear();
        m_nodeTable->setRowCount(0);
        m_totalFrames = 0;
        m_statusLabel->setText("Ramki CANopen: 0");
    });
    connect(m_nodeFilter, &QLineEdit::textChanged, this, &CanOpenWidget::onNodeFilterChanged);
}

void CanOpenWidget::onNodeFilterChanged(const QString &text) {
    bool ok;
    int nodeId = text.trimmed().toInt(&ok);
    m_model->setNodeFilter(ok && !text.trimmed().isEmpty() ? nodeId : -1);
    m_model->clear(); // re-filter requires re-feed (historical frames lost, live continues)
}

void CanOpenWidget::processFrame(const CanFrame &frame) {
    if (!CanOpenFrame::isCanOpen(frame)) return;
    CanOpenFrame cf = CanOpenFrame::fromCanFrame(frame);
    m_model->addFrame(cf);
    m_totalFrames++;
    m_statusLabel->setText(QString("Ramki CANopen: %1 | Węzłów: %2")
        .arg(m_totalFrames).arg(m_nodes.size()));

    if (cf.type == CanOpenFrame::HEARTBEAT && cf.nodeId > 0)
        updateNodeState(cf.nodeId, cf.dlc > 0 ? cf.data[0] : 0xFF, cf.timestamp);
}

void CanOpenWidget::updateNodeState(uint8_t nodeId, uint8_t stateCode, uint64_t ts) {
    auto &s = m_nodes[nodeId];
    s.nodeId          = nodeId;
    s.state           = stateCode;
    s.lastSeenUs      = ts;
    s.lastSeenWallMs  = QDateTime::currentMSecsSinceEpoch();
    s.hbCount++;
    s.timedOut        = false;
    refreshNodeTable();
}

void CanOpenWidget::refreshNodeTable() {
    m_nodeTable->setRowCount(0);
    for (const auto &s : m_nodes) {
        int row = m_nodeTable->rowCount();
        m_nodeTable->insertRow(row);
        m_nodeTable->setItem(row, 0, new QTableWidgetItem(QString::number(s.nodeId)));
        m_nodeTable->setItem(row, 1, new QTableWidgetItem(m_parser.heartbeatState(s.state)));
        m_nodeTable->setItem(row, 2, new QTableWidgetItem(
            QString::number(static_cast<double>(s.lastSeenUs) / 1'000'000.0, 'f', 3)));
        m_nodeTable->setItem(row, 3, new QTableWidgetItem(QString::number(s.hbCount)));
        m_nodeTable->setItem(row, 4, new QTableWidgetItem(s.timedOut ? "TAK" : "nie"));

        QColor bg = s.timedOut              ? QColor("#2e0a0a") :
                    (s.state == 0x05)       ? QColor("#0a2e0a") : // Operational
                    (s.state == 0x00)       ? QColor("#1a1a2e") : // Boot-up
                                              QColor("#1e1e0a");
        for (int c = 0; c < 5; ++c)
            if (auto *it = m_nodeTable->item(row, c)) it->setBackground(bg);
    }
}

void CanOpenWidget::checkHeartbeatTimeouts() {
    if (m_nodes.isEmpty()) return;

    int64_t nowMs = QDateTime::currentMSecsSinceEpoch();
    bool anyChanged = false;
    for (auto &s : m_nodes) {
        if (s.lastSeenWallMs > 0) {
            bool shouldTimeout = (nowMs - s.lastSeenWallMs) > (kHeartbeatTimeoutUs / 1000);
            if (shouldTimeout != s.timedOut) {
                s.timedOut = shouldTimeout;
                anyChanged = true;
            }
        }
    }
    if (anyChanged) refreshNodeTable();
}
