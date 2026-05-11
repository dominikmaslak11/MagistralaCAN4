#include "FrameDetailWidget.h"
#include "CanSniffer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>

FrameDetailWidget::FrameDetailWidget(QWidget *parent) : QWidget(parent) {
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0,0,0,0);

    auto *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    auto *scrollWidget = new QWidget;
    auto *layout = new QVBoxLayout(scrollWidget);

    m_idLabel = new QLabel("ID: —");
    m_idLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #00ffaa;");
    m_dlcLabel = new QLabel("DLC: —");
    m_dlcLabel->setStyleSheet("font-size: 14px; color: #ff66cc;");
    m_timestampLabel = new QLabel("Czas: —");
    m_timestampLabel->setStyleSheet("font-size: 14px; color: #c0c0c0;");
    m_signalLabel = new QLabel("Sygnały: —");
    m_signalLabel->setStyleSheet("font-size: 13px; color: #ffaa00; padding: 4px;");

    layout->addWidget(m_idLabel);
    layout->addWidget(m_dlcLabel);
    layout->addWidget(m_timestampLabel);
    layout->addWidget(m_signalLabel);

    m_grid = new QGridLayout;
    m_grid->setSpacing(2);

    for (int bit = 7; bit >= 0; --bit) {
        auto *header = new QLabel(QString("Bit %1").arg(bit));
        header->setAlignment(Qt::AlignCenter);
        header->setStyleSheet("font-weight: bold; color: #ffaa00; background-color: #1a1a2e;");
        m_grid->addWidget(header, 0, 8 - bit);
    }

    buildGrid();

    layout->addLayout(m_grid);
    layout->addStretch();

    // Przycisk wysyłania
    auto *btnLayout = new QHBoxLayout;
    m_sendBtn = new QPushButton("🚀 Wyślij zmodyfikowaną ramkę");
    m_sendBtn->setStyleSheet("background: #e94560; color: #ffffff; font-weight: bold; padding: 8px 15px; border-radius: 4px;");
    connect(m_sendBtn, &QPushButton::clicked, this, &FrameDetailWidget::sendModifiedFrame);
    btnLayout->addStretch();
    btnLayout->addWidget(m_sendBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    scrollArea->setWidget(scrollWidget);
    outerLayout->addWidget(scrollArea);

    setStyleSheet(R"(
        QLabel {
            font-family: "Consolas", "Courier New", monospace;
            padding: 2px 4px;
        }
    )");
}

void FrameDetailWidget::setDbcParser(DbcParser *parser) {
    m_dbcParser = parser;
}

void FrameDetailWidget::setSniffer(CanSniffer *sniffer) {
    m_sniffer = sniffer;
}

void FrameDetailWidget::buildGrid() {
    for (auto *lbl : m_byteLabels) delete lbl;
    for (auto &row : m_bitLabels) for (auto *lbl : row) delete lbl;
    m_byteLabels.clear();
    m_bitLabels.clear();

    for (int byte = 0; byte < 8; ++byte) {
        int row = byte + 1;

        // Etykieta bajtu – klikalna
        auto *byteLabel = new QLabel("00");
        byteLabel->setAlignment(Qt::AlignCenter);
        byteLabel->setStyleSheet("background-color: #161b22; color: #00ffaa; font-weight: bold;");
        byteLabel->installEventFilter(this);
        byteLabel->setProperty("byteIndex", byte);
        byteLabel->setCursor(Qt::PointingHandCursor);
        m_grid->addWidget(byteLabel, row, 0);
        m_byteLabels.append(byteLabel);

        // Etykiety bitów – klikalne
        QVector<QLabel*> bitRow;
        for (int bit = 7; bit >= 0; --bit) {
            auto *bitLabel = new QLabel("0");
            bitLabel->setAlignment(Qt::AlignCenter);
            bitLabel->setStyleSheet("background-color: #0d1117; color: #c0c0c0;");
            bitLabel->installEventFilter(this);
            bitLabel->setProperty("byteIndex", byte);
            bitLabel->setProperty("bitIndex", bit);
            bitLabel->setCursor(Qt::PointingHandCursor);
            m_grid->addWidget(bitLabel, row, 8 - bit);
            bitRow.append(bitLabel);
        }
        m_bitLabels.append(bitRow);
    }
}

void FrameDetailWidget::onByteClicked(int byteIndex) {
    if (byteIndex >= m_currentFrame.dlc) return;
    bool ok;
    QString newVal = QInputDialog::getText(this, "Edytuj bajt",
                                            QString("Nowa wartość hex dla bajtu %1:").arg(byteIndex),
                                            QLineEdit::Normal,
                                            QString("%1").arg(m_currentFrame.data[byteIndex], 2, 16, QChar('0')).toUpper(),
                                            &ok);
    if (ok && !newVal.isEmpty()) {
        uint8_t val = newVal.toUInt(&ok, 16);
        if (ok) {
            m_currentFrame.data[byteIndex] = val;
            updateByteDisplay(byteIndex);
            for (int bit = 0; bit < 8; ++bit)
                updateBitDisplay(byteIndex, bit);
        }
    }
}

