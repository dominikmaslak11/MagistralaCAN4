#pragma once
#include <QWidget>
#include <QTableView>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QTimer>
#include <QAbstractTableModel>
#include <QVector>
#include "UdsFrame.h"
#include "UdsParser.h"
#include "CanTpLayer.h"

class UdsTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col { TIME, CAN_ID, DIR, SID, SVC_NAME, DID, SUBFUNC, STATUS, DATA, Count };
    explicit UdsTableModel(QObject *p = nullptr);

    int rowCount(const QModelIndex & = QModelIndex()) const override { return static_cast<int>(m_frames.size()); }
    int columnCount(const QModelIndex & = QModelIndex()) const override { return Count; }
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

private slots:
    void purgeTimedOutSessions();

private:
    void setupUi();
    void onTpMessage(const CanTpMessage &msg);

    UdsParser     m_parser;
    CanTpLayer    m_tpLayer;
    QTimer        m_purgeTimer;
    UdsTableModel *m_model      = nullptr;
    QTableView    *m_table      = nullptr;
    QComboBox     *m_sidFilter  = nullptr;
    QCheckBox     *m_filterEnabled = nullptr;
    QPushButton   *m_clearBtn   = nullptr;
    QLabel        *m_statusLabel = nullptr;
    QLabel        *m_detailLabel = nullptr;
};
