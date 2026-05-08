#pragma once
#include <QWidget>
#include <QTableView>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QAbstractTableModel>
#include <QVector>
#include "UdsFrame.h"
#include "UdsParser.h"

class UdsTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col { TIME, CAN_ID, DIR, SID, SVC_NAME, DID, SUBFUNC, STATUS, DATA, _COUNT };
    explicit UdsTableModel(QObject *p = nullptr);

    int rowCount(const QModelIndex & = QModelIndex()) const override { return m_frames.size(); }
    int columnCount(const QModelIndex & = QModelIndex()) const override { return _COUNT; }
    QVariant data(const QModelIndex &idx, int role) const override;
    QVariant headerData(int s, Qt::Orientation o, int role) const override;

    void addFrame(const UdsFrame &f);
    void clear();
    UdsFrame frameAt(int row) const;
    void setParser(const UdsParser *p) { m_parser = p; }

private:
    QVector<UdsFrame> m_frames;
    const UdsParser *m_parser = nullptr;
    static constexpr int MAX = 5000;
};

class UdsWidget : public QWidget {
    Q_OBJECT
public:
    explicit UdsWidget(QWidget *parent = nullptr);

public slots:
    void processFrame(const CanFrame &frame);
    void onRowSelected(const QModelIndex &idx);

private:
    void setupUi();

    UdsParser m_parser;
    UdsTableModel *m_model;
    QTableView *m_table;
    QComboBox *m_sidFilter;
    QCheckBox *m_filterEnabled;
    QPushButton *m_clearBtn;
    QLabel *m_statusLabel;
    QLabel *m_detailLabel;
};
