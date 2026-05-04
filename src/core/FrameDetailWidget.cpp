#include "FrameDetailWidget.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QFont>
#include <QDebug>

FrameDetailWidget::FrameDetailWidget(QWidget *parent) : QWidget(parent) {
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0,0,0,0);

    auto *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; }");
    auto *scrollWidget = new QWidget;
    auto *layout = new QVBoxLayout(scrollWidget);

    // Sekcja identyfikatora
    m_idLabel = new QLabel("ID: —");
    m_idLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #00ffaa;");
    m_dlcLabel = new QLabel("DLC: —");
    m_dlcLabel->setStyleSheet("font-size: 14px; color: #ff66cc;");
    m_timestampLabel = new QLabel("Czas: —");
    m_timestampLabel->setStyleSheet("font-size: 14px; color: #c0c0c0;");

    layout->addWidget(m_idLabel);
    layout->addWidget(m_dlcLabel);
    layout->addWidget(m_timestampLabel);

    // Siatka bitów – przewijalna
    m_grid = new QGridLayout;
    m_grid->setSpacing(2);

    // Nagłówki kolumn (bity 7..0)
    for (int bit = 7; bit >= 0; --bit) {
        auto *header = new QLabel(QString("Bit %1").arg(bit));
        header->setAlignment(Qt::AlignCenter);
        header->setStyleSheet("font-weight: bold; color: #ffaa00; background-color: #1a1a2e;");
        m_grid->addWidget(header, 0, 8 - bit); // wiersz 0
    }

    buildGrid();

    layout->addLayout(m_grid);
    layout->addStretch();

    scrollArea->setWidget(scrollWidget);
    outerLayout->addWidget(scrollArea);

    setStyleSheet(R"(
        QLabel {
            font-family: "Consolas", "Courier New", monospace;
            padding: 2px 4px;
        }
    )");
}

void FrameDetailWidget::buildGrid() {
    // Czyścimy stare etykiety (jeśli istnieją)
    for (auto *lbl : m_byteLabels) delete lbl;
    for (auto &row : m_bitLabels) for (auto *lbl : row) delete lbl;
    m_byteLabels.clear();
    m_bitLabels.clear();

    // Tworzymy 8 wierszy dla bajtów 0..7 (klasyczny CAN)
    for (int byte = 0; byte < 8; ++byte) {
        int row = byte + 1; // wiersz 1..8

        // Etykieta bajtu
        auto *byteLabel = new QLabel("00");
        byteLabel->setAlignment(Qt::AlignCenter);
        byteLabel->setStyleSheet("background-color: #161b22; color: #00ffaa; font-weight: bold;");
        m_grid->addWidget(byteLabel, row, 0);
        m_byteLabels.append(byteLabel);

        // Etykiety bitów
        QVector<QLabel*> bitRow;
        for (int bit = 7; bit >= 0; --bit) {
            auto *bitLabel = new QLabel("0");
            bitLabel->setAlignment(Qt::AlignCenter);
            bitLabel->setStyleSheet("background-color: #0d1117; color: #c0c0c0;");
            m_grid->addWidget(bitLabel, row, 8 - bit); // kolumny 1..8
            bitRow.append(bitLabel);
        }
        m_bitLabels.append(bitRow);
    }
}

QLabel* FrameDetailWidget::createByteLabel(int byteIndex) {
    auto *lbl = new QLabel("00");
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setStyleSheet("background-color: #161b22; color: #00ffaa; font-weight: bold;");
    return lbl;
}

QString FrameDetailWidget::byteToBinary(uint8_t value) const {
    QString bin;
    for (int i = 7; i >= 0; --i) {
        bin += (value & (1 << i)) ? '1' : '0';
    }
    return bin;
}

void FrameDetailWidget::highlightChangedBits(const CanFrame &frame) {
    // Pobierz poprzednią ramkę dla tego ID
    auto it = m_lastFrameMap.find(frame.id);
    if (it != m_lastFrameMap.end()) {
        const CanFrame &prev = it.value();
        for (int byte = 0; byte < 8; ++byte) {
            uint8_t currVal = (byte < frame.dlc) ? frame.data[byte] : 0;
            uint8_t prevVal = (byte < prev.dlc) ? prev.data[byte] : 0;

            // Podświetl bajt, jeśli się zmienił
            if (currVal != prevVal) {
                m_byteLabels[byte]->setStyleSheet("background-color: #e94560; color: #ffffff; font-weight: bold;");
            }

            // Podświetl pojedyncze bity
            for (int bit = 0; bit < 8; ++bit) {
                bool currBit = (currVal >> (7 - bit)) & 1;
                bool prevBit = (prevVal >> (7 - bit)) & 1;
                if (currBit != prevBit) {
                    m_bitLabels[byte][bit]->setStyleSheet("background-color: #ff66cc; color: #ffffff; font-weight: bold;");
                }
            }
        }
    }

    // Zapamiętaj bieżącą ramkę
    m_lastFrameMap[frame.id] = frame;
}

void FrameDetailWidget::loadFrame(const CanFrame &frame) {
    // Aktualizuj nagłówek
    m_idLabel->setText(QString("ID: 0x%1").arg(frame.id, 3, 16, QChar('0')).toUpper());
    m_dlcLabel->setText(QString("DLC: %1").arg(frame.dlc));
    m_timestampLabel->setText(QString("Czas: %1 µs").arg(frame.timestamp));

    // Resetuj style przed aktualizacją
    for (int byte = 0; byte < 8; ++byte) {
        m_byteLabels[byte]->setStyleSheet("background-color: #161b22; color: #00ffaa; font-weight: bold;");
        for (int bit = 0; bit < 8; ++bit) {
            m_bitLabels[byte][bit]->setStyleSheet("background-color: #0d1117; color: #c0c0c0;");
        }
    }

    // Podświetl zmiany (robi to przed zapisaniem nowej ramki!)
    highlightChangedBits(frame);

    // Wypełnij bajty i bity
    for (int byte = 0; byte < 8; ++byte) {
        uint8_t value = (byte < frame.dlc) ? frame.data[byte] : 0;
        m_byteLabels[byte]->setText(QString("%1").arg(value, 2, 16, QChar('0')).toUpper());

        QString bin = byteToBinary(value);
        for (int bit = 7; bit >= 0; --bit) {
            m_bitLabels[byte][7 - bit]->setText(QString(bin[7 - bit]));
        }
    }
}
