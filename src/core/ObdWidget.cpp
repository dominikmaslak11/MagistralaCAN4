#include "ObdWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>

ObdTableModel::ObdTableModel(QObject *p) : QAbstractTableModel(p) {}

QVariant ObdTableModel::data(const QModelIndex &idx, int role) const {
    if (!idx.isValid() || idx.row() >= m_frames.size()) return {};
    const auto &f = m_frames.at(idx.row());
    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
        case TIME:      return QString::number(f.timestamp / 1000.0, 'f', 3);
        case CAN_ID:    return QString("0x%1").arg(f.canId, 3, 16, QChar('0')).toUpper();
        case DIR:       return f.type == ObdFrame::Request ? "REQ" : "RESP";
        case MODE:      return QString("0x%1").arg(f.mode, 2, 16, QChar('0'));
        case MODE_NAME: return m_parser ? m_parser->modeName(f.mode) : QString();
        case PID:       return QString("0x%1").arg(f.pid, 4, 16, QChar('0'));
        case PID_NAME:  return m_parser ? m_parser->pidName(f.mode, f.pid) : QString();
        case VALUE: {
            if (f.type == ObdFrame::Response && m_parser) {
                double v = m_parser->decodePidValue(f.mode, f.pid, f.data.data(), f.dlc);
                if (v != 0) return QString::number(v, 'f', 1) + " " + m_parser->pidUnit(f.mode, f.pid);
            }
            return "";
        }
        }
    }
    if (role == Qt::ForegroundRole) {
        if (f.type == ObdFrame::Response) return QColor("#ffaa00");
        return QColor("#00ffaa");
    }
    return {};
}

QVariant ObdTableModel::headerData(int s, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (s) {
    case TIME: return "Czas (s)"; case CAN_ID: return "CAN ID"; case DIR: return "Kier.";
    case MODE: return "Mode"; case MODE_NAME: return "Tryb"; case PID: return "PID";
    case PID_NAME: return "Parametr"; case VALUE: return "Wartosc";
    default: return {};
    }
}

void ObdTableModel::addFrame(const ObdFrame &f) {
    beginInsertRows(QModelIndex(), m_frames.size(), m_frames.size());
    m_frames.append(f);
    if (m_frames.size() > MAX) m_frames.pop_front();
    endInsertRows();
}
void ObdTableModel::clear() { beginResetModel(); m_frames.clear(); endResetModel(); }

ObdWidget::ObdWidget(QWidget *parent) : QWidget(parent) { setupUi(); }

void ObdWidget::setupUi() {
    auto *lay = new QVBoxLayout(this);
    auto *tool = new QHBoxLayout;
    m_clearBtn = new QPushButton("Wyczyść"); tool->addWidget(m_clearBtn);
    tool->addStretch();
    m_statusLabel = new QLabel("Ramki OBD-II: 0");
    m_statusLabel->setStyleSheet("color: #00ffaa; font-weight: bold;"); tool->addWidget(m_statusLabel);
    lay->addLayout(tool);

    m_model = new ObdTableModel(this); m_model->setParser(&m_parser);
    m_table = new QTableView; m_table->setModel(m_model);
    m_table->verticalHeader()->hide(); m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setShowGrid(false);
    lay->addWidget(m_table, 1);

    connect(m_clearBtn, &QPushButton::clicked, [this]() { m_model->clear(); m_statusLabel->setText("Ramki OBD-II: 0"); });

    setStyleSheet(R"(
        QPushButton { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560; border-radius: 4px; padding: 5px 12px; font-weight: bold; }
        QPushButton:hover { background: #e94560; color: #0a0e17; }
        QTableView { background-color: #0a0e17; color: #c0c0c0; gridline-color: #2a2a3c; font-family: Consolas, monospace; font-size: 11px; }
        QHeaderView::section { background-color: #1a1a2e; color: #ff66cc; font-weight: bold; padding: 4px; border: none; border-bottom: 2px solid #e94560; }
        QLabel { color: #c0c0c0; }
    )");
}

void ObdWidget::processFrame(const CanFrame &frame) {
    if (!ObdFrame::isObd(frame)) return;
    ObdFrame of = ObdFrame::fromCanFrame(frame);
    m_model->addFrame(of);
    m_statusLabel->setText(QString("Ramki OBD-II: %1").arg(m_model->rowCount()));
}
