#include "J1939Widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QGroupBox>
#include <QScrollArea>

// ═══════════════════════════════════════════════════════════════
// J1939TableModel
// ═══════════════════════════════════════════════════════════════

J1939TableModel::J1939TableModel(QObject *parent) : QAbstractTableModel(parent) {}

int J1939TableModel::rowCount(const QModelIndex &) const { return m_frames.size(); }
int J1939TableModel::columnCount(const QModelIndex &) const { return _COUNT; }

QVariant J1939TableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_frames.size()) return {};
    const auto &f = m_frames.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case TIMESTAMP: return QString::number(f.timestamp / 1000.0, 'f', 3);
        case PRIO:      return f.priority;
        case PGN:       return f.pgnHex();
        case PGN_NAME:  return m_parser ? m_parser->pgnName(f.pgn) : QString();
        case SA:        return QString("0x%1").arg(f.sourceAddress, 2, 16, QChar('0'));
        case DA:        return f.isPdu1 ? QString("0x%1").arg(f.destAddress, 2, 16, QChar('0')) : QString("GE 0x%1").arg(f.groupExt, 2, 16, QChar('0'));
        case DLC:       return f.dlc;
        case DATA: {
            QString s; s.reserve(f.dlc * 3);
            for (int i = 0; i < f.dlc && i < 8; ++i)
                s += QString("%1 ").arg(f.data[i], 2, 16, QChar('0')).toUpper();
            return s.trimmed();
        }
        default: return {};
        }
    }

    if (role == Qt::ForegroundRole) {
        switch (index.column()) {
        case PGN:       return QColor("#00ffaa");
        case PGN_NAME:  return QColor("#ffaa00");
        case SA:        return QColor("#ff66cc");
        case PRIO:      return (f.priority <= 2) ? QColor("#ff4444") : QColor("#c0c0c0");
        default:        return QColor("#c0c0c0");
        }
    }

    if (role == Qt::ToolTipRole && index.column() == PGN_NAME) {
        return m_parser ? m_parser->decodeSignals(f) : QString();
    }

    return {};
}

QVariant J1939TableModel::headerData(int section, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case TIMESTAMP: return "Czas (s)";
    case PRIO:      return "Prio";
    case PGN:       return "PGN";
    case PGN_NAME:  return "Nazwa PGN";
    case SA:        return "SA";
    case DA:        return "DA/GE";
    case DLC:       return "DLC";
    case DATA:      return "Dane";
    default:        return {};
    }
}

void J1939TableModel::addFrame(const J1939Frame &frame) {
    beginInsertRows(QModelIndex(), m_frames.size(), m_frames.size());
    m_frames.append(frame);
    if (m_frames.size() > MAX_ROWS) { m_frames.pop_front(); }
    endInsertRows();
}

void J1939TableModel::clear() {
    beginResetModel();
    m_frames.clear();
    endResetModel();
}

J1939Frame J1939TableModel::frameAt(int row) const {
    if (row >= 0 && row < m_frames.size()) return m_frames.at(row);
    return {};
}

// ═══════════════════════════════════════════════════════════════
// J1939Widget
// ═══════════════════════════════════════════════════════════════

J1939Widget::J1939Widget(QWidget *parent) : QWidget(parent) {
    setupUi();
}

