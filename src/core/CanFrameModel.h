#pragma once
#include <QAbstractTableModel>
#include <QVector>
#include <QMutex>
#include "CanFrame.h"

class DbcParser;
class J1939Parser;

class CanFrameModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ID, EXT, RTR, DLC, DATA, TIMESTAMP, FD, DELTA, SIGNAL, Count
    };

    /// Niestandardowa rola: bitmask zmienionych bajtów (64-bit, bit i = bajt i się zmienił)
    static constexpr int ChangedMaskRole = Qt::UserRole + 1;
    /// Niestandardowa rola: czy ramka jest burstem (do heatmap scrollbar)
    static constexpr int BurstRole = Qt::UserRole + 2;

    explicit CanFrameModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void processIncomingFrames(const QVector<CanFrame> &newFrames);
    void setOverwriteMode(bool enabled);
    void clear();
    CanFrame frameAt(int row) const;
    void setDbcParser(const DbcParser *dbc) { m_dbc = dbc; }
    void setJ1939Parser(const J1939Parser *j1939) { m_j1939 = j1939; }
    void setMaxFrames(int max) { m_maxFrames = max; }

    // NOWE: dostęp do wszystkich ramek (dla eksportu)
    QVector<CanFrame> allFrames() const;

    /// Unieważnia cache dla danego wiersza (wywołaj po zmianie danych ramki).
    void invalidateRowCache(int row);

    // ── Undo/Redo ──
    /// Czy jest dostępna operacja cofnij.
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    /// Czy jest dostępna operacja przywróć.
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    /// Cofa ostatnią operację niszczącą (clear, zmiana trybu nadpisywania).
    void undo();
    /// Przywraca cofniętą operację.
    void redo();

signals:
    /// Emitowany po każdej partii ramek — do WebSocket broadcast (batch JSON).
    void frameBatchUpdated(const QVector<CanFrame> &frames);

private:
    /// Cache tekstów DisplayRole — unika powtarzalnego formatowania hex i lookupów DBC.
    struct DisplayCache {
        QString id, ext, rtr, dlc, data, timestamp, fd, delta, signal;
        bool valid = false;
    };

    /// Dla podświetlania zmian: przechowuje poprzednie dane dla każdego ID.
    QHash<uint32_t, QVector<uint8_t>> m_previousData;
    /// Do obliczania delty: ostatni timestamp per ID.
    QHash<uint32_t, uint64_t> m_lastTimestampPerId;
    /// Delta dla każdej ramki (równoległa do m_frames).
    QVector<uint64_t> m_deltas;
    /// Burst detection: ostatni timestamp per ID.
    QHash<uint32_t, uint64_t> m_lastBurstTs;
    /// Czy ramka jest burstem.
    QVector<bool> m_isBurst;
    mutable QMutex m_mutex;
    QVector<CanFrame> m_frames;
    mutable QVector<DisplayCache> m_cache;  // cache DisplayRole, równoległy do m_frames
    QHash<uint32_t, int> m_idToRow;
    bool m_overwrite = true;
    int  m_maxFrames = 10000;  // ograniczone przy 2048B/frame (~20MB max)
    int  m_head = 0;           // fizyczny indeks logicznego wiersza 0 (bufor cykliczny)
    int  m_size = 0;           // bieżąca liczba ramek (≤ m_maxFrames)

    /// Mapuje wiersz logiczny na fizyczny indeks w buforze cyklicznym.
    inline int physRow(int logicalRow) const {
        return (m_head + logicalRow) % m_maxFrames;
    }

    // ── Undo/Redo ──
    static constexpr int MAX_UNDO = 5;

    struct Snapshot {
        QVector<CanFrame> frames;
        QVector<uint64_t> deltas;
        QVector<bool> isBurst;
        QHash<uint32_t, int> idToRow;
        QHash<uint32_t, QVector<uint8_t>> previousData;
        QHash<uint32_t, uint64_t> lastTimestampPerId;
        QHash<uint32_t, uint64_t> lastBurstTs;
        int head = 0;
        int size = 0;
    };
    QVector<Snapshot> m_undoStack;
    QVector<Snapshot> m_redoStack;

    void saveUndoState();
    void restoreSnapshot(const Snapshot &snap);

    const DbcParser   *m_dbc   = nullptr;
    const J1939Parser *m_j1939 = nullptr;
};
