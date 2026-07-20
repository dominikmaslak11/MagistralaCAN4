#pragma once
#include "LinParser.h"
#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QGroupBox>
#include <QSpinBox>
#include <QCheckBox>

/// Qt widget for live LIN bus frame display and manual parsing.
/// Shows received frames in a table with PID, checksum type, data, and validity status.
class LinWidget : public QWidget {
    Q_OBJECT
public:
    explicit LinWidget(QWidget *parent = nullptr);

public slots:
    /// Feed a raw LIN byte stream into the reassembly buffer and display decoded frames.
    void feedBytes(const QByteArray &raw);
    /// Parse and display a single pre-framed byte sequence (from manual input).
    void parseManual();
    void clear();

signals:
    void frameDecoded(LinFrame frame);

private:
    void buildUi();
    void appendFrame(const LinFrame &f);
    QString dataHex(const LinFrame &f) const;

    LinParser m_parser;

    // Manual input
    QLineEdit   *m_hexInput{};
    QComboBox   *m_csTypeCombo{};
    QPushButton *m_parseBtn{};
    QPushButton *m_clearBtn{};

    // Frame table
    QTableWidget *m_table{};
    QLabel       *m_statsLabel{};

    int m_totalFrames  = 0;
    int m_invalidCount = 0;

    static constexpr int kMaxRows = 500;
};
