#pragma once
#include <QWidget>
#include <QLabel>
#include <QGridLayout>
#include <QScrollArea>
#include <QHash>
#include "CanFrame.h"

class FrameDetailWidget : public QWidget {
    Q_OBJECT
public:
    explicit FrameDetailWidget(QWidget *parent = nullptr);
    void loadFrame(const CanFrame &frame);

private:
    void buildGrid();
    QLabel* createByteLabel(int byteIndex);
    QString byteToBinary(uint8_t value) const;
    void highlightChangedBits(const CanFrame &frame);

    QGridLayout *m_grid;
    QLabel *m_idLabel;
    QLabel *m_dlcLabel;
    QLabel *m_timestampLabel;
    QVector<QLabel*> m_byteLabels;      // etykiety bajtów (hex)
    QVector<QVector<QLabel*>> m_bitLabels;  // etykiety bitów (8 bajtów × 8 bitów)
    QHash<uint32_t, CanFrame> m_lastFrameMap; // ostatnia ramka dla każdego ID
    uint32_t m_currentId = 0xFFFFFFFF;
};
