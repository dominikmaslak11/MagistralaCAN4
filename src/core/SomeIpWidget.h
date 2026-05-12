#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include "SomeIpFrame.h"

class SomeIpWidget : public QWidget {
    Q_OBJECT
public:
    explicit SomeIpWidget(QWidget *parent = nullptr);

public slots:
    // Feed raw payload bytes (e.g. from CAN TP reassembly or Ethernet capture).
    void processPayload(const QByteArray &data);
    void clearMessages();
    void parseManualInput();

private:
    void updateDetails(int row);
    void addMessage(const SomeIpMessage &msg);

    QTableWidget *m_msgTable;
    QTextEdit    *m_detailView;
    QLineEdit    *m_hexInput;
    QPushButton  *m_parseBtn;
    QPushButton  *m_clearBtn;
    QLabel       *m_statusLabel;

    std::vector<SomeIpMessage> m_messages;
};
