#include "CanDashboard.h"
#include <QVBoxLayout>
#include <QScrollArea>
#include <QFont>
#include <QDebug>

CanDashboard::CanDashboard(QWidget *parent) : QWidget(parent) {
    auto *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    auto *scrollWidget = new QWidget;
    auto *mainLayout = new QVBoxLayout(scrollWidget);

    m_grid = new QGridLayout;
    m_grid->setSpacing(6);
    mainLayout->addLayout(m_grid);
    mainLayout->addStretch();

    scrollArea->setWidget(scrollWidget);
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0,0,0,0);
    outerLayout->addWidget(scrollArea);

    setStyleSheet(R"(
        QLabel {
            font-family: "Consolas", "Courier New", monospace;
            padding: 4px 8px;
            color: #c0c0c0;
        }
    )");
}

void CanDashboard::setDbcParser(const DbcParser *parser) {
    m_dbcParser = parser;
    // Wyczyść stare panele
    QLayoutItem *child;
    while ((child = m_grid->takeAt(0)) != nullptr) {
        if (child->widget()) delete child->widget();
        delete child;
    }
    m_valueLabels.clear();
    m_idToSignals.clear();

    if (!m_dbcParser) return;

    // Zbuduj mapowanie ID -> sygnały
    QVector<DbcMessage> msgs = m_dbcParser->messages();
    for (const auto &msg : msgs) {
        QStringList signalNames;
        for (const auto &sig : msg.sigList) {
            signalNames.append(sig.name);
        }
        m_idToSignals[msg.id] = signalNames;
    }

    buildPanels();
}

void CanDashboard::buildPanels() {
    if (!m_dbcParser) return;

    int row = 0, col = 0;
    const int maxCols = 3;

    // Nagłówki
    auto makeHeader = [&](const QString &text) {
        auto *lbl = new QLabel(text);
        lbl->setStyleSheet("font-weight: bold; color: #ffaa00; font-size: 14px;");
        lbl->setAlignment(Qt::AlignCenter);
        return lbl;
    };

    // Iteruj po wszystkich wiadomościach i sygnałach
    QVector<DbcMessage> msgs = m_dbcParser->messages();
    for (const auto &msg : msgs) {
        for (const auto &sig : msg.sigList) {
            // Etykieta nazwy sygnału
            auto *nameLabel = new QLabel(sig.name + ":");
            nameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            nameLabel->setStyleSheet("color: #ff66cc; font-weight: bold;");
            m_grid->addWidget(nameLabel, row, col * 2);

            // Etykieta wartości
            auto *valueLabel = new QLabel("— " + sig.unit);
            valueLabel->setAlignment(Qt::AlignCenter);
            valueLabel->setStyleSheet("background-color: #161b22; color: #00ffaa; font-size: 18px; font-weight: bold; border-radius: 4px; min-width: 100px;");
            m_grid->addWidget(valueLabel, row, col * 2 + 1);

            m_valueLabels[sig.name] = valueLabel;

            col++;
            if (col >= maxCols) {
                col = 0;
                row++;
            }
        }
    }
}

void CanDashboard::updateSignal(const CanFrame &frame) {
    if (!m_dbcParser) return;

    const auto &signalNames = m_idToSignals.value(frame.id);
    if (signalNames.isEmpty()) return;

    DbcMessage msg = m_dbcParser->messageForId(frame.id);
    if (msg.id == 0) return;

    // Dla każdego sygnału w tej wiadomości (uproszczona ekstrakcja, bajt po bajcie)
    // W docelowej wersji można użyć zaawansowanej ekstrakcji bitowej
    for (const auto &sig : msg.sigList) {
        int byteIdx = sig.startBit / 8;
        if (byteIdx < frame.dlc) {
            uint8_t rawValue = frame.data[byteIdx];
            double value = rawValue * sig.scale + sig.offset;
            QLabel *label = m_valueLabels.value(sig.name);
            if (label) {
                label->setText(QString("%1 %2").arg(value, 0, 'f', 1).arg(sig.unit));
            }
        }
    }
}
