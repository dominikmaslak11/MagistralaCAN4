#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include "DoIpFrame.h"

class DoIpWidget : public QWidget {
    Q_OBJECT
public:
    explicit DoIpWidget(QWidget *parent = nullptr);

public slots:
    void processPayload(const QByteArray &data);
    void clearMessages();
    void parseManualInput();

private:
    void addMessage(const DoIpMessage &msg);
    void updateDetails(int row);

    QTableWidget *m_msgTable;
    QTextEdit    *m_detailView;
    QLineEdit    *m_hexInput;
    QPushButton  *m_parseBtn;
    QPushButton  *m_clearBtn;
    QLabel       *m_statusLabel;

    std::vector<DoIpMessage> m_messages;
};
