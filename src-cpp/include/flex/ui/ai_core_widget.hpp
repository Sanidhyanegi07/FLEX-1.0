#ifndef FLEX_UI_AI_CORE_WIDGET_HPP
#define FLEX_UI_AI_CORE_WIDGET_HPP

#include <QWidget>
#include <QTimer>

#include <vector>

namespace flex::ui {

/**
 * Visual lifecycle state for the central AI Core.
 */
enum class CoreVisualState {
    Idle,        // Steady cosmic swirl in cyan/violet
    Attentive,   // Illuminated electric lavender filaments
    Thinking,    // Fast relativistic vortex spin with deep purple flares
    Responding,  // Outward pulsating gravitational ripples in emerald/cyan
    Error,       // Red alert pulsation with safe auto-recovery
    Offline      // Quenched singularity with muted smoke accretion disk
};

/**
 * Central Animated AI Core for FLEX 1.0.
 *
 * Implements a celestial black hole event horizon surrounded by a dynamic
 * swirling accretion disk, rotating relativistic filaments, multi-layered
 * photon sphere glow, and Keplerian stardust particle orbits.
 *
 * Guarantees:
 * - Framerate capped at ~30 FPS (33ms ticks) with < 1% CPU utilization.
 * - Supports Hero mode (for startup/empty chat) and Compact mode (for conversational HUD).
 * - Safe timer tracking: automatically pauses when hidden, cleanly cleans up on destroy.
 */
class AICoreWidget : public QWidget {
    Q_OBJECT

public:
    explicit AICoreWidget(QWidget* parent = nullptr, bool compact = false);
    ~AICoreWidget() override;

    [[nodiscard]] CoreVisualState visualState() const noexcept { return m_state; }
    void setVisualState(CoreVisualState state);

    [[nodiscard]] bool isCompact() const noexcept { return m_compact; }
    void setCompact(bool compact);

    void startAnimation();
    void stopAnimation();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private slots:
    void onAnimationTick();

private:
    struct StardustParticle {
        double rx;
        double ry;
        double speed;
        double phase;
        double size;
    };

    void initParticles();

    CoreVisualState m_state{CoreVisualState::Idle};
    bool m_compact{false};
    QTimer m_animTimer;
    double m_phase{0.0};
    double m_rippleProgress{0.0};
    bool m_rippleActive{false};
    std::vector<StardustParticle> m_particles;
};

} // namespace flex::ui

#endif // FLEX_UI_AI_CORE_WIDGET_HPP

