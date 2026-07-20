#include "ObdWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QDateTime>
#include <QGroupBox>
#include <cmath>

// ── ObdGauge ──────────────────────────────────────────────────────────────────

ObdGauge::ObdGauge(const QString &name, uint16_t pid,
                   const QString &unit, double min, double max,
                   QWidget *parent)
    : QWidget(parent), m_name(name), m_pid(pid), m_unit(unit), m_min(min), m_max(max)
{
    setMinimumSize(minimumSizeHint());
}

void ObdGauge::setValue(double v) {
    m_value    = v;
    m_hasValue = true;
    update();
}

void ObdGauge::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect wr = rect();

    // Background + border
    p.fillRect(wr, QColor(0x0A, 0x0E, 0x17));
    p.setPen(QPen(QColor(0x2A, 0x2A, 0x3C), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(wr.adjusted(0, 0, -1, -1), 6, 6);

    // Arc bounding rect: square, top portion of widget
    const int pad   = 12;
    const int arcSz = qMin(wr.width() - 2 * pad, wr.height() - 40);
    const QRect arcRect(wr.center().x() - arcSz / 2, pad, arcSz, arcSz);

    // Track arc (background, 225° → -45° clockwise = 270° span)
    QPen trackPen(QColor(0x2A, 0x2A, 0x3C), 9, Qt::SolidLine, Qt::FlatCap);
    p.setPen(trackPen);
    p.drawArc(arcRect, 225 * 16, -(270 * 16));

    // Value arc
    if (m_hasValue) {
        double t = qBound(0.0, (m_value - m_min) / (m_max - m_min), 1.0);
        QColor col = t < 0.65 ? QColor(0x00, 0xFF, 0xAA)
                   : t < 0.85 ? QColor(0xFF, 0xAA, 0x00)
                               : QColor(0xE9, 0x45, 0x60);
        QPen valPen(col, 9, Qt::SolidLine, Qt::FlatCap);
        p.setPen(valPen);
        p.drawArc(arcRect, 225 * 16, static_cast<int>(-t * 270.0 * 16));
    }

    // Centre value text
    QString valStr = m_hasValue ? QString::number(m_value, 'f', (m_max < 10 ? 2 : (m_max < 100 ? 1 : 0))) : "--";
    {
        QFont f = font();
        f.setPointSize(14);
        f.setBold(true);
        p.setFont(f);
        p.setPen(m_hasValue ? Qt::white : QColor(0x44, 0x44, 0x44));
        p.drawText(arcRect, Qt::AlignCenter, valStr);
    }

    // Unit below value
    {
        QFont f = font();
        f.setPointSize(8);
        p.setFont(f);
        p.setPen(QColor(0x88, 0x88, 0x88));
        QRect unitR(wr.left(), arcRect.bottom() - 10, wr.width(), 18);
        p.drawText(unitR, Qt::AlignHCenter | Qt::AlignTop, m_unit);
    }

    // PID name at bottom
    {
        QFont f = font();
        f.setPointSize(8);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(0xFF, 0x66, 0xCC));
        QRect nameR(wr.left() + 2, wr.bottom() - 18, wr.width() - 4, 18);
        p.drawText(nameR, Qt::AlignHCenter | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(m_name, Qt::ElideRight, nameR.width()));
    }
}

// ── ObdTableModel ─────────────────────────────────────────────────────────────

ObdTableModel::ObdTableModel(QObject *p) : QAbstractTableModel(p) {}