void J1939Widget::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);

    // --- Panel filtrów ---
    auto *filterLayout = new QHBoxLayout;

    m_filterEnabled = new QCheckBox("Filtruj:");
    m_filterEnabled->setChecked(false);
    m_filterEnabled->setStyleSheet("QCheckBox { color: #ff66cc; font-weight: bold; }");

    filterLayout->addWidget(m_filterEnabled);

    m_pgnFilter = new QComboBox;
    m_pgnFilter->setMinimumWidth(280);
    m_pgnFilter->addItem("-- wszystkie PGN --", 0);

    // Wypełnij znane PGN
    QList<uint32_t> pgns = m_parser.knownPgns();
    std::sort(pgns.begin(), pgns.end());
    for (uint32_t pgn : pgns) {
        QString label = QString("0x%1 – %2").arg(pgn, 5, 16, QChar('0')).toUpper()
                        .arg(m_parser.pgnName(pgn));
        m_pgnFilter->addItem(label, pgn);
    }
    m_pgnFilter->setEditable(true);
    filterLayout->addWidget(m_pgnFilter);

    filterLayout->addWidget(new QLabel("SA:"));
    m_saFilter = new QLineEdit;
    m_saFilter->setPlaceholderText("np. 00, 13, FF");
    m_saFilter->setMaximumWidth(60);
    filterLayout->addWidget(m_saFilter);

    m_clearBtn = new QPushButton("Wyczyść tabelę");
    filterLayout->addWidget(m_clearBtn);
    filterLayout->addStretch();

    m_statusLabel = new QLabel("Ramki J1939: 0");
    m_statusLabel->setStyleSheet("color: #00ffaa; font-weight: bold;");
    filterLayout->addWidget(m_statusLabel);

    mainLayout->addLayout(filterLayout);

    // --- Tabela ---
    m_model = new J1939TableModel(this);
    m_model->setParser(&m_parser);
    m_tableView = new QTableView;
    m_tableView->setModel(m_model);
    m_tableView->verticalHeader()->hide();
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->setShowGrid(false);
    m_tableView->setAlternatingRowColors(false);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setMinimumHeight(350);
    mainLayout->addWidget(m_tableView, 1);

    // --- Panel szczegółów SPN ---
    auto *detailGroup = new QGroupBox("Szczegóły SPN (zaznacz ramkę w tabeli)");
    detailGroup->setStyleSheet(
        "QGroupBox { color: #ff66cc; font-weight: bold; border: 1px solid #e94560; "
        "border-radius: 4px; margin-top: 8px; padding-top: 16px; }"
    );
    auto *detailLayout = new QVBoxLayout(detailGroup);
    m_detailLabel = new QLabel("Zaznacz ramkę J1939, aby zobaczyć zdekodowane sygnały.");
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setStyleSheet("color: #c0c0c0; font-family: Consolas, monospace; font-size: 13px; "
                                 "background-color: #1a1a2e; padding: 10px; border-radius: 4px;");
    m_detailLabel->setMinimumHeight(100);
    detailLayout->addWidget(m_detailLabel);
    mainLayout->addWidget(detailGroup);

    // --- Połączenia ---
    connect(m_filterEnabled, &QCheckBox::toggled, this, &J1939Widget::applyFilters);
    connect(m_pgnFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &J1939Widget::applyFilters);
    connect(m_saFilter, &QLineEdit::textChanged, this, &J1939Widget::applyFilters);
    connect(m_clearBtn, &QPushButton::clicked, this, [this]() {
        m_model->clear();
        m_statusLabel->setText("Ramki J1939: 0");
        m_detailLabel->setText("Zaznacz ramkę J1939, aby zobaczyć zdekodowane sygnały.");
    });
    connect(m_tableView, &QTableView::clicked, this, &J1939Widget::onFrameSelected);

    // Global QSS (style_dark/light.qss) handles widget styling
}

void J1939Widget::processFrame(const CanFrame &frame) {
    J1939Frame jframe;
    if (!J1939Frame::fromCanFrame(frame, jframe)) return;

    // Zastosuj filtr
    if (m_filterActive) {
        if (m_filterPgn != 0 && jframe.pgn != m_filterPgn) return;
        if (m_filterSa != 0xFF && jframe.sourceAddress != m_filterSa) return;
    }

    m_model->addFrame(jframe);
    m_statusLabel->setText(QString("Ramki J1939: %1").arg(m_model->rowCount()));
}

void J1939Widget::onFrameSelected(const QModelIndex &index) {
    if (!index.isValid()) return;
    J1939Frame frame = m_model->frameAt(index.row());
    QString info = frame.toString();
    QString spnSignals = m_parser.decodeSignals(frame);

    m_detailLabel->setText(QString(
        "<b style='color:#ff66cc;'>%1</b><br>"
        "<span style='color:#00ffaa;'><b>Sygnały SPN:</b></span><br>"
        "<span style='color:#c0c0c0;'>%2</span>"
    ).arg(info, spnSignals));
}

void J1939Widget::applyFilters() {
    m_filterActive = m_filterEnabled->isChecked();
    if (!m_filterActive) {
        m_filterPgn = 0;
        m_filterSa = 0xFF;
        return;
    }

    // Odczytaj PGN z comboboxa
    QVariant data = m_pgnFilter->currentData();
    if (data.isValid() && data.toUInt() != 0) {
        m_filterPgn = data.toUInt();
    } else if (m_filterActive) {
        // spróbuj sparsować wpisany tekst jako hex PGN
        bool ok;
        m_filterPgn = m_pgnFilter->currentText().toUInt(&ok, 16);
        if (!ok) m_filterPgn = 0;
    }

    // Odczytaj SA
    QString saText = m_saFilter->text().trimmed();
    if (!saText.isEmpty()) {
        bool ok;
        uint32_t val = saText.toUInt(&ok, 16);
        if (ok && val <= 0xFF) m_filterSa = static_cast<uint8_t>(val);
        else m_filterSa = 0xFF;
    } else {
        m_filterSa = 0xFF;
    }
}
