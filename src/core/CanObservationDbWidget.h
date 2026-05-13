#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QSpinBox>
#include "CanObservationDb.h"
#include "CanFrame.h"

class CanObservationDbWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanObservationDbWidget(QWidget *parent = nullptr);

public slots:
    void onFrame(const CanFrame &frame, uint64_t timestampUs = 0);

private slots:
    void openDb();
    void closeDb();
    void newSession();
    void queryById();
    void resetDb();
    void refreshStats();

private:
    void buildUi();
    void setConnected(bool connected);

    CanObservationDb m_db;

    QLineEdit    *m_pathEdit;
    QPushButton  *m_openBtn;
    QPushButton  *m_closeBtn;
    QPushButton  *m_newSessionBtn;
    QPushButton  *m_resetBtn;
    QLineEdit    *m_filterIdEdit;
    QSpinBox     *m_limitSpin;
    QPushButton  *m_queryBtn;
    QTableWidget *m_resultTable;
    QLabel       *m_statsLabel;
};