void FrameDetailWidget::onBitClicked(int byteIndex, int bitIndex) {
    if (byteIndex >= m_currentFrame.dlc) return;
    m_currentFrame.data[byteIndex] ^= (1 << (7 - bitIndex)); // przełącz bit
    updateByteDisplay(byteIndex);
    updateBitDisplay(byteIndex, bitIndex);
}

void FrameDetailWidget::sendModifiedFrame() {
    if (!m_sniffer) {
        QMessageBox::warning(this, "Brak sniffera", "Sniffer nie jest podlaczony.");
        return;
    }
    // Auto-open driver if needed (for manual frame sending without live sniffing)
    if (!m_sniffer->isSocketValid()) {
        m_sniffer->start("PCAN_USBBUS1");  // uses default, or last interface from QSettings
    }
    if (!m_sniffer->isSocketValid()) {
        QMessageBox::warning(this, "Blad CAN", "Nie mozna otworzyc sterownika CAN.\nUruchom sniffing w zakladce Ruch CAN.");
        return;
    }
    m_sniffer->writeFrame(m_currentFrame);
}

void FrameDetailWidget::updateByteDisplay(int byte) {
    uint8_t value = (byte < m_currentFrame.dlc) ? m_currentFrame.data[byte] : 0;
    m_byteLabels[byte]->setText(QString("%1").arg(value, 2, 16, QChar('0')).toUpper());
}

void FrameDetailWidget::updateBitDisplay(int byte, int bit) {
    uint8_t value = (byte < m_currentFrame.dlc) ? m_currentFrame.data[byte] : 0;
    bool bitVal = (value >> (7 - bit)) & 1;
    m_bitLabels[byte][bit]->setText(bitVal ? "1" : "0");
}

// EventFilter do obsługi kliknięć
bool FrameDetailWidget::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::MouseButtonPress) {
        QLabel *label = qobject_cast<QLabel*>(obj);
        if (label) {
            bool ok;
            int byteIdx = label->property("byteIndex").toInt(&ok);
            if (ok) {
                if (label->property("bitIndex").isValid()) {
                    int bitIdx = label->property("bitIndex").toInt();
                    onBitClicked(byteIdx, bitIdx);
                } else {
                    onByteClicked(byteIdx);
                }
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ... reszta metod jak poprzednio (highlightChangedBits, loadFrame)
void FrameDetailWidget::highlightChangedBits(const CanFrame &frame) {
    auto it = m_lastFrameMap.find(frame.id);
    if (it != m_lastFrameMap.end()) {
        const CanFrame &prev = it.value();
        for (int byte = 0; byte < 8; ++byte) {
            uint8_t currVal = (byte < frame.dlc) ? frame.data[byte] : 0;
            uint8_t prevVal = (byte < prev.dlc) ? prev.data[byte] : 0;

            if (currVal != prevVal) {
                m_byteLabels[byte]->setStyleSheet("background-color: #e94560; color: #ffffff; font-weight: bold;");
            }

            for (int bit = 0; bit < 8; ++bit) {
                bool currBit = (currVal >> (7 - bit)) & 1;
                bool prevBit = (prevVal >> (7 - bit)) & 1;
                if (currBit != prevBit) {
                    m_bitLabels[byte][bit]->setStyleSheet("background-color: #ff66cc; color: #ffffff; font-weight: bold;");
                }
            }
        }
    }
    m_lastFrameMap[frame.id] = frame;
}

QString FrameDetailWidget::byteToBinary(uint8_t value) const {
    QString bin;
    for (int i = 7; i >= 0; --i) {
        bin += (value & (1 << i)) ? '1' : '0';
    }
    return bin;
}

void FrameDetailWidget::loadFrame(const CanFrame &frame) {
    m_currentFrame = frame; // zapisz kopię do edycji
    m_idLabel->setText(QString("ID: 0x%1").arg(frame.id, 3, 16, QChar('0')).toUpper());
    m_dlcLabel->setText(QString("DLC: %1").arg(frame.dlc));
    m_timestampLabel->setText(QString("Czas: %1 µs").arg(frame.timestamp));

    if (m_dbcParser) {
        QString desc = m_dbcParser->signalDescriptions(frame.id, frame.data.data(), frame.dlc);
        m_signalLabel->setText(desc.isEmpty() ? "Sygnały: (brak w DBC)" : ("Sygnały: " + desc));
    } else {
        m_signalLabel->setText("Sygnały: (DBC nie wczytane)");
    }

    for (int byte = 0; byte < 8; ++byte) {
        m_byteLabels[byte]->setStyleSheet("background-color: #161b22; color: #00ffaa; font-weight: bold;");
        for (int bit = 0; bit < 8; ++bit)
            m_bitLabels[byte][bit]->setStyleSheet("background-color: #0d1117; color: #c0c0c0;");
    }

    highlightChangedBits(frame);

    for (int byte = 0; byte < 8; ++byte) {
        uint8_t value = (byte < frame.dlc) ? frame.data[byte] : 0;
        updateByteDisplay(byte);
        QString bin = byteToBinary(value);
        for (int bit = 7; bit >= 0; --bit)
            m_bitLabels[byte][7 - bit]->setText(QString(bin[7 - bit]));
    }
}
