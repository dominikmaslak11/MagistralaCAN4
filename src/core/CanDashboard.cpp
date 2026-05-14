#include "CanDashboard.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QLabel>

CanDashboard::CanDashboard(QWidget *parent) : QWidget(parent) {
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    // ── Górny pasek: wyszukiwarka + przycisk kolumn ──
    auto *topBar = new QHBoxLayout;
    m_searchBox = new QLineEdit;
    m_searchBox->setPlaceholderText("Szukaj sygnału...");
    m_searchBox->setStyleSheet(
        "QLineEdit { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560; "
        "border-radius: 4px; padding: 5px 10px; font-family: Consolas; font-size: 12px; }");
    topBar->addWidget(m_searchBox);
    m_columnsBtn = new QPushButton("3 kol.");
    m_columnsBtn->setFixedWidth(60);
    m_columnsBtn->setStyleSheet("QPushButton { background: #1a1a2e; color: #ffaa00; border: 1px solid #e94560; "
                                "border-radius: 4px; padding: 4px; font-weight: bold; }");
    topBar->addWidget(m_columnsBtn);
    outerLayout->addLayout(topBar);

    // ── Scroll area z grupami ──
    m_scrollArea = new QScrollArea;
    m_scrollArea->setWidgetResizable(true);
    m_scrollWidget = new QWidget;
    m_groupsLayout = new QVBoxLayout(m_scrollWidget);
    m_groupsLayout->setContentsMargins(4, 4, 4, 4);
    m_groupsLayout->addStretch();
    m_scrollArea->setWidget(m_scrollWidget);
    outerLayout->addWidget(m_scrollArea, 1);

    // ── Staleness timer ──
    m_staleTimer.setInterval(1000);
    connect(&m_staleTimer, &QTimer::timeout, this, &CanDashboard::onStalenessCheck);
    m_staleTimer.start();

    // ── Sygnały ──
    connect(m_searchBox, &QLineEdit::textChanged, this, &CanDashboard::applyFilter);
    connect(m_columnsBtn, &QPushButton::clicked, this, [this]() {
        m_columns = (m_columns == 2) ? 3 : (m_columns == 3) ? 4 : 2;
        m_columnsBtn->setText(QString("%1 kol.").arg(m_columns));
        rebuildDashboard();
    });

    setStyleSheet(R"(
        QScrollArea { background-color: #0a0e17; border: none; }
        QLabel { color: #c0c0c0; }
    )");
}

void CanDashboard::setDbcParser(const DbcParser *parser) {
    m_dbcParser = parser;
    m_idToSignals.clear();
    m_messages.clear();

    if (m_dbcParser) {
        m_messages = m_dbcParser->messages();
        for (const auto &msg : m_messages)
            for (const auto &sig : msg.sigList)
                m_idToSignals[msg.id].append(sig.name);
    }

    rebuildDashboard();
}

void CanDashboard::rebuildDashboard() {
    // Wyczyść stare widgety
    for (auto it = m_gauges.begin(); it != m_gauges.end(); ++it)
        delete it.value();
    m_gauges.clear();

    for (auto it = m_groups.begin(); it != m_groups.end(); ++it) {
        delete it->header;
        delete it->container;
    }
    m_groups.clear();

    // Usuń wszystko z layoutu (poza stretchem)
    while (m_groupsLayout->count() > 1) {
        auto *item = m_groupsLayout->takeAt(0);
        if (item->widget()) delete item->widget();
        delete item;
    }

    if (!m_dbcParser || m_messages.isEmpty()) {
        auto *lbl = new QLabel("Wczytaj plik DBC aby wyświetlić dashboard.");
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet("color: #ffaa00; font-size: 14px; padding: 20px;");
        m_groupsLayout->insertWidget(0, lbl);
        return;
    }

    for (const auto &msg : m_messages) {
        if (msg.sigList.isEmpty()) continue;

        // ── Nagłówek grupy (przycisk do zwijania) ──
        auto *header = new QPushButton(
            QString("📦 0x%1 — %2  [%3 sygn.]")
                .arg(msg.id, 3, 16, QChar('0')).toUpper()
                .arg(msg.name).arg(msg.sigList.size()));
        header->setStyleSheet(
            "QPushButton { text-align: left; background: #1a1a2e; color: #ff66cc; "
            "border: 1px solid #e94560; border-radius: 4px; padding: 6px 12px; "
            "font-weight: bold; font-size: 12px; } "
            "QPushButton:hover { background: #2a2a3e; }");
        header->setCheckable(true);
        header->setChecked(false);
        m_groupsLayout->insertWidget(m_groupsLayout->count() - 1, header);

        // ── Kontener z siatką gauge'y ──
        auto *container = new QFrame;
        auto *grid = new QGridLayout(container);
        grid->setSpacing(4);
        container->setFrameStyle(QFrame::NoFrame);

        int col = 0, row = 0;
        for (const auto &sig : msg.sigList) {
            auto *gauge = new CanGaugeWidget(CanGaugeWidget::Bar);
            gauge->setSignalName(sig.name);
            gauge->setUnit(sig.unit);
            gauge->setRange(sig.minimum, sig.maximum);
            gauge->setVisible(true);
            grid->addWidget(gauge, row, col, 1, 1);
            m_gauges[sig.name] = gauge;

            col++;
            if (col >= m_columns) { col = 0; row++; }
        }
        m_groupsLayout->insertWidget(m_groupsLayout->count() - 1, container);

        // Zwijanie
        uint32_t id = msg.id;
        GroupWidgets gw { header, container, false };
        m_groups[id] = gw;

        connect(header, &QPushButton::toggled, this, [this, id](bool checked) {
            if (m_groups.contains(id)) {
                m_groups[id].collapsed = checked;
                m_groups[id].container->setVisible(!checked);
                m_groups[id].header->setText(
                    m_groups[id].header->text().replace(checked ? "📦" : "📂", checked ? "📂" : "📦"));
            }
        });
    }

    applyFilter(m_searchBox->text());
}

void CanDashboard::applyFilter(const QString &text) {
    QString filter = text.trimmed().toLower();

    for (auto it = m_groups.begin(); it != m_groups.end(); ++it) {
        uint32_t id = it.key();
        const auto &sigNames = m_idToSignals.value(id);
        bool anyVisible = filter.isEmpty();

        if (!filter.isEmpty()) {
            // Sprawdź czy którykolwiek sygnał pasuje do filtra
            for (const auto &name : sigNames) {
                if (name.toLower().contains(filter) ||
                    QString("0x%1").arg(id, 3, 16, QChar('0')).contains(filter)) {
                    anyVisible = true;
                    break;
                }
            }
            // Pokaż/ukryj pojedyncze gauge'e
            for (const auto &name : sigNames) {
                if (auto *g = m_gauges.value(name))
                    g->setVisible(name.toLower().contains(filter));
            }
        } else {
            for (const auto &name : sigNames) {
                if (auto *g = m_gauges.value(name))
                    g->setVisible(true);
            }
        }

        it->header->setVisible(anyVisible);
        if (!it->collapsed)
            it->container->setVisible(anyVisible);
    }
}

void CanDashboard::updateSignal(const CanFrame &frame) {
    if (!m_dbcParser) return;
    if (m_idToSignals.value(frame.id).isEmpty()) return;

    const auto decoded = m_dbcParser->decodeSignals(frame.id, frame.data.data(), frame.dlc);
    for (auto it = decoded.cbegin(); it != decoded.cend(); ++it) {
        if (auto *gauge = m_gauges.value(it.key()))
            gauge->setValue(it.value());
    }
}

void CanDashboard::onStalenessCheck() {
    for (auto *gauge : m_gauges)
        gauge->checkStaleness();
}
