#pragma once
#include <QWidget>
#include <QTableView>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QAbstractTableModel>
#include <QVector>
#include "ObdFrame.h"

class ObdTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col { TIME, CAN_ID, DIR, MODE, MODE_NAME, PID, PID_NAME, VALUE, _COUNT };
    explicit ObdTableModel(QObject *p = nullptr);
    int rowCount(const QModelIndex & = QModelIndex()) const override { return m_frames.size(); }
    int columnCount(const QModelIndex & = QModelIndex()) const override { return _COUNT; }
    QVariant data(const QModelIndex &idx, int role) const override;
    QVariant headerData(int s, Qt::Orientation o, int role) const override;
    void addFrame(const ObdFrame &f);
    void clear();
    void setParser(const ObdParser *p) { m_parser = p; }
private:
    QVector<ObdFrame> m_frames;
    const ObdParser *m_parser = nullptr;
    static constexpr int MAX = 2000;
};

class ObdWidget : public QWidget {
    Q_OBJECT
public:
    explicit ObdWidget(QWidget *parent = nullptr);
public slots:
    void processFrame(const CanFrame &frame);
private:
    void setupUi();
    ObdParser m_parser;
    ObdTableModel *m_model;
    QTableView *m_table;
    QLabel *m_statusLabel;
    QPushButton *m_clearBtn;
};
