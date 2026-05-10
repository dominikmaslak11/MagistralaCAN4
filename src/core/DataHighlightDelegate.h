#pragma once
#include <QStyledItemDelegate>
#include <QPainter>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include "CanFrameModel.h"

/**
 * @brief Delegat kolumny DATA — podświetla zmienione bajty.
 *
 * Odczytuje DisplayRole (hex string) i ChangedMaskRole (64-bit bitmask).
 * Bajty z ustawionym bitem są rysowane na czerwono (#ff4444, bold),
 * pozostałe — standardowym kolorem kolumny (#aa44ff).
 */
class DataHighlightDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        // Pobierz styl i przygotuj opcję z tłem (zaznaczenie, hover, focus)
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();

        // Rysuj samo tło (bez tekstu — tekst rysujemy sami)
        opt.text.clear();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        if (!index.isValid()) return;

        QString hexText = index.data(Qt::DisplayRole).toString();
        if (hexText.isEmpty()) return;

        uint64_t mask = index.data(CanFrameModel::ChangedMaskRole).value<uint64_t>();

        // Kolory adaptowane do motywu
        bool dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
        const QString normColor = dark ? "#aa44ff" : "#7733cc";   // fiolet
        const QString chgColor  = dark ? "#ff4444" : "#cc0000";   // czerwony

        if (mask == 0) {
            // Brak zmian — rysuj jednolitym tekstem
            painter->save();
            painter->setPen(QColor(normColor));
            painter->setFont(opt.font);
            QRect textRect = opt.rect.adjusted(4, 0, -4, 0);
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, hexText);
            painter->restore();
            return;
        }

        // Podziel hex na tokeny (bajty)
        const QStringList tokens = hexText.split(' ', Qt::SkipEmptyParts);

        // Buduj HTML z podświetlonymi bajtami
        QString html;
        html += QStringLiteral("<span style='font-family: %1; font-size: %2px;'>")
                    .arg(opt.font.family(), QString::number(opt.font.pointSize()));

        for (int i = 0; i < tokens.size(); ++i) {
            const QString &token = tokens[i];
            bool isHexByte = (token.length() == 2 &&
                              token[0].isLetterOrNumber() &&
                              token[1].isLetterOrNumber());

            if (isHexByte && i < 64 && (mask & (1ULL << i))) {
                html += QStringLiteral("<span style='color: %1; font-weight: bold;'>%2</span>")
                            .arg(chgColor, token);
            } else {
                html += QStringLiteral("<span style='color: %1;'>%2</span>")
                            .arg(normColor, token);
            }

            if (i < tokens.size() - 1)
                html += ' ';
        }
        html += QStringLiteral("</span>");

        // Rysuj HTML przez QTextDocument
        painter->save();
        QTextDocument doc;
        doc.setDefaultFont(opt.font);
        doc.setHtml(html);

        QRect textRect = opt.rect.adjusted(4, 0, -4, 0);
        painter->translate(textRect.topLeft());
        doc.setTextWidth(textRect.width());

        QAbstractTextDocumentLayout::PaintContext ctx;
        ctx.palette = opt.palette;
        doc.documentLayout()->draw(painter, ctx);
        painter->restore();
    }
};