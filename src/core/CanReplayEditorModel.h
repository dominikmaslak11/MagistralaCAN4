#pragma once
#include <QAbstractTableModel>
#include "CanPlayer.h"
#include "CanFrame.h"

/// Model tabeli dla edytora replay.
/// Każdy wiersz odpowiada jednej ramce z załadowanego pliku .mcan.
/// Kolumny: # | Aktywna | Timestamp | ID | EXT | DLC | Payload
class CanReplayEditorModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColNum = 0,
        ColEnabled,
        ColTimestamp,
        ColId,
        ColExt,
        ColDlc,
        ColPayload,
        ColCount
    };

    explicit CanReplayEditorModel(CanPlayer *player, QObject *parent = nullptr);

    /// Przeładowuje model po loadFile() na playerze.
    void reload();

    /// Resetuje wszystkie edycje (overrides + skip) do stanu oryginalnego.
    void resetAllEdits();

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /// Zwraca efektywną ramkę dla wiersza (override jeśli istnieje, inaczej oryginał).
    CanFrame effectiveFrame(int row) const;

    bool isModified(int row) const;

private:
    CanPlayer *m_player;
    int        m_rowCount = 0;

    QString payloadHex(const CanFrame &f) const;
    bool parsePayloadHex(const QString &hex, CanFrame &f) const;
};
