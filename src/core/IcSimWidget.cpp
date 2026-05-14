#include "IcSimWidget.h"
#include "CanSniffer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QSizePolicy>
#include <cmath>

// ── Dashboard paint widget ────────────────────────────────────────────────────

class IcSimDashPanel : public QWidget {
public:
    explicit IcSimDashPanel(QWidget *p = nullptr) : QWidget(p) {
        setMinimumSize(480, 210);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(210);
    }

    void setState(const IcSimState &s) { m_state = s; update(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(0x0A, 0x0E, 0x17));
        paintSpeedometer(p);
        paintDoors(p);
        paintSignals(p);
    }

private:
    // Speedometer: arc from 7:30 (0 mph) to 4:30 (90 mph), 270° span via 12 o'clock.
    // Qt arc convention: 0°=3 o'clock, positive=CCW.
    // 7:30 position = 225° in Qt coords, 4:30 = 315°.
    // Sweep CW (-270°) from 225° to arrive at 315°.
    void paintSpeedometer(QPainter &p) {
        const int W = width() * 6 / 10;
        const QPointF ctr(W / 2.0, height() / 2.0 + 10);
        const double R = std::min(W / 2.0, height() / 2.0) - 18;

        QPen trackPen(QColor(0x25, 0x2A, 0x35), 10, Qt::SolidLine, Qt::FlatCap);
        p.setPen(trackPen);
        QRectF arc(ctr.x() - R, ctr.y() - R, R * 2, R * 2);
        p.drawArc(arc, 225 * 16, -270 * 16);

        float s = std::min(std::max(m_state.speedMph, 0.f), 90.f);
        if (s > 0.5f) {
            QPen speedPen(QColor(0x00, 0xFF, 0xAA), 10, Qt::SolidLine, Qt::FlatCap);
            p.setPen(speedPen);
            int span = static_cast<int>(-270 * 16 * s / 90.f);
            p.drawArc(arc, 225 * 16, span);
        }

        // Tick marks at 0, 30, 60, 90 mph
        for (int tick : {0, 30, 60, 90}) {
            double angleDeg = 225.0 - (tick / 90.0) * 270.0;
            double rad = angleDeg * M_PI / 180.0;
            double sx = std::cos(rad), sy = -std::sin(rad);
            QPointF outer = ctr + QPointF(R * sx, R * sy);
            QPointF inner = ctr + QPointF((R - 14) * sx, (R - 14) * sy);
            p.setPen(QPen(QColor(0x55, 0x60, 0x6A), 2));
            p.drawLine(inner, outer);

            QPointF lblPt = ctr + QPointF((R - 28) * sx, (R - 28) * sy);
            p.setFont(QFont("Consolas", 7));
            p.setPen(QColor(0x60, 0x70, 0x80));
            p.drawText(QRectF(lblPt.x() - 12, lblPt.y() - 7, 24, 14),
                       Qt::AlignCenter, QString::number(tick));
        }

        // Needle
        {
            double angleDeg = 225.0 - (s / 90.f) * 270.0;
            double rad = angleDeg * M_PI / 180.0;
            QPointF tip = ctr + QPointF((R - 6) * std::cos(rad),
                                        -(R - 6) * std::sin(rad));
            QPointF base = ctr + QPointF(-8 * std::cos(rad), 8 * std::sin(rad));
            p.setPen(QPen(QColor(0xFF, 0x66, 0x00), 3, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(base, tip);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x30, 0x35, 0x40));
            p.drawEllipse(ctr, 7.0, 7.0);
        }

        // Speed value
        p.setFont(QFont("Consolas", 22, QFont::Bold));
        p.setPen(QColor(0xEE, 0xEE, 0xEE));
        p.drawText(QRectF(ctr.x() - 36, ctr.y() + R * 0.25, 72, 30),
                   Qt::AlignCenter, QString::number(static_cast<int>(s)));
        p.setFont(QFont("Consolas", 8));
        p.setPen(QColor(0x55, 0x65, 0x75));
        p.drawText(QRectF(ctr.x() - 16, ctr.y() + R * 0.25 + 24, 32, 14),
                   Qt::AlignCenter, "mph");

        p.setFont(QFont("Consolas", 7));
        p.setPen(QColor(0x30, 0x40, 0x50));
        p.drawText(QRectF(4, 4, 60, 12), Qt::AlignLeft, "ICSim");
    }

