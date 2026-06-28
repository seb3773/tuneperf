#include "tqttoggleswitch.h"

#include <ntqapplication.h>
#include <ntqpainter.h>
#include <ntqpen.h>
#include <ntqfont.h>
#include <ntqfontmetrics.h>
#include <ntqevent.h>

#include <math.h>

static inline double tqt_absd(double v) { return v < 0.0 ? -v : v; }

static inline int tqt_iround(double v) {
    if (v >= 0.0) return (int)(v + 0.5);
    return (int)(v - 0.5);
}

static inline double tqt_clamp(double x, double a, double b) {
    if (x < a) return a;
    if (x > b) return b;
    return x;
}

static inline TQColor tqt_toggle_solid_fg_color_(const TQColor& bg)
{
    const int y = (bg.red() * 30 + bg.green() * 59 + bg.blue() * 11) / 100;
    if (y > 200) return TQColor(0, 0, 0);
    return TQColor(255, 255, 255);
}

TQtToggleSwitch::TQtToggleSwitch(TQWidget* parent, const char* name)
    : TQWidget(parent, name),
#ifdef TQT_TOGGLE_MINIMAL
      m_mode((int)BorderSolidSoftRectNoText),
#else
      m_mode((int)BorderTraceRounded),
#endif
      m_checked(0),
      m_fg(TQColor(255, 255, 255)),
      m_on(TQColor(0, 210, 255)), // Cyan: #00D2FF
      m_off(TQColor(63, 63, 70)),  // Dark gray: #3F3F46
      m_bd(TQColor(0, 210, 255)),
      m_bdSize(2),
      m_animDuration(250), // Snappier animation
      m_easing((int)OutCirc),
      m_prog(0.0),
      m_progStart(0.0),
      m_progTarget(0.0),
      m_animStart(),
      m_animating(0),
      m_pressed(0),
      m_moved(0),
      m_dragging(0),
      m_moveTargetState(0),
      m_prevX(0),
      m_pressPos(),
      m_slideLeft(0.0),
      m_slideRight(1.0),
      m_timer(this) {
    setBackgroundMode(PaletteBackground);
    setMinimumSize(48, 20); // Smaller default size for toggles
    setSizePolicy(TQSizePolicy::Fixed, TQSizePolicy::Fixed);

    connect(&m_timer, SIGNAL(timeout()), this, SLOT(onAnimTick()));
    m_timer.start(15);

    calculateGeometry();
}

TQtToggleSwitch::TQtToggleSwitch(int mode, TQWidget* parent, const char* name)
    : TQWidget(parent, name),
#ifdef TQT_TOGGLE_MINIMAL
      m_mode((int)BorderSolidSoftRectNoText),
#else
      m_mode((int)BorderTraceRounded),
#endif
      m_checked(0),
      m_fg(TQColor(255, 255, 255)),
      m_on(TQColor(0, 210, 255)),
      m_off(TQColor(63, 63, 70)),
      m_bd(TQColor(0, 210, 255)),
      m_bdSize(2),
      m_animDuration(250),
      m_easing((int)OutCirc),
      m_prog(0.0),
      m_progStart(0.0),
      m_progTarget(0.0),
      m_animStart(),
      m_animating(0),
      m_pressed(0),
      m_moved(0),
      m_dragging(0),
      m_moveTargetState(0),
      m_prevX(0),
      m_pressPos(),
      m_slideLeft(0.0),
      m_slideRight(1.0),
      m_timer(this) {
    setBackgroundMode(PaletteBackground);
    setMinimumSize(48, 20);
    setSizePolicy(TQSizePolicy::Fixed, TQSizePolicy::Fixed);

    connect(&m_timer, SIGNAL(timeout()), this, SLOT(onAnimTick()));
    m_timer.start(15);

    setMode(mode);
    calculateGeometry();
}

TQtToggleSwitch::TQtToggleSwitch(int mode, bool checked, TQWidget* parent, const char* name)
    : TQWidget(parent, name),
#ifdef TQT_TOGGLE_MINIMAL
      m_mode((int)BorderSolidSoftRectNoText),
#else
      m_mode((int)BorderTraceRounded),
