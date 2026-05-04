#pragma once
#include <QWidget>
#include <QLabel>
#include <QGridLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QEvent>
#include <QHash>
#include "CanFrame.h"
#include "DbcParser.h"

class CanSniffer; // forward declaration

class FrameDetailWidget : public QWidget {
    Q_OBJECT
public:
    explicit FrameDetailWidget(QWidget *parent = nullptr);
    void loadFrame(const CanFrame &frame);
    void setDbcParser(DbcParser *parser);
    void setSniffer(CanSniffer *sniffer);   // do wysyłania ramek

private slots:
    void onByteClicked(int byteIndex);
    void onBitClicked(int byteIndex, int bitIndex);
    void sendModifiedFrame();

private:
    void buildGrid();
    QString byteToBinary(uint8_t value) const;
    void highlightChangedBits(const CanFrame &frame);
    void updateByteDisplay(int byte);
    void updateBitDisplay(int byte, int bit);
    bool eventFilter(QObject *obj, QEvent *event) override;

    QGridLayout *m_grid;
    QLabel *m_idLabel;
    QLabel *m_dlcLabel;
    QLabel *m_timestampLabel;
    QLabel *m_signalLabel;
    QPushButton *m_sendBtn;

    QVector<QLabel*> m_byteLabels;
    QVector<QVector<QLabel*>> m_bitLabels;
    QHash<uint32_t, CanFrame> m_lastFrameMap;
    DbcParser *m_dbcParser = nullptr;
    CanSniffer *m_sniffer = nullptr;

    // Bieżąca, edytowalna kopia ramki
    CanFrame m_currentFrame;
};
