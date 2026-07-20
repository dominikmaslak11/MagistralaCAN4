#include "CanCustomDashboard.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QSettings>
#include <QJsonDocument>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include <algorithm>

namespace {

const char *kStyleSheet = R"(
    QScrollArea { background: #0a0e17; border: none; }
    QWidget#gridWidget { background: #0a0e17; }
)";

} // namespace

CanCustomDashboard::CanCustomDashboard(QWidget *parent) : QWidget(parent) {
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(4, 4, 4, 4);
    outerLayout->setSpacing(4);

    // ── Header bar ──────────────────────────────────────────────────────────
    auto *header = new QHBoxLayout;
    header->setSpacing(6);

    auto *title = new QLabel("Konfigurowalny Dashboard Sygnałów DBC");
    title->setStyleSheet("color: #ff66cc; font-weight: bold; font-size: 13px; font-family: Consolas;");
    header->addWidget(title);
    header->addStretch();

    m_colCombo = new QComboBox;
    m_colCombo->addItems({"2 kol.", "3 kol.", "4 kol.", "5 kol."});
    m_colCombo->setCurrentIndex(1);
    m_colCombo->setFixedWidth(80);
    m_colCombo->setStyleSheet(
        "QComboBox { background: #1a1a2e; color: #ffaa00; border: 1px solid #e94560; "
        "border-radius: 4px; padding: 3px 8px; font-family: Consolas; }"
        "QComboBox QAbstractItemView { background: #1a1a2e; color: #ffaa00; selection-background-color: #e94560; }");
    connect(m_colCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) { changeColumns(idx + 2); });
    header->addWidget(m_colCombo);

    m_addBtn = new QPushButton("+ Dodaj");
    m_addBtn->setVisible(false);
    m_addBtn->setStyleSheet(
        "QPushButton { background: #1a1a2e; color: #00ffaa; border: 1px solid #00ffaa; "
        "border-radius: 4px; padding: 4px 10px; font-family: Consolas; }"
        "QPushButton:hover { background: #00ffaa; color: #0a0e17; }");
    connect(m_addBtn, &QPushButton::clicked, this, &CanCustomDashboard::addGaugeDialog);
    header->addWidget(m_addBtn);

    m_editBtn = new QPushButton("Edytuj");
    m_editBtn->setCheckable(true);
    m_editBtn->setStyleSheet(
        "QPushButton { background: #1a1a2e; color: #ffaa00; border: 1px solid #e94560; "
        "border-radius: 4px; padding: 4px 10px; font-family: Consolas; }"
        "QPushButton:checked { background: #e94560; color: white; }"
        "QPushButton:hover:!checked { background: #2a2a3e; }");
    connect(m_editBtn, &QPushButton::toggled, this, &CanCustomDashboard::toggleEditMode);
    header->addWidget(m_editBtn);

    auto *saveBtn = new QPushButton("Zapisz");
    saveBtn->setStyleSheet(
        "QPushButton { background: #1a1a2e; color: #c0c0c0; border: 1px solid #e94560; "
        "border-radius: 4px; padding: 4px 10px; font-family: Consolas; }"
        "QPushButton:hover { background: #2a2a3e; }");
    connect(saveBtn, &QPushButton::clicked, this, &CanCustomDashboard::saveLayout);
    header->addWidget(saveBtn);

    auto *loadBtn = new QPushButton("Wczytaj");
    loadBtn->setStyleSheet(
        "QPushButton { background: #1a1a2e; color: #c0c0c0; border: 1px solid #e94560; "
        "border-radius: 4px; padding: 4px 10px; font-family: Consolas; }"
        "QPushButton:hover { background: #2a2a3e; }");
    connect(loadBtn, &QPushButton::clicked, this, &CanCustomDashboard::loadLayout);
    header->addWidget(loadBtn);

    outerLayout->addLayout(header);

    // ── Scroll area ──────────────────────────────────────────────────────────
    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    m_gridWidget = new QWidget;
    m_gridWidget->setObjectName("gridWidget");
    m_grid = new QGridLayout(m_gridWidget);
    m_grid->setSpacing(6);
    m_grid->setContentsMargins(4, 4, 4, 4);
    m_scroll->setWidget(m_gridWidget);
    outerLayout->addWidget(m_scroll, 1);

    setStyleSheet(kStyleSheet);

    // ── Staleness timer ──────────────────────────────────────────────────────
    auto *staleTimer = new QTimer(this);
    staleTimer->setInterval(1000);
    connect(staleTimer, &QTimer::timeout, this, [this]() {
        for (auto &e : m_entries) e.gauge->checkStaleness();
    });
    staleTimer->start();

    // ── Restore saved layout from QSettings ─────────────────────────────────
    QSettings settings;
    QByteArray data = settings.value("CanCustomDashboard/layout").toByteArray();
    if (!data.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            m_layout = DashboardLayout::fromJson(doc.object());
            if (m_layout.columns >= 2 && m_layout.columns <= 5)
                m_colCombo->setCurrentIndex(m_layout.columns - 2);
        }
    }

    rebuildGrid();
}