#endif
      m_checked(0),
      m_fg(TQColor(255, 255, 255)),
      m_on(TQColor(0, 210, 255)),
      m_off(TQColor(63, 63, 70)),
      m_bd(TQColor(0, 210, 255)),
      m_bdSize(2),
      m_animDuration(250),
      m_easing((int)OutCirc),
      m_prog(0.0),
      m_progStart(0.0),
      m_progTarget(0.0),
      m_animStart(),
      m_animating(0),
      m_pressed(0),
      m_moved(0),
      m_dragging(0),
      m_moveTargetState(0),
      m_prevX(0),
      m_pressPos(),
      m_slideLeft(0.0),
      m_slideRight(1.0),
      m_timer(this) {
    setBackgroundMode(PaletteBackground);
    setMinimumSize(48, 20);
    setSizePolicy(TQSizePolicy::Fixed, TQSizePolicy::Fixed);

    connect(&m_timer, SIGNAL(timeout()), this, SLOT(onAnimTick()));
    m_timer.start(15);

    setMode(mode);
    setStateWithoutSignal(checked);
    calculateGeometry();
}

bool TQtToggleSwitch::isChecked() const { return m_checked != 0; }

bool TQtToggleSwitch::getState() const { return isChecked(); }

void TQtToggleSwitch::setMode(int mode) {
#ifdef TQT_TOGGLE_MINIMAL
    Q_UNUSED(mode);
    m_mode = (int)BorderSolidSoftRectNoText;
#else
    if (mode < 0) mode = 0;
    if (mode > 6) mode = 6;
    m_mode = mode;
#endif
    update();
}

int TQtToggleSwitch::mode() const { return m_mode; }

void TQtToggleSwitch::setSuitableHeight(int h) {
    if (h < 8) h = 8;
    setFixedSize(h * 3, h);
    calculateGeometry();
}

void TQtToggleSwitch::setForeground(const TQColor& c) { m_fg = c; update(); }

void TQtToggleSwitch::setBackground(const TQColor& on, const TQColor& off) {
    m_on = on;
    m_off = off;
    update();
}

void TQtToggleSwitch::setBorder(const TQColor& c, int size) {
    m_bd = c;
    if (size < 0) size = 0;
    m_bdSize = size;
    calculateGeometry();
    update();
}

void TQtToggleSwitch::setAnimationDuration(int ms) {
    if (ms < 0) ms = 0;
    m_animDuration = ms;
}

void TQtToggleSwitch::setAnimationEasing(int easing) {
    if (easing < 0) easing = 0;
    if (easing > 2) easing = 2;
    m_easing = easing;
}

const TQColor& TQtToggleSwitch::foregroundColor() const { return m_fg; }
const TQColor& TQtToggleSwitch::onColor() const { return m_on; }
const TQColor& TQtToggleSwitch::offColor() const { return m_off; }
const TQColor& TQtToggleSwitch::borderColor() const { return m_bd; }
int TQtToggleSwitch::borderSize() const { return m_bdSize; }

TQSize TQtToggleSwitch::minimumSizeHint() const { return TQSize(48, 20); }

TQSize TQtToggleSwitch::sizeHint() const {
    const int h = height();
    if (h > 0) return TQSize(h * 3, h);
    return minimumSizeHint();
}

void TQtToggleSwitch::setState(bool state) {
    const int ns = state ? 1 : 0;
    const int changed = (ns != m_checked);
    setStateWithoutSignal(state);
    if (changed) emit stateChanged(isChecked());
}

void TQtToggleSwitch::setStateWithoutSignal(bool state) {
    m_checked = state ? 1 : 0;
    startSwitchAnimation();
}

void TQtToggleSwitch::toggleState() {
    toggleStateWithoutSignal();
    emit stateChanged(isChecked());
}

void TQtToggleSwitch::toggleStateWithoutSignal() {
    setStateWithoutSignal(!isChecked());
}

void TQtToggleSwitch::resizeEvent(TQResizeEvent* e) {
    TQWidget::resizeEvent(e);
    calculateGeometry();
}

void TQtToggleSwitch::mousePressEvent(TQMouseEvent* e) {
    if (e->button() == TQt::LeftButton) {
        m_pressPos = e->pos();
        m_moved = 0;
        m_dragging = 0;
        m_prevX = m_pressPos.x();
        m_pressed = 1;
        e->accept();
        return;
    }
    TQWidget::mousePressEvent(e);
}

