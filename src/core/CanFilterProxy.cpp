#include "CanFilterProxy.h"
#include "CanFrameModel.h"
#include <QAbstractItemModel>

CanFilterProxy::CanFilterProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

QString CanFilterProxy::setExpression(const QString &expr) {
    QString trimmed = expr.trimmed();
    beginFilterChange();
    if (trimmed.isEmpty()) {
        m_exprActive = false;
        endFilterChange();
        return {};
    }
    m_expr = CanFilterExpr::parse(trimmed);
    if (!m_expr.isValid()) {
        m_exprActive = false;
        endFilterChange();
        return m_expr.errorString();
    }
    m_exprActive   = true;
    m_filterActive = false;
    m_filterId     = 0;
    endFilterChange();
    return {};
}

void CanFilterProxy::setIdFilter(const QString &text) {
    QString filter = text.trimmed();
    if (filter.startsWith("0x", Qt::CaseInsensitive))
        filter = filter.mid(2);

    bool wasActive = m_filterActive;
    bool newActive = !filter.isEmpty();
    uint32_t newId = newActive ? filter.toUInt(nullptr, 16) : 0;

    if (wasActive != newActive || newActive) {
        beginFilterChange();
        m_filterActive = newActive;
        m_filterId     = newId;
        endFilterChange();
    } else {
        m_filterActive = newActive;
        m_filterId     = newId;
    }
}

bool CanFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    if (!m_filterActive && !m_exprActive)
        return true;

    if (m_exprActive) {
        auto *model = qobject_cast<CanFrameModel*>(sourceModel());
        if (!model) return true;
        CanFrame frame = model->frameAt(sourceRow);
        return m_expr.matches(frame);
    }

    // Simple ID filter
    QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!idx.isValid()) return false;
    QVariant v = sourceModel()->data(idx, Qt::UserRole);
    if (!v.isValid()) return false;
    return v.toUInt() == m_filterId;
}

bool CanFilterProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const {
    QVariant l = sourceModel()->data(left, Qt::UserRole);
    QVariant r = sourceModel()->data(right, Qt::UserRole);

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

    return QSortFilterProxyModel::lessThan(left, right);
}
