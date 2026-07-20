#pragma once
#include "XcpParser.h"
#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QComboBox>

/// Widget for decoding XCP/CCP-over-CAN packets.
class XcpWidget : public QWidget {
    Q_OBJECT
public:
    explicit XcpWidget(QWidget *parent = nullptr);

public slots:
    void clear();

private slots:
    void parseManual();

private:
    void buildUi();
    void appendFrame(const XcpFrame &f, const QString &raw);
    QString payloadHex(const std::vector<uint8_t> &v) const;

    QLineEdit    *m_hexInput{};
    QComboBox    *m_dirCombo{};
    QPushButton  *m_parseBtn{};
    QPushButton  *m_clearBtn{};
    QTableWidget *m_table{};
    QLabel       *m_statsLabel{};

    int m_total = 0, m_negCount = 0;
};