void CanCustomDashboard::setDbcParser(const DbcParser *parser) {
    m_dbcParser = parser;
    if (!m_dbcParser) return;
    // Re-apply DBC ranges to existing gauges
    for (auto &e : m_entries) {
        if (!e.config.useDbcRange) continue;
        for (const auto &msg : m_dbcParser->messages()) {
            if (msg.id != e.config.canId) continue;
            for (const auto &sig : msg.sigList) {
                if (sig.name != e.config.signalName) continue;
                if (sig.maximum > sig.minimum)
                    e.gauge->setRange(sig.minimum, sig.maximum);
                if (!sig.unit.isEmpty())
                    e.gauge->setUnit(sig.unit);
            }
        }
    }
}

void CanCustomDashboard::processFrame(const CanFrame &frame) {
    if (!m_dbcParser || m_entries.isEmpty()) return;

    bool relevant = false;
    for (const auto &e : m_entries)
        if (e.config.canId == frame.id) { relevant = true; break; }
    if (!relevant) return;

    const auto decoded = m_dbcParser->decodeSignals(frame.id, frame.data.data(), frame.dlc);
    for (auto &e : m_entries) {
        if (e.config.canId != frame.id) continue;
        auto it = decoded.find(e.config.signalName);
        if (it == decoded.end()) continue;
        e.gauge->setValue(it.value());
        e.gauge->recordValue(it.value());
    }
}

void CanCustomDashboard::toggleEditMode() {
    m_editMode = m_editBtn->isChecked();
    m_addBtn->setVisible(m_editMode);
    applyEditMode();
}

void CanCustomDashboard::applyEditMode() {
    for (auto &e : m_entries)
        if (e.delBtn) e.delBtn->setVisible(m_editMode);
}