void TQtToggleSwitch::mouseMoveEvent(TQMouseEvent* e) {
    if (e->state() & TQt::LeftButton) {
        if (!m_moved) {
            if ((e->pos() - m_pressPos).manhattanLength() > TQApplication::startDragDistance()) m_moved = 1;
        }

        if (m_moved) {
            const int x = e->pos().x();
            if (x <= (int)m_slideLeft) setProgressManual(0.0);
            else if (x >= (int)m_slideRight) setProgressManual(1.0);
            else setProgressManual(((double)x - m_slideLeft) / (m_slideRight - m_slideLeft));

            m_moveTargetState = (x > m_prevX) ? 1 : 0;
            m_prevX = x;
            m_dragging = 1;
        }

        e->accept();
        return;
    }

    TQWidget::mouseMoveEvent(e);
}

void TQtToggleSwitch::mouseReleaseEvent(TQMouseEvent* e) {
    if (e->button() == TQt::LeftButton) {
        const int old = m_checked;

        if (!m_moved) {
            toggleState();
        } else {
            const double total = (m_slideRight - m_slideLeft);
            const double stick = total * 0.15;
            const int x = e->pos().x();

            if ((double)x <= m_slideLeft + stick) m_checked = 0;
            else if ((double)x >= m_slideRight - stick) m_checked = 1;
            else m_checked = m_moveTargetState;

            if (m_checked != old) emit stateChanged(isChecked());

            startSwitchAnimation();
        }

        m_pressed = 0;

        e->accept();
        return;
    }

    TQWidget::mouseReleaseEvent(e);
}

void TQtToggleSwitch::paintEvent(TQPaintEvent*) {
    TQPainter p(this);

    p.fillRect(rect(), colorGroup().brush(TQColorGroup::Background));

    const int w = width();
    const int h = height();
    if (w <= 2 || h <= 2) return;

    const double prog = clamp01(m_prog);

    const int bd = (m_bdSize < 0) ? 0 : m_bdSize;
    const int bx = (bd / 2) + 1;
    const int by = (bd / 2) + 1;
    const int bw = w - (bd + 2);
    const int bh = h - (bd + 2);
    if (bw <= 2 || bh <= 2) return;

    const TQRect body(bx, by, bw, bh);
    const TQColor bg = blendedBgColor(prog);

#ifdef TQT_TOGGLE_MINIMAL
    if (m_mode == (int)BorderSolidSoftRectNoText) {
        TQRect sb(0, 0, w, h);
        if (sb.width() < 2 || sb.height() < 2) return;

        {
            int newW = (w * 5) / 8;
            const int minW = h + 2;
            if (newW < minW) newW = minW;
            const int x0 = (w - newW) / 2;
            sb = TQRect(x0, 0, newW, h);
            if (sb.width() < 2 || sb.height() < 2) sb = body;
        }

        const int ph = sb.height();
        if (ph <= 2) return;

        const int margin = 2;
        int side = ph - (margin * 2);
        if (side < 2) side = 2;

        int pr = ph / 6;
        if (pr < 2) pr = 2;
        if (pr > (ph / 2)) pr = ph / 2;

        int kr = pr;
        if (kr < 2) kr = 2;
        if (kr > (side / 2)) kr = side / 2;

        const int knobW = side;
        const int knobH = side;
        const int y0 = sb.top() + margin;
        const int xMin = sb.left() + margin;
        const int xMax = sb.right() - margin - knobW + 1;
        int xk = xMin + tqt_iround((double)(xMax - xMin) * prog);
        if (xk < xMin) xk = xMin;
        if (xk > xMax) xk = xMax;

        p.setPen(NoPen);
        p.setBrush(bg);
        {
            int rr = pr;
            const int rmax = ((sb.width() < sb.height()) ? sb.width() : sb.height()) / 2;
            if (rr < 0) rr = 0;
            if (rr > rmax) rr = rmax;

            if (rr <= 0) {
                p.drawRect(sb);
            } else {
                const int x0 = sb.left();
                const int y0 = sb.top();
                const int x1 = sb.right();
                const int y1 = sb.bottom();
                p.drawEllipse(x0, y0, rr * 2, rr * 2);
                p.drawEllipse(x1 - rr * 2 + 1, y0, rr * 2, rr * 2);
                p.drawEllipse(x1 - rr * 2 + 1, y1 - rr * 2 + 1, rr * 2, rr * 2);
                p.drawEllipse(x0, y1 - rr * 2 + 1, rr * 2, rr * 2);

                if (sb.width() > rr * 2) {
                    p.drawRect(x0 + rr, y0, sb.width() - rr * 2, sb.height());
                }
                if (sb.height() > rr * 2) {
                    p.drawRect(x0, y0 + rr, sb.width(), sb.height() - rr * 2);
                }
            }
        }

        p.setPen(NoPen);
        p.setBrush(TQColor(255, 255, 255));
        {
            const TQRect kb(xk, y0, knobW, knobH);
            int rr = kr;
            const int rmax = ((kb.width() < kb.height()) ? kb.width() : kb.height()) / 2;
            if (rr < 0) rr = 0;
            if (rr > rmax) rr = rmax;

            if (rr <= 0) {
                p.drawRect(kb);
            } else {
                const int x0 = kb.left();
                const int y0 = kb.top();
                const int x1 = kb.right();
                const int y1 = kb.bottom();
                p.drawEllipse(x0, y0, rr * 2, rr * 2);
                p.drawEllipse(x1 - rr * 2 + 1, y0, rr * 2, rr * 2);
                p.drawEllipse(x1 - rr * 2 + 1, y1 - rr * 2 + 1, rr * 2, rr * 2);
                p.drawEllipse(x0, y1 - rr * 2 + 1, rr * 2, rr * 2);

                if (kb.width() > rr * 2) {
                    p.drawRect(x0 + rr, y0, kb.width() - rr * 2, kb.height());
                }
                if (kb.height() > rr * 2) {
                    p.drawRect(x0, y0 + rr, kb.width(), kb.height() - rr * 2);
                }
            }
        }
        return;
    }
#endif
}

