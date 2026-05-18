#pragma once
#include <QAbstractTableModel>
#include <QVector>
#include <QString>
#include <limits>
#include <cstdint>
#include "CanFrame.h"

struct MapperSignal {
    QString  name;
    uint32_t canId          = 0;
    int      startBit       = 0;
    int      length         = 8;
    bool     isLittleEndian = true;
    bool     isSigned       = false;
    double   scale          = 1.0;
    double   offset         = 0.0;
    QString  unit;
    double   liveValue      = std::numeric_limits<double>::quiet_NaN();
};

class CanSignalMapperModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColName = 0, ColId, ColStartBit, ColLength,
        ColEndian, ColSigned, ColScale, ColOffset, ColUnit, ColLive,
        ColCount
    };

    explicit CanSignalMapperModel(QObject *parent = nullptr);

    int      rowCount   (const QModelIndex & = {}) const override;
    int      columnCount(const QModelIndex & = {}) const override;
    QVariant data       (const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags (const QModelIndex &index) const override;
    bool setData        (const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    void addSignal   (const MapperSignal &sig);
    void removeSignal(int row);
    void clearSignals();

    const MapperSignal &signalAt(int row) const { return m_signals[row]; }

    bool saveToFile  (const QString &path) const;
    bool loadFromFile(const QString &path);

public slots:
    void processFrame(const CanFrame &frame);

private:
    QVector<MapperSignal> m_signals;
};
