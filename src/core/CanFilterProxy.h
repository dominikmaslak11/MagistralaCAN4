#pragma once
#include <QSortFilterProxyModel>
#include "CanFilterExpr.h"
#include <cstdint>

class CanFrameModel;

/**
 * @brief Proxy filtrująco-sortujący dla CanFrameModel.
 *
 * Filtruje wg CAN ID (prosty hex) lub wyrażenia Wireshark-style (CanFilterExpr).
 * Sortuje przez Qt::UserRole.
 */
class CanFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit CanFilterProxy(QObject *parent = nullptr);

    /// Ustaw filtr CAN ID (hex, np. "123" lub "0x123"). Pusty tekst = brak filtra.
    void setIdFilter(const QString &text);

    /// Ustaw wyrażenie filtra (np. "can.id==0x100 && can.data[0]>0x50").
    /// Puste wyrażenie = brak filtra. Zwraca błąd parsowania lub pusty string.
    QString setExpression(const QString &expr);

    /// Czy filtr jest aktywny.
    bool isFilterActive() const { return m_filterActive || m_exprActive; }

    /// Bieżący filtr jako hex string (wielkie litery), pusty gdy brak filtra.
    QString currentFilterId() const {
        return m_filterActive ? QString::number(m_filterId, 16).toUpper() : QString();
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    bool m_filterActive = false;
    uint32_t m_filterId = 0;

    bool          m_exprActive = false;
    CanFilterExpr m_expr;
};
