#pragma once
#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QAbstractItemModel>
#include <QScrollBar>
#include <QTableView>
#include <algorithm>
#include "core/CanFrameModel.h"

/**
 * @brief Miniaturowa mapa aktywności — pasek heatmap obok QTableView.
 *
 * Każdy piksel pionowy odpowiada zakresowi wierszy modelu.
 * Kolor: niebieski (normalny ruch) → pomarańczowo-czerwony (bursty).
 * Kliknięcie przewija tabelę do danej pozycji.
 */
class HeatmapBar : public QWidget {
    Q_OBJECT
public:
    explicit HeatmapBar(QAbstractItemModel *model, QWidget *parent = nullptr)
        : QWidget(parent), m_model(model)
    {
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        setFixedWidth(14);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setToolTip("Kliknij, aby przeskoczyć do ramek.\nCzerwony = bursty, niebieski = normalny ruch.");
    }

    void setTableView(QTableView *tv) { m_tableView = tv; }

signals:
    void jumpRequested(int modelRow);

protected:
    void paintEvent(QPaintEvent *) override {
        if (!m_model || m_model->rowCount() == 0) return;

        const int h = height();
        const int totalRows = m_model->rowCount();
        if (h != m_colors.size())
            rebuildDensity();

        QPainter p(this);
        for (int y = 0; y < h; ++y) {
            p.fillRect(0, y, width(), 1, m_colors[y]);
        }
    }

    void resizeEvent(QResizeEvent *) override {
        m_colors.clear(); // wymuś rebuild przy następnym paint
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton)
            jumpToPos(e->pos().y());
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (e->buttons() & Qt::LeftButton)
            jumpToPos(e->pos().y());
    }

private:
    void rebuildDensity() {
        if (!m_model) return;

        const int h = height();
        const int totalRows = m_model->rowCount();
        if (h <= 0 || totalRows <= 0) {
            m_colors.fill(Qt::transparent, h);
            return;
        }

        m_colors.resize(h);
        const float rowsPerPx = std::max(1.0f, (float)totalRows / h);

        for (int y = 0; y < h; ++y) {
            int rowStart = (int)(y * rowsPerPx);
            int rowEnd   = std::min((int)((y + 1) * rowsPerPx), totalRows);
            if (rowStart >= totalRows) { m_colors[y] = Qt::transparent; continue; }

            int burstCount = 0, sampleCount = 0;
            int step = std::max(1, (rowEnd - rowStart) / 8); // ~8 próbek na piksel
            for (int r = rowStart; r < rowEnd; r += step) {
                QVariant v = m_model->data(m_model->index(r, 0), CanFrameModel::BurstRole);
                if (v.toBool()) ++burstCount;
                ++sampleCount;
            }

            float density = sampleCount > 0 ? (float)burstCount / sampleCount : 0.0f;
            m_colors[y] = densityToColor(density);
        }
    }

    static QColor densityToColor(float d) {
        // d=0 → ciemnoniebieski, d=1 → jaskrawoczerwony
        if (d <= 0.0f) return QColor(26, 42, 58);       // #1a2a3a
        if (d >= 1.0f) return QColor(255, 50, 50);        // #ff3232
        // Interpolacja w HSL: blue (210°) → orange (30°) → red (0°)
        float hue = 210.0f - d * 210.0f;                  // 210° → 0°
        return QColor::fromHsvF(hue / 360.0f, 0.9f, 0.5f + d * 0.4f);
    }

    void jumpToPos(int y) {
        if (!m_model || !m_tableView) return;

        const int totalRows = m_model->rowCount();
        if (totalRows <= 0) return;

        const float rowsPerPx = std::max(1.0f, (float)totalRows / height());
        int targetRow = std::min((int)(y * rowsPerPx), totalRows - 1);

        QModelIndex idx = m_model->index(targetRow, 0);
        m_tableView->scrollTo(idx, QAbstractItemView::PositionAtTop);
        m_tableView->setCurrentIndex(idx);
    }

    QAbstractItemModel *m_model;
    QTableView *m_tableView = nullptr;
    QVector<QColor> m_colors;
};