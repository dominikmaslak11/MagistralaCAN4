#pragma once
#include <QSortFilterProxyModel>
#include <cstdint>

/**
 * @brief Proxy filtrująco-sortujący dla CanFrameModel.
 *
 * Zastępuje ręczne setRowHidden() i CanFrameModel::sort().
 * Filtruje wg CAN ID (hex), sortuje przez Qt::UserRole.
 */
class CanFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit CanFilterProxy(QObject *parent = nullptr);

    /// Ustaw filtr CAN ID (hex, np. "123" lub "0x123"). Pusty tekst = brak filtra.
    void setIdFilter(const QString &text);

    /// Czy filtr jest aktywny.
    bool isFilterActive() const { return m_filterActive; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    bool m_filterActive = false;
    uint32_t m_filterId = 0;
};
