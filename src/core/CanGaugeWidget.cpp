#include "CanGaugeWidget.h"
#include <QDateTime>
#include <QFontMetrics>
#include <cmath>

CanGaugeWidget::CanGaugeWidget(Style style, QWidget *parent)
    : QWidget(parent), m_style(style) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void CanGaugeWidget::setSignalName(const QString &name) { m_name = name; update(); }
void CanGaugeWidget::setUnit(const QString &unit)     { m_unit = unit; update(); }

void CanGaugeWidget::setRange(double min, double max) {
    m_min = min; m_max = max;
    if (!m_hasObserved) { m_minObserved = min; m_maxObserved = max; }
    update();
}

void CanGaugeWidget::setValue(double value) {
    m_value = value;
    recordValue(value);
    update();
}

void CanGaugeWidget::recordValue(double value) {
    m_lastUpdateMs = QDateTime::currentMSecsSinceEpoch();
    m_stale = false;
    if (!m_hasObserved) {
        m_minObserved = value;
        m_maxObserved = value;
        m_hasObserved = true;
    } else {
        if (value < m_minObserved) m_minObserved = value;
        if (value > m_maxObserved) m_maxObserved = value;
    }
}

void CanGaugeWidget::checkStaleness() {
    if (m_lastUpdateMs == 0) return;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool wasStale = m_stale;
    m_stale = (now - m_lastUpdateMs) > m_staleTimeoutMs;
    if (wasStale != m_stale) update();
}

bool CanGaugeWidget::isOutOfRange() const {
    return m_value < m_min || m_value > m_max;
}

QSize CanGaugeWidget::minimumSizeHint() const { return {180, 60}; }
QSize CanGaugeWidget::sizeHint() const {
    switch (m_style) {
    case Bar:     return {220, 70};
    case Digital: return {180, 90};
    case Compact: return {150, 50};
    }
    return {200, 65};
}

QColor CanGaugeWidget::valueColor() const {
    if (m_stale) return GREY;
    if (isOutOfRange()) return RED;
    if (m_max - m_min > 0) {
        double ratio = (m_value - m_min) / (m_max - m_min);
        if (ratio > 0.85) return RED;
        if (ratio > 0.70) return ORANGE;
    }
    return GREEN;
}

// ── Paint ────────────────────────────────────────────────────

void CanGaugeWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Tło
    p.setPen(Qt::NoPen);
    p.setBrush(DARKBG);
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);

    p.setPen(BORDER);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);

    switch (m_style) {
    case Bar:     paintBar(p); break;
    case Digital: paintDigital(p); break;
    case Compact: paintCompact(p); break;
    }
}

void CanGaugeWidget::paintBar(QPainter &p) {
    int w = width(), h = height();
    int margin = 6;

    // Nazwa sygnału
    QFont nameFont("Consolas", 9, QFont::Bold);
    p.setFont(nameFont);
    p.setPen(PINK);
    p.drawText(margin, margin, w - 2*margin, 16, Qt::AlignLeft, m_name);

    // Wartość + jednostka (prawy górny róg)
    QColor col = valueColor();
    QFont valFont("Consolas", 11, QFont::Bold);
    p.setFont(valFont);
    p.setPen(col);
    QString valStr = QString("%1 %2").arg(m_value, 0, 'f', 1).arg(m_unit);
    p.drawText(margin, margin, w - 2*margin, 16, Qt::AlignRight, valStr);

    // Pasek
    int barY = 26, barH = 16, barX = margin, barW = w - 2*margin;
    p.setPen(Qt::NoPen);
    p.setBrush(BG);
    p.drawRoundedRect(barX, barY, barW, barH, 4, 4);

    if (m_max > m_min && !m_stale) {
        double ratio = std::clamp((m_value - m_min) / (m_max - m_min), 0.0, 1.0);
        int fillW = static_cast<int>(barW * ratio);
        if (fillW > 0) {
            QLinearGradient grad(barX, 0, barX + barW, 0);
            grad.setColorAt(0.0, GREEN);
            grad.setColorAt(0.7, ORANGE);
            grad.setColorAt(0.85, RED);
            p.setBrush(grad);
            p.drawRoundedRect(barX, barY, fillW, barH, 4, 4);
        }
    }

    // Min / Max pod paskiem
    QFont smallFont("Consolas", 7);
    p.setFont(smallFont);
    p.setPen(GREY);
    p.drawText(barX, barY + barH + 2, barW, 12, Qt::AlignLeft,
               QString("%1").arg(m_min, 0, 'f', 1));
    p.drawText(barX, barY + barH + 2, barW, 12, Qt::AlignCenter,
               m_stale ? "STALE" : (isOutOfRange() ? "⚠ OOR" :
                QString("min:%1 max:%2").arg(m_minObserved, 0, 'f', 1).arg(m_maxObserved, 0, 'f', 1)));
    p.drawText(barX, barY + barH + 2, barW, 12, Qt::AlignRight,
               QString("%1").arg(m_max, 0, 'f', 1));
}

void CanGaugeWidget::paintDigital(QPainter &p) {
    int w = width(), h = height(), m = 6;

    // Nazwa
    QFont nf("Consolas", 9, QFont::Bold);
    p.setFont(nf); p.setPen(PINK);
    p.drawText(m, m, w - 2*m, 14, Qt::AlignCenter, m_name);

    // Duża cyfra
    QColor col = valueColor();
    QFont vf("Consolas", 26, QFont::Bold);
    p.setFont(vf); p.setPen(col);
    QString vs = m_stale ? "—" : QString::number(m_value, 'f', 1);
    p.drawText(m, 22, w - 2*m, 32, Qt::AlignCenter, vs);

    // Jednostka
    QFont uf("Consolas", 10);
    p.setFont(uf); p.setPen(GREY);
    p.drawText(m, 52, w - 2*m, 14, Qt::AlignCenter, m_unit);

    // Min/Max linia
    QFont sf("Consolas", 7);
    p.setFont(sf);
    p.drawText(m, h - 16, w - 2*m, 12, Qt::AlignLeft, QString("%1").arg(m_min, 0, 'f', 1));
    p.drawText(m, h - 16, w - 2*m, 12, Qt::AlignRight, QString("%1").arg(m_max, 0, 'f', 1));

    if (m_stale) {
        p.setPen(RED);
        p.drawText(m, h - 16, w - 2*m, 12, Qt::AlignCenter, "STALE");
    }
}

void CanGaugeWidget::paintCompact(QPainter &p) {
    int w = width(), h = height(), m = 4;

    QFont nf("Consolas", 7);
    p.setFont(nf); p.setPen(PINK);
    p.drawText(m, 2, w - 2*m, 12, Qt::AlignLeft, m_name);

    QColor col = valueColor();
    QFont vf("Consolas", 12, QFont::Bold);
    p.setFont(vf); p.setPen(col);
    p.drawText(m, 14, w - 2*m, 16, Qt::AlignLeft,
               m_stale ? "—" : QString("%1 %2").arg(m_value, 0, 'f', 1).arg(m_unit));

    QFont sf("Consolas", 6);
    p.setFont(sf); p.setPen(GREY);
    p.drawText(m, 30, w - 2*m, 10, Qt::AlignLeft,
               QString("min:%1 max:%2").arg(m_minObserved, 0, 'f', 1).arg(m_maxObserved, 0, 'f', 1));
}
