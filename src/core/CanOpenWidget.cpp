#include "CanOpenWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>

CanOpenTableModel::CanOpenTableModel(QObject *p) : QAbstractTableModel(p) {}

QVariant CanOpenTableModel::data(const QModelIndex &idx, int role) const {
    if (!idx.isValid() || idx.row() >= m_frames.size()) return {};
    const auto &f = m_frames.at(idx.row());
    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
        case TIME:      return QString::number(f.timestamp / 1000.0, 'f', 3);
        case CAN_ID:    return QString("0x%1").arg(f.canId, 3, 16, QChar('0')).toUpper();
        case NODE:      return f.nodeId ? QString::number(f.nodeId) : "ALL";
        case TYPE:      return QString::number(f.funcCode, 16).toUpper();
        case TYPE_NAME: return m_parser ? m_parser->typeName(f.type) : "?";
        case DETAIL: {
            switch (f.type) {
            case CanOpenFrame::NMT:  return m_parser->nmtCommand(f.data[1]);
            case CanOpenFrame::EMCY: { uint16_t ec = (f.data[0]<<8)|f.data[1]; return m_parser->emcyCode(ec); }
            case CanOpenFrame::SDO_TX: case CanOpenFrame::SDO_RX: return m_parser->sdoCommand(f.data[0]);
            case CanOpenFrame::HEARTBEAT: return m_parser->heartbeatState(f.data[0]);
            default: return "";
            }
        }
        case DATA: {
            QString s; for (int i = 0; i < f.dlc && i < 8; ++i) s += QString("%1 ").arg(f.data[i], 2, 16, QChar('0')).toUpper();
            return s.trimmed();
        }
        }
    }
    if (role == Qt::ForegroundRole) {
        if (f.type == CanOpenFrame::EMCY || f.type == CanOpenFrame::NMT) return QColor("#ff4444");
        if (f.type == CanOpenFrame::HEARTBEAT) return QColor("#00ffaa");
        return QColor("#ffaa00");
    }
    return {};
}

QVariant CanOpenTableModel::headerData(int s, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (s) {
    case TIME: return "Czas (s)"; case CAN_ID: return "CAN ID"; case NODE: return "Węzeł";
    case TYPE: return "Funkcja"; case TYPE_NAME: return "Typ"; case DETAIL: return "Szczegóły";
    case DATA: return "Dane"; default: return {};
    }
}

void CanOpenTableModel::addFrame(const CanOpenFrame &f) {
    beginInsertRows(QModelIndex(), m_frames.size(), m_frames.size());
    m_frames.append(f); if (m_frames.size() > MAX) m_frames.pop_front(); endInsertRows();
}
void CanOpenTableModel::clear() { beginResetModel(); m_frames.clear(); endResetModel(); }

CanOpenWidget::CanOpenWidget(QWidget *parent) : QWidget(parent) { setupUi(); }

void CanOpenWidget::setupUi() {
    auto *lay = new QVBoxLayout(this);
    auto *tool = new QHBoxLayout;
    m_clearBtn = new QPushButton("Wyczyść"); tool->addWidget(m_clearBtn); tool->addStretch();
    m_statusLabel = new QLabel("Ramki CANopen: 0"); m_statusLabel->setStyleSheet("color: #00ffaa; font-weight: bold;"); tool->addWidget(m_statusLabel);
    lay->addLayout(tool);
    m_model = new CanOpenTableModel(this); m_model->setParser(&m_parser);
    m_table = new QTableView; m_table->setModel(m_model);
    m_table->verticalHeader()->hide(); m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setShowGrid(false); lay->addWidget(m_table, 1);
    connect(m_clearBtn, &QPushButton::clicked, [this]() { m_model->clear(); m_statusLabel->setText("Ramki CANopen: 0"); });
    // Global QSS (style_dark/light.qss) handles widget styling
}

void CanOpenWidget::processFrame(const CanFrame &frame) {
    if (!CanOpenFrame::isCanOpen(frame)) return;
    CanOpenFrame cf = CanOpenFrame::fromCanFrame(frame);
    m_model->addFrame(cf);
    m_statusLabel->setText(QString("Ramki CANopen: %1").arg(m_model->rowCount()));
}