void TQtToggleSwitch::calculateGeometry() {
    const int w = width();
    const int h = height();
    const double bs = (double)m_bdSize;

    m_slideLeft = bs * 0.5;
    m_slideRight = (double)w - bs * 0.5;
}

void TQtToggleSwitch::startSwitchAnimation() {
    m_progStart = m_prog;
    m_progTarget = m_checked ? 1.0 : 0.0;
    m_animStart = TQTime::currentTime();
    m_animating = 1;
}

void TQtToggleSwitch::setProgressManual(double p) {
    m_prog = clamp01(p);
    update();
}

TQColor TQtToggleSwitch::blendedBgColor(double p) const {
    p = clamp01(p);
    const int r = (int)((double)m_off.red() + ((double)m_on.red() - (double)m_off.red()) * p);
    const int g = (int)((double)m_off.green() + ((double)m_on.green() - (double)m_off.green()) * p);
    const int b = (int)((double)m_off.blue() + ((double)m_on.blue() - (double)m_off.blue()) * p);
    return TQColor(r, g, b);
}

double TQtToggleSwitch::ease(int easing, double t) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;

    switch (easing) {
        case (int)OutCirc: {
            const double u = t - 1.0;
            return sqrt(1.0 - u * u);
        }
        case (int)OutBack: {
            const double c1 = 1.70158;
            const double c3 = c1 + 1.0;
            const double u = t - 1.0;
            return 1.0 + c3 * u * u * u + c1 * u * u;
        }
        case (int)InOutCubic:
        default:
            if (t < 0.5) return 4.0 * t * t * t;
            return 1.0 - pow(-2.0 * t + 2.0, 3.0) * 0.5;
    }
}

void TQtToggleSwitch::onAnimTick() {
    int dirty = 0;

    if (m_animating) {
        const int dt = m_animStart.msecsTo(TQTime::currentTime());
        double t = (m_animDuration <= 0) ? 1.0 : ((double)dt / (double)m_animDuration);
        if (t >= 1.0) {
            t = 1.0;
            m_animating = 0;
        }

        const double e = ease(m_easing, t);
        m_prog = m_progStart + (m_progTarget - m_progStart) * e;
        dirty = 1;
    }

    if (dirty) update();
}

#include "tqttoggleswitch.moc"
