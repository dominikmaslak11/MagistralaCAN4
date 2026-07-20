#include "CanSignalMonitorWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QFont>
#include <chrono>

namespace {

uint64_t nowUs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace

CanSignalMonitorWidget::CanSignalMonitorWidget(QWidget *parent) : QWidget(parent) {
    buildUi();
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &CanSignalMonitorWidget::onTick);
    m_timer->start(kTickMs);
}

void CanSignalMonitorWidget::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);

    auto *bar = new QHBoxLayout;
    bar->addWidget(new QLabel("Filter:", this));
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText("Signal or message name…");
    m_filterEdit->setMaximumWidth(200);
    bar->addWidget(m_filterEdit);
    bar->addSpacing(12);
    bar->addWidget(new QLabel("Stale after (ms):", this));
    m_staleSpin = new QSpinBox(this);
    m_staleSpin->setRange(100, 60000);
    m_staleSpin->setValue(kDefaultStaleMs);
    m_staleSpin->setSingleStep(100);
    m_staleSpin->setMaximumWidth(80);
    bar->addWidget(m_staleSpin);
    bar->addStretch();
    m_resetBtn = new QPushButton("Reset", this);
    bar->addWidget(m_resetBtn);
    root->addLayout(bar);

    m_table = new QTableWidget(0, 6, this);
    m_table->setHorizontalHeaderLabels({"Message", "Signal", "Value", "Unit", "Updates", "Status"});
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setDefaultSectionSize(100);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(true);
    m_table->setFont(QFont("Courier New", 9));
    root->addWidget(m_table, 1);

    m_infoLabel = new QLabel("Signals: 0  |  Alarms: 0", this);
    root->addWidget(m_infoLabel);

    connect(m_resetBtn,   &QPushButton::clicked, this, &CanSignalMonitorWidget::reset);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &CanSignalMonitorWidget::applyFilter);
}

void CanSignalMonitorWidget::setDbc(const DbcParser *dbc) {
    m_monitor.setDbc(dbc);
}

void CanSignalMonitorWidget::processFrame(const CanFrame &frame) {
    m_monitor.update(frame);
}

void CanSignalMonitorWidget::reset() {
    m_monitor.reset();
    m_table->setRowCount(0);
    m_infoLabel->setText("Signals: 0  |  Alarms: 0");
}

void CanSignalMonitorWidget::onTick() {
    refreshTable();
}

void CanSignalMonitorWidget::applyFilter() {
    refreshTable();
}

void CanSignalMonitorWidget::refreshTable() {
    bool sortingWasEnabled = m_table->isSortingEnabled();
    m_table->setSortingEnabled(false);

    auto values = m_monitor.allValues();
    QString filt = m_filterEdit->text().trimmed().toLower();
    uint64_t staleUs = static_cast<uint64_t>(m_staleSpin->value()) * 1000ULL;
    uint64_t now = nowUs();

    m_table->setRowCount(0);

    for (auto &sv : values) {
        if (!filt.isEmpty()) {
            if (!sv.signalName.toLower().contains(filt) &&
                !sv.msgName.toLower().contains(filt)) continue;
        }

        int row = m_table->rowCount();
        m_table->insertRow(row);

        bool stale   = (now > sv.lastUpdateUs) && ((now - sv.lastUpdateUs) > staleUs);
        bool alarmed = sv.alarmActive();

        QColor bg;
        if (alarmed)    bg = QColor(0xFF, 0x88, 0x88);
        else if (stale) bg = QColor(0xCC, 0xCC, 0xCC);

        auto cell = [&](int col, const QString &txt) {
            auto *item = new QTableWidgetItem(txt);
            item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
            if (bg.isValid()) item->setBackground(bg);
            m_table->setItem(row, col, item);
        };

        cell(0, sv.msgName);
        cell(1, sv.signalName);
        cell(2, QString::number(sv.physValue, 'f', 3));
        cell(3, sv.unit);
        cell(4, QString::number(sv.updateCount));
        cell(5, alarmed ? "ALARM" : stale ? "STALE" : "OK");
    }

    if (sortingWasEnabled) m_table->setSortingEnabled(true);
    m_infoLabel->setText(QString("Signals: %1  |  Alarms: %2")
        .arg(values.size()).arg(m_monitor.activeAlarmCount()));
}