    void paintDoors(QPainter &p) {
        const int x0 = width() * 6 / 10 + 10;
        const int doorW = (width() - x0 - 10) / 2 - 4;
        const int doorH = (height() - 20) / 2 - 4;

        p.setFont(QFont("Consolas", 7));
        for (int i = 0; i < 4; ++i) {
            int col = i % 2, row = i / 2;
            int dx = x0 + col * (doorW + 8);
            int dy = 10 + row * (doorH + 8);
            QRectF r(dx, dy, doorW, doorH);

            bool locked = m_state.isDoorLocked(i);
            QColor border = locked ? QColor(0x00, 0xCC, 0x66) : QColor(0xE9, 0x45, 0x60);
            QColor fill   = locked ? QColor(0x04, 0x18, 0x0E) : QColor(0x22, 0x04, 0x08);

            p.setPen(QPen(border, 2));
            p.setBrush(fill);
            p.drawRoundedRect(r, 4, 4);

            // Door outline
            p.setPen(QPen(border, 1));
            QRectF icon(dx + doorW * 0.2, dy + doorH * 0.2,
                        doorW * 0.6, doorH * 0.55);
            p.drawRoundedRect(icon, 2, 2);
            QRectF win(icon.x() + 3, icon.y() + 3,
                       icon.width() - 6, icon.height() * 0.4);
            p.drawRoundedRect(win, 2, 2);

            p.setFont(QFont("Consolas", 7, QFont::Bold));
            p.setPen(border);
            p.drawText(QRectF(dx, dy + doorH * 0.78, doorW, doorH * 0.22),
                       Qt::AlignCenter,
                       locked ? QString("D%1 \xF0\x9F\x94\x92").arg(i + 1)
                              : QString("D%1 \xF0\x9F\x94\x93").arg(i + 1));
        }
    }

    void paintSignals(QPainter &p) {
        auto drawTriangle = [&](int cx, int cy, bool pointRight, bool active) {
            QColor col = active ? QColor(0x00, 0xFF, 0x66) : QColor(0x25, 0x30, 0x25);
            p.setPen(Qt::NoPen);
            p.setBrush(col);
            QPainterPath path;
            if (pointRight) {
                path.moveTo(cx - 10, cy - 8);
                path.lineTo(cx + 10, cy);
                path.lineTo(cx - 10, cy + 8);
            } else {
                path.moveTo(cx + 10, cy - 8);
                path.lineTo(cx - 10, cy);
                path.lineTo(cx + 10, cy + 8);
            }
            path.closeSubpath();
            p.drawPath(path);
        };

        int midY = height() - 18;
        int leftX = 22;
        drawTriangle(leftX,              midY, false, m_state.leftSignal);
        drawTriangle(leftX + 20,         midY, false, m_state.leftSignal);
        drawTriangle(width() * 6/10 - 42, midY, true,  m_state.rightSignal);
        drawTriangle(width() * 6/10 - 22, midY, true,  m_state.rightSignal);

        p.setFont(QFont("Consolas", 7));
        p.setPen(m_state.leftSignal  ? QColor(0x00, 0xFF, 0x66) : QColor(0x30, 0x40, 0x30));
        p.drawText(QRectF(leftX - 8, midY - 20, 50, 12), Qt::AlignCenter, "LEFT");
        p.setPen(m_state.rightSignal ? QColor(0x00, 0xFF, 0x66) : QColor(0x30, 0x40, 0x30));
        p.drawText(QRectF(width() * 6/10 - 50, midY - 20, 50, 12), Qt::AlignCenter, "RIGHT");
    }

