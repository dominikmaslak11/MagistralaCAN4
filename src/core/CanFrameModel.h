#pragma once
#include <QAbstractTableModel>
#include <QVector>
#include <QMutex>
#include <QTimer>
#include <QHash>
#include "CanFrame.h"

class CanFrameModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ID, EXT, RTR, DLC, DATA, TIMESTAMP, _COUNT
    };

    explicit CanFrameModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

public slots:
    // Odbiera batch ramek z wątku GUI
    void processIncomingFrames(const QVector<CanFrame> &newFrames);
    void setOverwriteMode(bool enabled);
    void clear();

signals:
    void frameUpdated(const CanFrame &frame);   // dla szczegółów ramki (później)

private:
    mutable QMutex m_mutex;
    QVector<CanFrame> m_frames;          // aktualny bufor ramek w modelu
    QHash<uint32_t, int> m_idToRow;      // mapowanie CAN ID -> indeks w m_frames (tylko gdy overwrite)
    bool m_overwrite = true;             // domyślnie nadpisuj
};
