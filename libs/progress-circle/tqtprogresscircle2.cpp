#include "tqtprogresscircle2.h"

#include <ntqpainter.h>
#include <ntqfont.h>
#include <ntqfontmetrics.h>
#include <ntqpen.h>
#include <ntqtimer.h>

#include <math.h>
#include <time.h>

static unsigned int tqt_msec_monotonic2() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned int)((unsigned long long)ts.tv_sec * 1000ULL + (unsigned long long)ts.tv_nsec / 1000000ULL);
}

static inline double tqt_clamp01_2(double x) {
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

TQtProgressCircle2::TQtProgressCircle2(TQWidget* parent, const char* name)
    : TQWidget(parent, name),
      m_value(0),
      m_maximum(100),
      m_isSpinning(false),
      m_spinSpeed(2.8f),
      m_spinBarLength(42.0f),
      m_spinPhase(0.0),
      m_barWidth(4),
      m_rimWidth(4),
      m_barColor(0, 150, 136),
      m_rimColor(131, 208, 201),
      m_blockCount(1),
      m_blockScale(0.9f),
      m_text(),
      m_unit(),
      m_textColor(0, 0, 0),
      m_unitColor(0, 0, 0),
      m_textSize(0),
      m_unitSize(0),
      m_showPercent(false),
      m_showValue(false),
      m_visibleValue(0),
      m_targetValue(0),
      m_animatingValue(0),
      m_valueAnimStartMs(0),
      m_timer(new TQTimer(this)) {
    connect(m_timer, SIGNAL(timeout()), this, SLOT(onTick()));
    setBackgroundMode(PaletteBackground);
    setMinimumSize(8, 8);
}

TQSize TQtProgressCircle2::sizeHint() const {
    return TQSize(30, 30);
}

TQSizePolicy TQtProgressCircle2::sizePolicy() const {
    return TQSizePolicy(TQSizePolicy::Fixed, TQSizePolicy::Fixed);
}

float TQtProgressCircle2::value() const { return m_value; }
float TQtProgressCircle2::maximum() const { return m_maximum; }
bool TQtProgressCircle2::isSpinning() const { return m_isSpinning; }
float TQtProgressCircle2::spinSpeed() const { return m_spinSpeed; }
float TQtProgressCircle2::spinBarLength() const { return m_spinBarLength; }
int TQtProgressCircle2::barWidth() const { return m_barWidth; }
int TQtProgressCircle2::rimWidth() const { return m_rimWidth; }
TQColor TQtProgressCircle2::barColor() const { return m_barColor; }
TQColor TQtProgressCircle2::rimColor() const { return m_rimColor; }
int TQtProgressCircle2::blockCount() const { return m_blockCount; }
float TQtProgressCircle2::blockScale() const { return m_blockScale; }
TQString TQtProgressCircle2::text() const { return m_text; }
TQString TQtProgressCircle2::unit() const { return m_unit; }
TQColor TQtProgressCircle2::textColor() const { return m_textColor; }
TQColor TQtProgressCircle2::unitColor() const { return m_unitColor; }
int TQtProgressCircle2::textSize() const { return m_textSize; }
int TQtProgressCircle2::unitSize() const { return m_unitSize; }
bool TQtProgressCircle2::showPercent() const { return m_showPercent; }
bool TQtProgressCircle2::showValue() const { return m_showValue; }

void TQtProgressCircle2::setValue(float v) {
    if (v < 0) v = 0;
    if (m_value == v) return;

    m_value = v;
    m_targetValue = v;
    m_animatingValue = 1;
    m_valueAnimStartMs = tqt_msec_monotonic2();
    emit valueChanged(v);
    updateTimers();
    update();
}

void TQtProgressCircle2::setMaximum(float m) {
    if (m < 0) m = 0;
    if (m_maximum == m) return;

    m_maximum = m;
    if (m_maximum > 0 && m_visibleValue > m_maximum) m_visibleValue = m_maximum;
    emit maximumChanged(m);
    update();
}

void TQtProgressCircle2::spin() {
    if (m_isSpinning) return;
    m_isSpinning = true;
    updateTimers();
    update();
}

void TQtProgressCircle2::stopSpinning() {
    if (!m_isSpinning) return;
    m_isSpinning = false;
    updateTimers();
    update();
}

void TQtProgressCircle2::setSpinSpeed(float s) { m_spinSpeed = s; }
void TQtProgressCircle2::setSpinBarLength(float lenAngle) { m_spinBarLength = lenAngle; update(); }

void TQtProgressCircle2::setBarWidth(int w) { m_barWidth = w; update(); }
void TQtProgressCircle2::setRimWidth(int w) { m_rimWidth = w; update(); }
void TQtProgressCircle2::setBarColor(const TQColor& c) { m_barColor = c; update(); }
void TQtProgressCircle2::setRimColor(const TQColor& c) { m_rimColor = c; update(); }

void TQtProgressCircle2::setBlockCount(int count) { m_blockCount = count; update(); }
void TQtProgressCircle2::setBlockScale(float scale) { m_blockScale = scale; update(); }

void TQtProgressCircle2::setText(const TQString& t) { m_text = t; update(); }
void TQtProgressCircle2::setUnit(const TQString& u) { m_unit = u; update(); }
void TQtProgressCircle2::setTextColor(const TQColor& c) { m_textColor = c; update(); }
void TQtProgressCircle2::setUnitColor(const TQColor& c) { m_unitColor = c; update(); }
void TQtProgressCircle2::setTextSize(int s) { m_textSize = s; update(); }
void TQtProgressCircle2::setUnitSize(int s) { m_unitSize = s; update(); }
void TQtProgressCircle2::setShowPercent(bool on) { m_showPercent = on; update(); }
void TQtProgressCircle2::setShowValue(bool on) { m_showValue = on; update(); }

void TQtProgressCircle2::updateTimers() {
    if (m_isSpinning || m_animatingValue) {
        if (!m_timer->isActive()) m_timer->start(16); // ~60fps
    } else {
        m_timer->stop();
    }
}

void TQtProgressCircle2::onTick() {
    unsigned int now = tqt_msec_monotonic2();
    bool needUpdate = false;

    if (m_isSpinning) {
        m_spinPhase += m_spinSpeed;
        if (m_spinPhase >= 360.0) m_spinPhase -= 360.0;
        needUpdate = true;
    }

    if (m_animatingValue) {
        const unsigned int dt = now - m_valueAnimStartMs;
        if (dt >= 400U) {
            m_visibleValue = m_targetValue;
            m_animatingValue = 0;
            updateTimers();
            needUpdate = true;
        } else {
            double t = (double)dt / 400.0;
            t = tqt_clamp01_2(t);
            // easeInOut cubic or quad
            const double s = t < 0.5 ? 2.0 * t * t : -1.0 + (4.0 - 2.0 * t) * t;

            const float from = m_visibleValue;
            const float to = m_targetValue;
            const float nv = from + (float)((to - from) * s);

            double diff = nv - m_visibleValue;
            if (diff < 0) diff = -diff;
            if (diff > 0.1) {
                m_visibleValue = nv;
                needUpdate = true;
            }
        }
    }

    if (needUpdate) {
        update();
    }
}

static inline TQRect tqt_square_rect2(const TQRect& r) {
    if (r.width() > r.height()) {
        const int diff = r.width() - r.height();
        return TQRect(r.x() + diff / 2, r.y(), r.width() - diff, r.height());
    }
    if (r.height() > r.width()) {
        const int diff = r.height() - r.width();
        return TQRect(r.x(), r.y() + diff / 2, r.width(), r.height() - diff);
    }
    return r;
}

void TQtProgressCircle2::drawBlocks(TQPainter& p, const TQRect& rect, int w, double startDeg, double spanDeg, const TQColor& c) {
    if (m_blockCount <= 1) {
        TQPen pen(c);
        pen.setWidth(w);
        pen.setCapStyle(TQt::FlatCap);
        p.setPen(pen);
        p.setBrush(NoBrush);
        p.drawArc(rect, (int)floor(startDeg * 16.0 + 0.5), (int)floor(spanDeg * 16.0 + 0.5));
        return;
    }

    double blockDeg = 360.0 / m_blockCount;
    double blockScaleDeg = blockDeg * m_blockScale;

    TQPen pen(c);
    pen.setWidth(w);
    pen.setCapStyle(TQt::FlatCap);
    p.setPen(pen);
    p.setBrush(NoBrush);

    double endDeg = startDeg + spanDeg;
    double minDeg = startDeg < endDeg ? startDeg : endDeg;
    double maxDeg = startDeg > endDeg ? startDeg : endDeg;

    for (int i = 0; i < m_blockCount; ++i) {
        double bStart = i * blockDeg;
        double bSpan = blockScaleDeg;
        double bEnd = bStart + bSpan;

        // Simplify block intersection (assuming solid blocks for now if any part is within span)
        // Adjust coordinate system: Qt starts 0 at 3 o'clock, counter-clockwise.
        // Usually we want blocks fixed. For determinate progress, arc starts at 90 deg and goes negative (clockwise).
        // Let's just draw the active blocks.
        
        // Convert block start/end to the logic range.
        // It's easier to just draw the intersected arc for each block.
        double drawStart = bStart;
        double drawEnd = bEnd;

        // Move to span's reference frame:
        // Actually, a simpler way is to check if the center of the block is within the progress arc.
        double bCenter = bStart + bSpan / 2.0;

        // Handle modulo 360 logic for checking if bCenter is inside the arc
        // This is a naive approach. Since span is usually negative (clockwise):
        double aStart = startDeg;
        double aEnd = startDeg + spanDeg;
        double lStart = aStart < aEnd ? aStart : aEnd;
        double lEnd = aStart > aEnd ? aStart : aEnd;
        
        // Normalize
        while (lStart < 0) { lStart += 360; lEnd += 360; }
        
        bool inside = false;
        double testCenter = bCenter;
        while (testCenter < lStart) testCenter += 360;
        if (testCenter >= lStart && testCenter <= lEnd) inside = true;

        if (inside) {
            p.drawArc(rect, (int)floor(drawStart * 16.0 + 0.5), (int)floor(bSpan * 16.0 + 0.5));
        }
    }
}

void TQtProgressCircle2::paintEvent(TQPaintEvent*) {
    TQPainter p(this);

    p.fillRect(rect(), colorGroup().brush(TQColorGroup::Background));

    TQRect sr = tqt_square_rect2(rect());
    int padding = TQMAX(m_barWidth, m_rimWidth) / 2 + 1;
    sr.addCoords(padding, padding, -padding, -padding);
    if (sr.width() <= 4 || sr.height() <= 4) return;

    // Draw rim
    if (m_rimWidth > 0) {
        if (m_blockCount > 1) {
            drawBlocks(p, sr, m_rimWidth, 0.0, 360.0, m_rimColor);
        } else {
            TQPen pen(m_rimColor);
            pen.setWidth(m_rimWidth);
            pen.setCapStyle(TQt::FlatCap);
            p.setPen(pen);
            p.setBrush(NoBrush);
            p.drawArc(sr, 0, 360 * 16);
        }
    }

    // Draw bar
    if (m_barWidth > 0) {
        double startDeg = 90.0; // 12 o'clock in TQt is 90 deg. (0 is 3 o'clock)
        double spanDeg = 0;

        if (m_isSpinning) {
            startDeg = 90.0 - m_spinPhase;
            spanDeg = -m_spinBarLength;
        } else if (m_maximum > 0) {
            float v = m_visibleValue;
            if (v < 0) v = 0;
            if (v > m_maximum) v = m_maximum;
            spanDeg = -((double)v * 360.0 / (double)m_maximum);
        }

        if (spanDeg != 0.0) {
            drawBlocks(p, sr, m_barWidth, startDeg, spanDeg, m_barColor);
        }
    }

    // Draw text
    TQString s = m_text;
    if (m_showValue) {
        if (!s.isEmpty()) s += "\n";
        s += TQString::number((int)m_visibleValue);
    } else if (m_showPercent && m_maximum > 0) {
        float v = m_visibleValue;
        if (v < 0) v = 0;
        if (v > m_maximum) v = m_maximum;
        int pct = (int)floor(((double)v * 100.0) / (double)m_maximum + 0.5);
        if (!s.isEmpty()) s += "\n";
        s += TQString::number(pct);
    }

    if (!s.isEmpty()) {
        TQFont f = font();
        f.setBold(true);
        if (m_textSize > 0) {
            f.setPixelSize(m_textSize);
        } else {
            int px = sr.width() / 4;
            if (px < 8) px = 8;
            f.setPixelSize(px);
            
            // Auto down-scale to fit text inside the inner circle bounds
            TQFontMetrics fm(f);
            int maxWidth = (int)(sr.width() * 0.8);
            TQString fullText = s;
            if (!m_unit.isEmpty()) fullText += m_unit;
            
            while (px > 8 && fm.width(fullText) > maxWidth) {
                px -= 2;
                if (px < 8) px = 8;
                f.setPixelSize(px);
                fm = TQFontMetrics(f);
            }
        }
        
        // Simple rendering without unit for now, or append unit
        if (!m_unit.isEmpty()) {
            s += m_unit; // Ideally unit is drawn with different size, but for standard version we can append.
        }

        p.setFont(f);
        p.setPen(m_textColor);
        p.drawText(sr, TQt::AlignCenter, s);
    }
}

#include "tqtprogresscircle2.moc"
