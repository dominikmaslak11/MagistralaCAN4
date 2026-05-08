#pragma once
#include <QWidget>
#include <QTableView>
#include <QLabel>
#include <QPushButton>
#include <QAbstractTableModel>
#include <QVector>
#include "CanOpenFrame.h"

class CanOpenTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col { TIME, CAN_ID, NODE, TYPE, TYPE_NAME, DETAIL, DATA, _COUNT };
    explicit CanOpenTableModel(QObject *p = nullptr);
    int rowCount(const QModelIndex & = QModelIndex()) const override { return m_frames.size(); }
    int columnCount(const QModelIndex & = QModelIndex()) const override { return _COUNT; }
    QVariant data(const QModelIndex &idx, int role) const override;
    QVariant headerData(int s, Qt::Orientation o, int role) const override;
    void addFrame(const CanOpenFrame &f);
    void clear();
    void setParser(const CanOpenParser *p) { m_parser = p; }
private:
    QVector<CanOpenFrame> m_frames;
    const CanOpenParser *m_parser = nullptr;
    static constexpr int MAX = 2000;
};

class CanOpenWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanOpenWidget(QWidget *parent = nullptr);
public slots:
    void processFrame(const CanFrame &frame);
private:
    void setupUi();
    CanOpenParser m_parser;
    CanOpenTableModel *m_model;
    QTableView *m_table;
    QLabel *m_statusLabel;
    QPushButton *m_clearBtn;
};