    IcSimState m_state;
};

// ── IcSimWidget ───────────────────────────────────────────────────────────────

IcSimWidget::IcSimWidget(CanSniffer *sniffer, QWidget *parent)
    : QWidget(parent), m_sniffer(sniffer)
{
    m_continuousTimer.setInterval(100);
    connect(&m_continuousTimer, &QTimer::timeout, this, &IcSimWidget::onContinuousTimer);
    buildLayout();
}

void IcSimWidget::buildLayout()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    m_dash = new IcSimDashPanel(this);
    root->addWidget(m_dash);

    auto *ctrlRow = new QHBoxLayout;
    ctrlRow->setSpacing(6);
    ctrlRow->addWidget(buildSpeedGroup());
    ctrlRow->addWidget(buildDoorGroup());
    ctrlRow->addWidget(buildSignalGroup());
    root->addLayout(ctrlRow);

    root->addWidget(buildConfigGroup());
    root->addStretch();
}

QGroupBox *IcSimWidget::buildSpeedGroup()
{
    auto *grp = new QGroupBox("Prędkość");
    auto *lay = new QVBoxLayout(grp);

    m_speedLabel = new QLabel("0 mph", this);
    m_speedLabel->setAlignment(Qt::AlignCenter);
    m_speedLabel->setStyleSheet("font: bold 14pt 'Consolas'; color: #00ffaa;");
    lay->addWidget(m_speedLabel);

    m_speedSlider = new QSlider(Qt::Horizontal, this);
    m_speedSlider->setRange(0, 90);
    m_speedSlider->setValue(0);
    m_speedSlider->setTickInterval(10);
    m_speedSlider->setTickPosition(QSlider::TicksBelow);
    connect(m_speedSlider, &QSlider::valueChanged, this, &IcSimWidget::onSpeedSliderChanged);
    lay->addWidget(m_speedSlider);

    auto *btnRow = new QHBoxLayout;
    auto *accelBtn = new QPushButton("▲ Przyspiesz", this);
    auto *brakeBtn = new QPushButton("▼ Hamuj", this);
    connect(accelBtn, &QPushButton::clicked, this, [this]{
        m_speedSlider->setValue(std::min(m_speedSlider->value() + 5, 90));
    });
    connect(brakeBtn, &QPushButton::clicked, this, [this]{
        m_speedSlider->setValue(std::max(m_speedSlider->value() - 5, 0));
    });
    btnRow->addWidget(accelBtn);
    btnRow->addWidget(brakeBtn);
    lay->addLayout(btnRow);

    m_continuousChk = new QCheckBox("Ciągłe wysyłanie (100 ms)", this);
    connect(m_continuousChk, &QCheckBox::toggled, this, [this](bool on){
        on ? m_continuousTimer.start() : m_continuousTimer.stop();
    });
    lay->addWidget(m_continuousChk);

    auto *resetBtn = new QPushButton("Reset", this);
    connect(resetBtn, &QPushButton::clicked, this, &IcSimWidget::onResetState);
    lay->addWidget(resetBtn);

    return grp;
}

