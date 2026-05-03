#pragma once
#include <QAbstractTableModel>
#include <QVector>
#include <QMutex>
#include <QTimer>
#include "CanFrame.h"

class CanFrameModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ID, EXT, RTR, DLC, DATA, TIMESTAMP, _COUNT
    };

    explicit CanFrameModel(QObject *parent = nullptr);

    // QAbstractTableModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Slot do przyjmowania nowych ramek – wywoływany przez timer co 33ms
    void appendFrames(const QVector<CanFrame> &newFrames);

private:
    mutable QMutex m_mutex;
    QVector<CanFrame> m_frames;          // główny bufor modelu
    QVector<CanFrame> m_pendingFrames;   // ramki oczekujące na dodanie (poza wątkiem GUI)
};
