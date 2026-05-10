#include "CanFilterProxy.h"
#include <QAbstractItemModel>

CanFilterProxy::CanFilterProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true); // re-filtruj przy zmianie danych
}

void CanFilterProxy::setIdFilter(const QString &text) {
    QString filter = text.trimmed();
    if (filter.startsWith("0x", Qt::CaseInsensitive))
        filter = filter.mid(2);

    bool wasActive = m_filterActive;
    m_filterActive = !filter.isEmpty();
    m_filterId = m_filterActive ? filter.toUInt(nullptr, 16) : 0;

    if (wasActive != m_filterActive || m_filterActive)
        invalidateFilter();
}

bool CanFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    if (!m_filterActive)
        return true;

    // CanFrameModel::Column::ID = 0
    QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!idx.isValid()) return false;

    // UserRole zwraca numeryczne ID
    QVariant v = sourceModel()->data(idx, Qt::UserRole);
    if (!v.isValid()) return false;

    return v.toUInt() == m_filterId;
}

bool CanFilterProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const {
    QVariant l = sourceModel()->data(left, Qt::UserRole);
    QVariant r = sourceModel()->data(right, Qt::UserRole);

    // Numeryczne porównanie gdy obie strony mają UserRole
    if (l.isValid() && r.isValid()) {
        switch (left.column()) {
        case 0: // ID
        case 3: // DLC
            return l.toUInt() < r.toUInt();
        case 5: // TIMESTAMP
        case 7: // DELTA
            return l.toULongLong() < r.toULongLong();
        default:
            return l.toULongLong() < r.toULongLong();
        }
    }

    // Fallback: porównanie tekstowe
    return QSortFilterProxyModel::lessThan(left, right);
}