QGroupBox *IcSimWidget::buildDoorGroup()
{
    auto *grp = new QGroupBox("Drzwi");
    auto *lay = new QGridLayout(grp);

    const char *names[] = {"D1 (FL)", "D2 (FR)", "D3 (RL)", "D4 (RR)"};
    for (int i = 0; i < 4; ++i) {
        m_doorBtn[i] = new QPushButton(names[i], this);
        m_doorBtn[i]->setCheckable(true);
        m_doorBtn[i]->setChecked(true);
        connect(m_doorBtn[i], &QPushButton::clicked, this, [this, i]{ onToggleDoor(i); });
        lay->addWidget(m_doorBtn[i], i / 2, i % 2);
    }
    updateDoorButtons();

    auto *lockAllBtn   = new QPushButton("Zamknij wszystkie", this);
    auto *unlockAllBtn = new QPushButton("Otwórz wszystkie", this);
    connect(lockAllBtn,   &QPushButton::clicked, this, &IcSimWidget::onLockAll);
    connect(unlockAllBtn, &QPushButton::clicked, this, &IcSimWidget::onUnlockAll);
    lay->addWidget(lockAllBtn,   2, 0);
    lay->addWidget(unlockAllBtn, 2, 1);

    return grp;
}

QGroupBox *IcSimWidget::buildSignalGroup()
{
    auto *grp = new QGroupBox("Kierunkowskazy");
    auto *lay = new QVBoxLayout(grp);

    m_leftSigBtn  = new QPushButton("◄ Lewy", this);
    m_rightSigBtn = new QPushButton("Prawy ►", this);
    m_leftSigBtn->setCheckable(true);
    m_rightSigBtn->setCheckable(true);
    connect(m_leftSigBtn,  &QPushButton::clicked, this, &IcSimWidget::onToggleLeftSignal);
    connect(m_rightSigBtn, &QPushButton::clicked, this, &IcSimWidget::onToggleRightSignal);
    lay->addWidget(m_leftSigBtn);
    lay->addWidget(m_rightSigBtn);
    lay->addStretch();

    return grp;
}

