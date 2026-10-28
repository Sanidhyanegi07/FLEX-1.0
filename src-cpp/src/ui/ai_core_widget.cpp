#include "flex/ui/ai_core_widget.hpp"
#include "flex/ui/theme.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <cmath>

namespace flex::ui {

namespace {
constexpr double PI = 3.14159265358979323846;
}

AICoreWidget::AICoreWidget(QWidget* parent, bool compact)
    : QWidget(parent), m_compact(compact)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAttribute(Qt::WA_NoSystemBackground, true);

    initParticles();

    connect(&m_animTimer, &QTimer::timeout, this, &AICoreWidget::onAnimationTick);
    m_animTimer.setInterval(33); // ~30 FPS

    if (!m_compact) {
        setMinimumHeight(200);
    } else {
        setFixedHeight(70);
    }
}

AICoreWidget::~AICoreWidget()
{
    m_animTimer.stop();
}

void AICoreWidget::initParticles()
{
    m_particles.clear();
    const int count = 16;
    for (int i = 0; i < count; ++i) {
        StardustParticle p;
        p.rx = 34.0 + (i * 3.8);
        p.ry = p.rx * 0.75;
        p.speed = 0.8 + (count - i) * 0.04;
        p.phase = i * (2.0 * PI / count);
        p.size = (i % 2 == 0) ? 2.0 : 3.0;
        m_particles.push_back(p);
    }
}

void AICoreWidget::setVisualState(CoreVisualState state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    if (state == CoreVisualState::Responding) {
        m_rippleActive = true;
        m_rippleProgress = 0.0;
    }
    update();
}

void AICoreWidget::setCompact(bool compact)
{
    if (m_compact == compact) {
        return;
    }
    m_compact = compact;
    if (m_compact) {
        setFixedHeight(70);
    } else {
        setMinimumHeight(200);
        setMaximumHeight(QWIDGETSIZE_MAX);
    }
    updateGeometry();
    update();
}

void AICoreWidget::startAnimation()
{
    if (!m_animTimer.isActive()) {
        m_animTimer.start();
    }
}

void AICoreWidget::stopAnimation()
{
    if (m_animTimer.isActive()) {
        m_animTimer.stop();
    }
}

void AICoreWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    startAnimation();
}

void AICoreWidget::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    stopAnimation();
}

QSize AICoreWidget::sizeHint() const
{
    if (m_compact) {
        return QSize(200, 70);
    }
    return QSize(350, 220);
}

QSize AICoreWidget::minimumSizeHint() const
{
    if (m_compact) {
        return QSize(80, 50);
    }
    return QSize(250, 160);
}

void AICoreWidget::onAnimationTick()
{
    double step = 0.025;
    switch (m_state) {
        case CoreVisualState::Thinking:   step = 0.075; break;
        case CoreVisualState::Responding: step = 0.045; break;
        case CoreVisualState::Attentive:  step = 0.035; break;
        case CoreVisualState::Error:      step = 0.040; break;
        case CoreVisualState::Offline:    step = 0.008; break;
        case CoreVisualState::Idle:       step = 0.020; break;
    }

    m_phase += step;
    if (m_phase > 2.0 * PI * 1000.0) {
        m_phase = 0.0;
    }

    if (m_rippleActive) {
        m_rippleProgress += 0.035;
        if (m_rippleProgress >= 1.0) {
            if (m_state == CoreVisualState::Responding) {
                m_rippleProgress = 0.0;
            } else {
                m_rippleActive = false;
            }
        }
    }

    update();
}

void AICoreWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0) {
        return;
    }

    double cx = w / 2.0;
    double cy = m_compact ? (h / 2.0) : (h * 0.44);

    double scale = m_compact ? 0.45 : std::min(1.0, std::min(w / 380.0, h / 240.0));
    if (scale < 0.35) scale = 0.35;

    // Palette selection based on state
    QColor haloCore, haloMid, haloOuter;
    std::vector<QColor> filamentColors;
    QColor photonRingColor;
    QColor particleColor;

    switch (m_state) {
        case CoreVisualState::Offline:
            haloCore = QColor(38, 42, 53, 160);
            haloMid = QColor(26, 29, 36, 120);
            haloOuter = QColor(18, 20, 26, 80);
            filamentColors = { QColor("#475569"), QColor("#334155"), QColor("#64748B") };
            photonRingColor = QColor(100, 116, 139, 140);
            particleColor = QColor(148, 163, 184, 120);
            break;
        case CoreVisualState::Error:
            haloCore = QColor(90, 30, 38, 180);
            haloMid = QColor(63, 22, 28, 140);
            haloOuter = QColor(42, 18, 21, 90);
            filamentColors = { QColor("#EF4444"), QColor("#F87171"), QColor("#DC2626"), QColor("#FCA5A5") };
            photonRingColor = QColor(254, 202, 202, 220);
            particleColor = QColor(248, 113, 113, 200);
            break;
        case CoreVisualState::Thinking:
            haloCore = QColor(96, 34, 168, 200);
            haloMid = QColor(67, 26, 117, 150);
            haloOuter = QColor(45, 18, 77, 90);
            filamentColors = { QColor("#A855F7"), QColor("#C084FC"), QColor("#E879F9"), QColor("#7C3AED") };
            photonRingColor = QColor(243, 232, 255, 230);
            particleColor = QColor(216, 180, 254, 220);
            break;
        case CoreVisualState::Responding:
            haloCore = QColor(26, 90, 120, 200);
            haloMid = QColor(20, 66, 88, 150);
            haloOuter = QColor(14, 42, 56, 90);
            filamentColors = { QColor("#38BDF8"), QColor("#34D399"), QColor("#6EE7B7"), QColor("#A78BFA") };
            photonRingColor = QColor(224, 242, 254, 230);
            particleColor = QColor(56, 189, 248, 220);
            break;
        case CoreVisualState::Attentive:
            haloCore = QColor(82, 60, 168, 190);
            haloMid = QColor(59, 44, 120, 140);
            haloOuter = QColor(38, 28, 78, 90);
            filamentColors = { QColor("#818CF8"), QColor("#A78BFA"), QColor("#C4B5FD"), QColor("#38BDF8") };
            photonRingColor = QColor(238, 242, 255, 220);
            particleColor = QColor(196, 181, 253, 200);
            break;
        case CoreVisualState::Idle:
        default:
            haloCore = QColor(51, 40, 110, 180);
            haloMid = QColor(33, 27, 69, 130);
            haloOuter = QColor(21, 18, 43, 80);
            filamentColors = { QColor("#7C3AED"), QColor("#8B5CF6"), QColor("#A78BFA"), QColor("#38BDF8"), QColor("#C4B5FD") };
            photonRingColor = QColor(224, 231, 255, 200);
            particleColor = QColor(196, 181, 253, 190);
            break;
    }

    // 1. Accretion Halos (multi-layered diffuse glow)
    double rOuter = 88.0 * scale;
    double rMid   = 64.0 * scale;
    double rCore  = 44.0 * scale;

    painter.setPen(Qt::NoPen);

    painter.setBrush(haloOuter);
    painter.drawEllipse(QPointF(cx, cy), rOuter, rOuter * 0.78);

    painter.setBrush(haloMid);
    painter.drawEllipse(QPointF(cx, cy), rMid, rMid * 0.78);

    painter.setBrush(haloCore);
    painter.drawEllipse(QPointF(cx, cy), rCore, rCore * 0.78);

    // 2. Gravitational Ripple (Active during Responding)
    if (m_rippleActive) {
        double ripR = (30.0 + m_rippleProgress * 80.0) * scale;
        int ripAlpha = static_cast<int>((1.0 - m_rippleProgress) * 180.0);
        QPen ripPen(QColor(56, 189, 248, ripAlpha), 2.0);
        painter.setPen(ripPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(cx, cy), ripR, ripR * 0.75);
    }

    // 3. Rotating Relativistic Arcs (Filaments)
    int arcIdx = 0;
    const double baseAngles[] = { 0.0, 60.0, 140.0, 210.0, 275.0, 320.0 };
    const double arcExtents[] = { 105.0, 125.0, 95.0, 115.0, 85.0, 100.0 };

    for (int i = 0; i < 6; ++i) {
        QColor col = filamentColors[arcIdx % filamentColors.size()];
        arcIdx++;
        double penWidth = (i % 2 == 0) ? 2.2 : 3.0;
        painter.setPen(QPen(col, penWidth * scale, Qt::SolidLine, Qt::RoundCap));
        painter.setBrush(Qt::NoBrush);

        double rotDegrees = (m_phase * 180.0 / PI) * ((i % 2 == 0) ? 1.0 : 1.25);
        double startAngle = std::fmod(baseAngles[i] + rotDegrees, 360.0);
        double arcR = (36.0 + (i * 7.5)) * scale;

        QRectF arcRect(cx - arcR, cy - arcR * 0.76, arcR * 2.0, arcR * 2.0 * 0.76);
        painter.drawArc(arcRect, static_cast<int>(startAngle * 16), static_cast<int>(arcExtents[i] * 16));
    }

    // 4. Stardust Particles in Keplerian Orbit
    painter.setPen(Qt::NoPen);
    painter.setBrush(particleColor);
    for (const auto& p : m_particles) {
        double curAngle = p.phase + (m_phase * p.speed);
        double px = cx + (p.rx * scale * std::cos(curAngle));
        double py = cy + (p.ry * scale * std::sin(curAngle));
        double pSize = p.size * scale;
        painter.drawEllipse(QPointF(px, py), pSize, pSize);
    }

    // 5. Bright Photon Sphere Ring
    double photonR = 24.0 * scale;
    QPen photonPen(photonRingColor, 2.0 * scale);
    painter.setPen(photonPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(cx, cy), photonR, photonR * 0.85);

    // 6. Central Singularity (Pitch Black Event Horizon)
    double singR = 20.0 * scale;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#000000"));
    painter.drawEllipse(QPointF(cx, cy), singR, singR * 0.85);

    // 7. Hero Mode Text
    if (!m_compact && h >= 160) {
        painter.setPen(QColor(Theme::TextPrimary));
        QFont titleFont("Segoe UI", 14, QFont::Bold);
        painter.setFont(titleFont);
        QRectF titleRect(0, cy + (50.0 * scale), w, 28);
        painter.drawText(titleRect, Qt::AlignCenter, "FLEX 1.0");

        painter.setPen(QColor(Theme::TextMuted));
        QFont subFont("Segoe UI", 9, QFont::Normal);
        painter.setFont(subFont);
        QRectF subRect(0, cy + (74.0 * scale), w, 22);
        painter.drawText(subRect, Qt::AlignCenter, "Local Intelligence. Deterministic C Core.");
    }
}

} // namespace flex::ui

