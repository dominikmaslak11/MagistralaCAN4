#pragma once
#include <QWidget>
#include <QLabel>
#include <QGridLayout>
#include <QScrollArea>
#include <QHash>
#include "CanFrame.h"
#include "DbcParser.h"

class FrameDetailWidget : public QWidget {
    Q_OBJECT
public:
    explicit FrameDetailWidget(QWidget *parent = nullptr);
    void loadFrame(const CanFrame &frame);
    void setDbcParser(DbcParser *parser);   // NOWE

private:
    void buildGrid();
    QLabel* createByteLabel(int byteIndex);
    QString byteToBinary(uint8_t value) const;
    void highlightChangedBits(const CanFrame &frame);

    QGridLayout *m_grid;
    QLabel *m_idLabel;
    QLabel *m_dlcLabel;
    QLabel *m_timestampLabel;
    QLabel *m_signalLabel;                 // NOWE
    QVector<QLabel*> m_byteLabels;
    QVector<QVector<QLabel*>> m_bitLabels;
    QHash<uint32_t, CanFrame> m_lastFrameMap;
    DbcParser *m_dbcParser = nullptr;      // NOWE
};