void CanCustomDashboard::addGaugeDialog() {
    if (!m_dbcParser) {
        QMessageBox::information(this, "Dashboard", "Najpierw wczytaj plik DBC.");
        return;
    }

    struct SigEntry { QString display, sigName; uint32_t canId; double min, max; QString unit; };
    QVector<SigEntry> sigs;
    for (const auto &msg : m_dbcParser->messages()) {
        for (const auto &sig : msg.sigList) {
            sigs.append({
                QString("0x%1  %2  →  %3")
                    .arg(msg.id, 3, 16, QChar('0')).toUpper()
                    .arg(msg.name).arg(sig.name),
                sig.name, msg.id, sig.minimum, sig.maximum, sig.unit
            });
        }
    }
    if (sigs.isEmpty()) {
        QMessageBox::information(this, "Dashboard", "DBC nie zawiera żadnych sygnałów.");
        return;
    }

    auto *dlg = new QDialog(this);
    dlg->setWindowTitle("Dodaj wskaźnik");
    dlg->setMinimumWidth(460);
    dlg->setStyleSheet(
        "QDialog { background: #1a1a2e; }"
        "QLabel { color: #c0c0c0; font-family: Consolas; font-size: 12px; }"
        "QComboBox, QDoubleSpinBox { background: #0a0e17; color: #00ffaa; "
        "  border: 1px solid #e94560; border-radius: 3px; padding: 3px; font-family: Consolas; }"
        "QComboBox QAbstractItemView { background: #0a0e17; color: #00ffaa; "
        "  selection-background-color: #e94560; }"
        "QCheckBox { color: #c0c0c0; font-family: Consolas; }"
        "QPushButton { background: #0a0e17; color: #00ffaa; border: 1px solid #e94560; "
        "  border-radius: 4px; padding: 4px 14px; font-family: Consolas; }"
        "QPushButton:hover { background: #e94560; color: white; }");

    auto *form = new QFormLayout(dlg);
    form->setContentsMargins(12, 12, 12, 12);
    form->setSpacing(8);

    auto *sigCombo = new QComboBox;
    sigCombo->setMaxVisibleItems(20);
    for (const auto &s : sigs)
        sigCombo->addItem(s.display);
    form->addRow("Sygnał:", sigCombo);

    auto *styleCombo = new QComboBox;
    styleCombo->addItems({"Bar", "Digital", "Compact"});
    form->addRow("Styl:", styleCombo);

    auto *useDbcChk = new QCheckBox("Użyj zakresu z DBC");
    useDbcChk->setChecked(true);
    form->addRow("", useDbcChk);

    auto *minSpin = new QDoubleSpinBox;
    minSpin->setRange(-1e9, 1e9); minSpin->setDecimals(3);
    minSpin->setEnabled(false);
    form->addRow("Min:", minSpin);

    auto *maxSpin = new QDoubleSpinBox;
    maxSpin->setRange(-1e9, 1e9); maxSpin->setDecimals(3);
    maxSpin->setValue(100.0);
    maxSpin->setEnabled(false);
    form->addRow("Max:", maxSpin);

    auto refreshRange = [&](int idx) {
        if (idx < 0 || idx >= sigs.size()) return;
        minSpin->setValue(sigs[idx].min);
        maxSpin->setValue(sigs[idx].max > sigs[idx].min ? sigs[idx].max : 100.0);
    };
    refreshRange(0);

    connect(sigCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            dlg, [&](int idx) { refreshRange(idx); });
    connect(useDbcChk, &QCheckBox::toggled, dlg, [&](bool on) {
        minSpin->setEnabled(!on);
        maxSpin->setEnabled(!on);
    });

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(btns);
    connect(btns, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    if (dlg->exec() != QDialog::Accepted) { delete dlg; return; }

    const int si = sigCombo->currentIndex();
    if (si < 0 || si >= sigs.size()) { delete dlg; return; }

    GaugeConfig cfg;
    cfg.signalName  = sigs[si].sigName;
    cfg.canId       = sigs[si].canId;
    cfg.style       = styleCombo->currentIndex();
    cfg.useDbcRange = useDbcChk->isChecked();
    if (cfg.useDbcRange) {
        cfg.rangeMin = sigs[si].min;
        cfg.rangeMax = sigs[si].max > sigs[si].min ? sigs[si].max : 100.0;
    } else {
        cfg.rangeMin = minSpin->value();
        cfg.rangeMax = maxSpin->value();
    }
    cfg.unit = sigs[si].unit;

    delete dlg;

    m_layout.addGauge(cfg);
    rebuildGrid();
    persistLayout();
}

void CanCustomDashboard::removeGauge(int index) {
    if (index < 0 || index >= m_layout.gauges.size()) return;
    m_layout.removeGauge(index);
    rebuildGrid();
    persistLayout();
}

void CanCustomDashboard::changeColumns(int cols) {
    m_layout.columns = cols;
    rebuildGrid();
    persistLayout();
}

void CanCustomDashboard::rebuildGrid() {
    // Remove all items from the grid, deleting their widgets
    while (m_grid->count() > 0) {
        QLayoutItem *item = m_grid->takeAt(0);
        if (item->widget()) delete item->widget();
        delete item;
    }
    m_entries.clear();

    if (m_layout.gauges.isEmpty()) {
        auto *lbl = new QLabel(
            "Wczytaj plik DBC, kliknij 'Edytuj' → '+ Dodaj',\n"
            "aby dodać wskaźniki sygnałów do dashboardu.", m_gridWidget);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet("color: #ffaa00; font-size: 13px; font-family: Consolas; padding: 40px;");
        m_grid->addWidget(lbl, 0, 0);
        return;
    }

    const int cols = std::max(1, m_layout.columns);

    for (int i = 0; i < m_layout.gauges.size(); ++i) {
        const auto &cfg = m_layout.gauges[i];
        const auto style = static_cast<CanGaugeWidget::Style>(std::clamp(cfg.style, 0, 2));

        // ── Cell container ──
        auto *cell = new QFrame(m_gridWidget);
        cell->setFrameStyle(QFrame::NoFrame);
        cell->setStyleSheet("QFrame { background: #161b22; border-radius: 6px; border: 1px solid #2a2a3c; }");

        auto *cellLay = new QVBoxLayout(cell);
        cellLay->setContentsMargins(4, 4, 4, 4);
        cellLay->setSpacing(2);

        // ── Top bar: signal name + delete button ──
        auto *topBar = new QHBoxLayout;
        topBar->setContentsMargins(0, 0, 0, 0);
        topBar->setSpacing(4);

        auto *nameLabel = new QLabel(cfg.signalName, cell);
        nameLabel->setStyleSheet("color: #ff66cc; font-size: 10px; font-family: Consolas; background: transparent; border: none;");
        nameLabel->setToolTip(QString("CAN ID: 0x%1").arg(cfg.canId, 3, 16, QChar('0')).toUpper());
        topBar->addWidget(nameLabel, 1);

        auto *delBtn = new QPushButton("✕", cell);
        delBtn->setFixedSize(18, 18);
        delBtn->setStyleSheet(
            "QPushButton { background: #e94560; color: white; border: none; "
            "border-radius: 3px; font-size: 10px; font-weight: bold; }"
            "QPushButton:hover { background: #ff6680; }");
        delBtn->setVisible(m_editMode);
        topBar->addWidget(delBtn);
        cellLay->addLayout(topBar);

        // ── Gauge widget ──
        auto *gauge = new CanGaugeWidget(style, cell);
        gauge->setSignalName(cfg.signalName);
        gauge->setUnit(cfg.unit);
        gauge->setRange(cfg.rangeMin, cfg.rangeMax);
        cellLay->addWidget(gauge);

        // Override with DBC ranges if available
        if (cfg.useDbcRange && m_dbcParser) {
            for (const auto &msg : m_dbcParser->messages()) {
                if (msg.id != cfg.canId) continue;
                for (const auto &sig : msg.sigList) {
                    if (sig.name != cfg.signalName) continue;
                    if (sig.maximum > sig.minimum)
                        gauge->setRange(sig.minimum, sig.maximum);
                    if (!sig.unit.isEmpty())
                        gauge->setUnit(sig.unit);
                }
            }
        }

        const int capturedIndex = i;
        connect(delBtn, &QPushButton::clicked, this, [this, capturedIndex]() {
            removeGauge(capturedIndex);
        });

        GaugeEntry entry;
        entry.gauge  = gauge;
        entry.cell   = cell;
        entry.delBtn = delBtn;
        entry.config = cfg;
        m_entries.append(entry);

        m_grid->addWidget(cell, i / cols, i % cols);
    }

    // Fill empty columns in last row so gauges don't stretch
    const int lastRowCount = static_cast<int>(m_layout.gauges.size()) % cols;
    if (lastRowCount != 0) {
        const int lastRow = (static_cast<int>(m_layout.gauges.size()) - 1) / cols;
        for (int c = lastRowCount; c < cols; ++c) {
            auto *spacer = new QWidget(m_gridWidget);
            spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            m_grid->addWidget(spacer, lastRow, c);
        }
    }
}

void CanCustomDashboard::persistLayout() {
    QSettings settings;
    settings.setValue("CanCustomDashboard/layout",
                      QJsonDocument(m_layout.toJson()).toJson(QJsonDocument::Compact));
}

void CanCustomDashboard::saveLayout() {
    const QString path = QFileDialog::getSaveFileName(
        this, "Zapisz układ dashboardu", QString(),
        "JSON (*.json);;Wszystkie pliki (*)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Dashboard", "Nie można zapisać pliku.");
        return;
    }
    f.write(QJsonDocument(m_layout.toJson()).toJson());
}

void CanCustomDashboard::loadLayout() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Wczytaj układ dashboardu", QString(),
        "JSON (*.json);;Wszystkie pliki (*)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Dashboard", "Nie można otworzyć pliku.");
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        QMessageBox::warning(this, "Dashboard", "Nieprawidłowy format pliku JSON.");
        return;
    }
    m_layout = DashboardLayout::fromJson(doc.object());
    if (m_layout.columns >= 2 && m_layout.columns <= 5)
        m_colCombo->setCurrentIndex(m_layout.columns - 2);
    rebuildGrid();
    persistLayout();
}
