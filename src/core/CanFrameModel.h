#pragma once
#include <QAbstractTableModel>
#include <QVector>
#include <QMutex>
#include "CanFrame.h"

class CanFrameModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ID, EXT, RTR, DLC, DATA, TIMESTAMP, FD, DELTA, _COUNT
    };

    explicit CanFrameModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void processIncomingFrames(const QVector<CanFrame> &newFrames);
    void setOverwriteMode(bool enabled);
    void clear();
    CanFrame frameAt(int row) const;

    // NOWE: dostęp do wszystkich ramek (dla eksportu)
    QVector<CanFrame> allFrames() const;

signals:
    void frameUpdated(const CanFrame &frame);

private:
    /// Dla podświetlania zmian: przechowuje poprzednie dane dla każdego ID.
    QHash<uint32_t, QVector<uint8_t>> m_previousData;
    /// Do obliczania delty: ostatni timestamp per ID.
    QHash<uint32_t, uint64_t> m_lastTimestampPerId;
    /// Delta dla każdej ramki (równoległa do m_frames).
    QVector<uint64_t> m_deltas;
    mutable QMutex m_mutex;
    QVector<CanFrame> m_frames;
    QHash<uint32_t, int> m_idToRow;
    bool m_overwrite = true;
};