QGroupBox *IcSimWidget::buildConfigGroup()
{
    auto *grp = new QGroupBox("Konfiguracja CAN ID (dla ICSim z seedem -s)");
    auto *lay = new QHBoxLayout(grp);

    auto addSpin = [&](const QString &label, uint32_t def) -> QSpinBox * {
        lay->addWidget(new QLabel(label));
        auto *spin = new QSpinBox(this);
        spin->setRange(1, 0x7FF);
        spin->setValue(static_cast<int>(def));
        spin->setDisplayIntegerBase(16);
        spin->setPrefix("0x");
        lay->addWidget(spin);
        return spin;
    };

    m_speedIdSpin  = addSpin("Speed ID:",  IcSimDecoder::SPEED_ID);
    m_doorIdSpin   = addSpin("Door ID:",   IcSimDecoder::DOOR_ID);
    m_signalIdSpin = addSpin("Signal ID:", IcSimDecoder::SIGNAL_ID);
    lay->addStretch();

    return grp;
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void IcSimWidget::processFrame(const CanFrame &frame)
{
    if (IcSimDecoder::decode(frame, m_state, speedId(), doorId(), signalId())) {
        m_dash->setState(m_state);
        if (!m_speedSlider->isSliderDown()) {
            int mph = static_cast<int>(m_state.speedMph + 0.5f);
            QSignalBlocker blk(m_speedSlider);
            m_speedSlider->setValue(mph);
        }
        updateDoorButtons();
        updateSignalButtons();
        m_speedLabel->setText(QString("%1 mph / %2 kph")
                              .arg(static_cast<int>(m_state.speedMph))
                              .arg(static_cast<int>(m_state.speedKph)));
    }
}

void IcSimWidget::onSpeedSliderChanged(int mph)
{
    m_speedLabel->setText(QString("%1 mph").arg(mph));
    m_state.speedMph = static_cast<float>(mph);
    m_state.speedKph = m_state.speedMph / 0.6213751f;
    m_dash->setState(m_state);
    sendFrame(IcSimDecoder::buildSpeedFrame(static_cast<float>(mph), speedId()));
}

void IcSimWidget::onLockAll()
{
    m_state.doorMask = 0x0F;
    updateDoorButtons();
    m_dash->setState(m_state);
    sendFrame(IcSimDecoder::buildDoorFrame(0x0F, doorId()));
}

void IcSimWidget::onUnlockAll()
{
    m_state.doorMask = 0x00;
    updateDoorButtons();
    m_dash->setState(m_state);
    sendFrame(IcSimDecoder::buildDoorFrame(0x00, doorId()));
}

void IcSimWidget::onToggleDoor(int door)
{
    m_state.doorMask ^= (1u << door);
    updateDoorButtons();
    m_dash->setState(m_state);
    sendFrame(IcSimDecoder::buildDoorFrame(m_state.doorMask, doorId()));
}

void IcSimWidget::onToggleLeftSignal()
{
    m_state.leftSignal = m_leftSigBtn->isChecked();
    updateSignalButtons();
    m_dash->setState(m_state);
    sendFrame(IcSimDecoder::buildSignalFrame(m_state.leftSignal, m_state.rightSignal, signalId()));
}

void IcSimWidget::onToggleRightSignal()
{
    m_state.rightSignal = m_rightSigBtn->isChecked();
    updateSignalButtons();
    m_dash->setState(m_state);
    sendFrame(IcSimDecoder::buildSignalFrame(m_state.leftSignal, m_state.rightSignal, signalId()));
}

void IcSimWidget::onContinuousTimer()
{
    sendFrame(IcSimDecoder::buildSpeedFrame(static_cast<float>(m_speedSlider->value()), speedId()));
}

void IcSimWidget::onResetState()
{
    m_speedSlider->setValue(0);
    m_continuousChk->setChecked(false);
    m_state = IcSimState{};
    m_dash->setState(m_state);
    updateDoorButtons();
    updateSignalButtons();
    sendFrame(IcSimDecoder::buildSpeedFrame(0.f, speedId()));
    sendFrame(IcSimDecoder::buildDoorFrame(0x0F, doorId()));
    sendFrame(IcSimDecoder::buildSignalFrame(false, false, signalId()));
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void IcSimWidget::updateDoorButtons()
{
    const char *locked_style   = "QPushButton { background:#04180E; color:#00CC66; border:1px solid #00CC66; border-radius:3px; }";
    const char *unlocked_style = "QPushButton { background:#220408; color:#E94560; border:1px solid #E94560; border-radius:3px; }";
    for (int i = 0; i < 4; ++i) {
        bool locked = m_state.isDoorLocked(i);
        QSignalBlocker blk(m_doorBtn[i]);
        m_doorBtn[i]->setChecked(locked);
        m_doorBtn[i]->setStyleSheet(locked ? locked_style : unlocked_style);
        m_doorBtn[i]->setText(QString(locked ? "\xF0\x9F\x94\x92 D%1" : "\xF0\x9F\x94\x93 D%1").arg(i + 1));
    }
}

void IcSimWidget::updateSignalButtons()
{
    const char *activeStyle = "QPushButton { background:#004422; color:#00FF66; border:1px solid #00FF66; border-radius:3px; }";
    const char *inactStyle  = "QPushButton { border-radius:3px; }";
    QSignalBlocker bl(m_leftSigBtn), br(m_rightSigBtn);
    m_leftSigBtn->setChecked(m_state.leftSignal);
    m_rightSigBtn->setChecked(m_state.rightSignal);
    m_leftSigBtn->setStyleSheet(m_state.leftSignal   ? activeStyle : inactStyle);
    m_rightSigBtn->setStyleSheet(m_state.rightSignal ? activeStyle : inactStyle);
}

void IcSimWidget::sendFrame(const CanFrame &f)
{
    if (m_sniffer) m_sniffer->writeFrame(f);
}

uint32_t IcSimWidget::speedId()  const { return static_cast<uint32_t>(m_speedIdSpin->value()); }
uint32_t IcSimWidget::doorId()   const { return static_cast<uint32_t>(m_doorIdSpin->value()); }
uint32_t IcSimWidget::signalId() const { return static_cast<uint32_t>(m_signalIdSpin->value()); }