QVariant ObdTableModel::data(const QModelIndex &idx, int role) const {
    if (!idx.isValid() || idx.row() >= m_frames.size()) return {};
    const auto &f = m_frames.at(idx.row());
    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
        case TIME:      return QString::number(f.timestamp / 1'000'000.0, 'f', 3);
        case CAN_ID:    return QString("0x%1").arg(f.canId, 3, 16, QChar('0')).toUpper();
        case DIR:       return f.type == ObdFrame::Request ? "REQ" : "RESP";
        case MODE:      return QString("0x%1").arg(f.mode, 2, 16, QChar('0'));
        case MODE_NAME: return m_parser ? m_parser->modeName(f.mode) : QString();
        case PID:       return QString("0x%1").arg(f.pid, 4, 16, QChar('0'));
        case PID_NAME:  return m_parser ? m_parser->pidName(f.mode, f.pid) : QString();
        case VALUE: {
            if (f.type == ObdFrame::Response && m_parser && f.mode == 0x01) {
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
    case PID_NAME: return "Parametr"; case VALUE: return "Wartość";
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

// ── ObdWidget ─────────────────────────────────────────────────────────────────

static const QString kStyle = R"(
    QPushButton { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560;
                  border-radius: 4px; padding: 5px 12px; font-weight: bold; }
    QPushButton:hover { background: #e94560; color: #0a0e17; }
    QTableView, QTableWidget { background-color: #0a0e17; color: #c0c0c0;
                  gridline-color: #2a2a3c; font-family: Consolas, monospace; font-size: 11px; }
    QHeaderView::section { background-color: #1a1a2e; color: #ff66cc; font-weight: bold;
                  padding: 4px; border: none; border-bottom: 2px solid #e94560; }
    QLabel { color: #c0c0c0; }
    QTabWidget::pane { border: 1px solid #2a2a3c; }
    QTabBar::tab { background: #1a1a2e; color: #c0c0c0; padding: 6px 14px; }
    QTabBar::tab:selected { color: #00ffaa; border-bottom: 2px solid #00ffaa; }
)";

ObdWidget::ObdWidget(QWidget *parent) : QWidget(parent) { setupUi(); }

void ObdWidget::setupUi() {
    auto *lay  = new QVBoxLayout(this);
    auto *tool = new QHBoxLayout;

    m_clearBtn = new QPushButton("Wyczyść");
    tool->addWidget(m_clearBtn);
    tool->addStretch();
    m_statusLabel = new QLabel("Ramki OBD-II: 0");
    m_statusLabel->setStyleSheet("color: #00ffaa; font-weight: bold;");
    tool->addWidget(m_statusLabel);
    lay->addLayout(tool);

    auto *tabs = new QTabWidget;

    // ── Tab 1: frame log ──────────────────────────────────────────────────────
    m_model = new ObdTableModel(this);
    m_model->setParser(&m_parser);
    m_table = new QTableView;
    m_table->setModel(m_model);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setShowGrid(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    tabs->addTab(m_table, "Ramki OBD-II");

    // ── Tab 2: DTC list ───────────────────────────────────────────────────────
    m_dtcTable = new QTableWidget(0, 4);
    m_dtcTable->setHorizontalHeaderLabels({"Czas (s)", "Tryb", "Kod DTC", "Źródło"});
    m_dtcTable->verticalHeader()->hide();
    m_dtcTable->horizontalHeader()->setStretchLastSection(true);
    m_dtcTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dtcTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    tabs->addTab(m_dtcTable, "Kody DTC");

    // ── Tab 3: live PIDs ──────────────────────────────────────────────────────
    m_liveTable = new QTableWidget(0, 3);
    m_liveTable->setHorizontalHeaderLabels({"Parametr", "Wartość", "Jednostka"});
    m_liveTable->verticalHeader()->hide();
    m_liveTable->horizontalHeader()->setStretchLastSection(true);
    m_liveTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_liveTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    tabs->addTab(m_liveTable, "Live PIDs");

    // ── Tab 4: arc gauges ─────────────────────────────────────────────────────
    struct GaugeDef { const char *name; uint16_t pid; const char *unit; double mn, mx; };
    static const GaugeDef defs[GAUGE_COUNT] = {
        {"Engine RPM",    0x0C, "rpm",  0,    8000},
        {"Vehicle Speed", 0x0D, "km/h", 0,    260},
        {"Coolant Temp",  0x05, "°C",   -40,  215},
        {"Engine Load",   0x04, "%",    0,    100},
        {"Throttle",      0x11, "%",    0,    100},
        {"MAF Air Flow",  0x10, "g/s",  0,    200},
    };

    auto *gaugeContainer = new QWidget;
    auto *gaugeGrid = new QGridLayout(gaugeContainer);
    gaugeGrid->setSpacing(6);
    gaugeGrid->setContentsMargins(8, 8, 8, 8);
    for (int i = 0; i < GAUGE_COUNT; ++i) {
        const auto &d = defs[i];
        m_gauges[i] = new ObdGauge(d.name, d.pid, d.unit, d.mn, d.mx);
        gaugeGrid->addWidget(m_gauges[i], i / 3, i % 3);
    }
    gaugeGrid->setRowStretch(0, 1);
    gaugeGrid->setRowStretch(1, 1);
    tabs->addTab(gaugeContainer, "Wskaźniki");

    lay->addWidget(tabs, 1);

    connect(m_clearBtn, &QPushButton::clicked, this, [this]() {
        m_model->clear();
        m_dtcTable->setRowCount(0);
        m_liveValues.clear();
        m_liveTable->setRowCount(0);
        m_reassembler.resetAll();
        for (auto *g : m_gauges) if (g) g->setValue(0);
        m_totalFrames = 0;
        m_statusLabel->setText("Ramki OBD-II: 0");
    });

    setStyleSheet(kStyle);
}

void ObdWidget::processFrame(const CanFrame &frame) {
    if (!ObdFrame::isObd(frame)) return;

    IsoTpReassembler::Result res;
    if (!m_reassembler.feed(frame, res))
        return;

    ObdFrame of = ObdFrame::fromPayload(res.canId, res.timestamp, res.payload);
    m_model->addFrame(of);
    m_totalFrames++;

    if (of.type == ObdFrame::Response) {
        if (of.mode == 0x03 || of.mode == 0x07 || of.mode == 0x0A) {
            QStringList dtcs = m_parser.parseDtcs(of);
            if (!dtcs.isEmpty())
                updateDtcTable(dtcs, of.mode);
        }
        if (of.mode == 0x01) {
            updateLivePidTable(of);
            updateGauges(of);
        }
    }

    int dtcRows = m_dtcTable->rowCount();
    m_statusLabel->setText(QString("Ramki OBD-II: %1 | Live PIDs: %2 | DTCs: %3")
        .arg(m_totalFrames).arg(m_liveValues.size()).arg(dtcRows));
}

void ObdWidget::updateDtcTable(const QStringList &dtcs, uint8_t mode) {
    QString modeStr = m_parser.modeName(mode);
    QString tsStr   = QString::number(
        static_cast<double>(QDateTime::currentMSecsSinceEpoch()) / 1000.0, 'f', 3);
    // Source label per DTC mode
    QString src = (mode == 0x03) ? "Stored" : (mode == 0x07) ? "Pending" : "Permanent";

    for (const QString &dtc : dtcs) {
        int row = m_dtcTable->rowCount();
        m_dtcTable->insertRow(row);
        m_dtcTable->setItem(row, 0, new QTableWidgetItem(tsStr));
        m_dtcTable->setItem(row, 1, new QTableWidgetItem(modeStr));
        auto *codeItem = new QTableWidgetItem(dtc);
        // colour by category: P=yellow, C=cyan, B=magenta, U=orange
        QColor col = dtc.startsWith('P') ? QColor("#ffee00") :
                     dtc.startsWith('C') ? QColor("#00ffff") :
                     dtc.startsWith('B') ? QColor("#ff88ff") :
                                           QColor("#ff8800");
        codeItem->setForeground(col);
        m_dtcTable->setItem(row, 2, codeItem);
        m_dtcTable->setItem(row, 3, new QTableWidgetItem(src));
    }
    if (m_dtcTable->rowCount() > 500)
        m_dtcTable->removeRow(0);
}

void ObdWidget::updateLivePidTable(const ObdFrame &f) {
    double val   = m_parser.decodePidValue(f.mode, f.pid, f.data.data(), f.dlc);
    QString name = m_parser.pidName(f.mode, f.pid);
    QString unit = m_parser.pidUnit(f.mode, f.pid);

    m_liveValues[f.pid] = {name, val, unit};

    m_liveTable->setRowCount(0);
    auto keys = m_liveValues.keys();
    std::sort(keys.begin(), keys.end());
    for (uint16_t pid : keys) {
        const auto &e = m_liveValues[pid];
        int row = m_liveTable->rowCount();
        m_liveTable->insertRow(row);
        m_liveTable->setItem(row, 0, new QTableWidgetItem(e.name));
        auto *valItem = new QTableWidgetItem(QString::number(e.value, 'f', 2));
        valItem->setForeground(QColor("#00ffaa"));
        m_liveTable->setItem(row, 1, valItem);
        m_liveTable->setItem(row, 2, new QTableWidgetItem(e.unit));
    }
}

void ObdWidget::updateGauges(const ObdFrame &f) {
    double val = m_parser.decodePidValue(f.mode, f.pid, f.data.data(), f.dlc);
    for (auto *g : m_gauges) {
        if (g && g->pid() == f.pid)
            g->setValue(val);
    }
}
