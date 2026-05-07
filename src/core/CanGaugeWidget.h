#pragma once
#include <QWidget>
#include <QString>
#include <QTimer>
#include <QPainter>

/**
 * @brief Konfigurowalny widget wskaźnika CAN – wyświetla wartość sygnału
 *        z min/max, staleness detection i cyberpunk estetyką.
 *
 * Style: Bar (poziomy pasek) lub Digital (duża cyfra).
 */
class CanGaugeWidget : public QWidget {
    Q_OBJECT
public:
    enum Style { Bar, Digital, Compact };

    explicit CanGaugeWidget(Style style = Bar, QWidget *parent = nullptr);

    void setSignalName(const QString &name);
    void setUnit(const QString &unit);
    void setRange(double min, double max);
    void setValue(double value);
    void setStaleTimeout(int ms) { m_staleTimeoutMs = ms; }

    /// Zwraca true jeśli wartość jest poza zakresem DBC.
    [[nodiscard]] bool isOutOfRange() const;

    /// Aktualizuje min/max obserwowane i timestamp.
    void recordValue(double value);

    /// Sprawdza staleness na podstawie timestampa.
    void checkStaleness();

    [[nodiscard]] double currentValue() const { return m_value; }
    [[nodiscard]] double minObserved() const { return m_minObserved; }
    [[nodiscard]] double maxObserved() const { return m_maxObserved; }

protected:
    void paintEvent(QPaintEvent *) override;
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

private:
    void paintBar(QPainter &p);
    void paintDigital(QPainter &p);
    void paintCompact(QPainter &p);
    QColor valueColor() const;

    Style    m_style = Bar;
    QString  m_name;
    QString  m_unit;
    double   m_value = 0;
    double   m_min   = 0;
    double   m_max   = 100;
    bool     m_stale  = false;
    qint64   m_lastUpdateMs = 0;
    int      m_staleTimeoutMs = 2000;

    // Obserwowane min/max
    double   m_minObserved = 0;
    double   m_maxObserved = 0;
    bool     m_hasObserved = false;

    // Kolory
    static constexpr QColor BG    {0x16, 0x1B, 0x22};
    static constexpr QColor BORDER{0x2A, 0x2A, 0x3C};
    static constexpr QColor GREEN {0x00, 0xFF, 0xAA};
    static constexpr QColor ORANGE{0xFF, 0xAA, 0x00};
    static constexpr QColor RED   {0xE9, 0x45, 0x60};
    static constexpr QColor PINK  {0xFF, 0x66, 0xCC};
    static constexpr QColor GREY  {0x60, 0x60, 0x60};
    static constexpr QColor DARKBG{0x0A, 0x0E, 0x17};
};
