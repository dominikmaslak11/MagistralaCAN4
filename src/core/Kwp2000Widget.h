#pragma once
#include "Kwp2000Parser.h"
#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>

/// Widget for decoding and displaying KWP2000 / ISO 14230 diagnostic frames.
class Kwp2000Widget : public QWidget {
    Q_OBJECT
public:
    explicit Kwp2000Widget(QWidget *parent = nullptr);

public slots:
    void clear();

private slots:
    void parseManual();

private:
    void buildUi();
    void appendFrame(const Kwp2000Frame &f, const QString &raw);
    QString payloadHex(const std::vector<uint8_t> &v) const;

    QLineEdit    *m_hexInput;
    QPushButton  *m_parseBtn;
    QPushButton  *m_clearBtn;
    QTableWidget *m_table;
    QLabel       *m_statsLabel;

    int m_total = 0, m_errors = 0;
};
