/*
 *  TQtSlidingStackedWidget - Animated sliding page widget for TQt3
 *  Ported from SlidingStackedWidget (MIT License, Tim Schneeberger)
 */
#include "tqtslidingstackedwidget.h"

TQtSlidingStackedWidget::TQtSlidingStackedWidget(TQWidget *parent, const char *name)
    : TQWidgetStack(parent, name)
{
    m_speed = 300;
    m_vertical = false;
    m_wrap = false;
    m_active = false;
    m_nowIdx = 0;
    m_nextIdx = 0;
    m_elapsed = 0;

    m_timer = new TQTimer(this);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(onTimerTick()));
}

TQtSlidingStackedWidget::~TQtSlidingStackedWidget()
{
}

int TQtSlidingStackedWidget::addWidget(TQWidget *w)
{
    int id = m_widgets.count();
    m_widgets.append(w);
    TQWidgetStack::addWidget(w, id);
    if (id == 0) {
        raiseWidget(w);
    } else {
        w->hide();
    }
    return id;
}

int TQtSlidingStackedWidget::count() const
{
    return (int)m_widgets.count();
}

int TQtSlidingStackedWidget::currentIndex() const
{
    return m_nowIdx;
}

TQWidget *TQtSlidingStackedWidget::currentWidget() const
{
    if (m_nowIdx >= 0 && m_nowIdx < (int)m_widgets.count())
        return const_cast<TQPtrList<TQWidget>&>(m_widgets).at(m_nowIdx);
    return 0;
}

TQWidget *TQtSlidingStackedWidget::widgetAt(int index) const
{
    if (index >= 0 && index < (int)m_widgets.count())
        return const_cast<TQPtrList<TQWidget>&>(m_widgets).at(index);
    return 0;
}

void TQtSlidingStackedWidget::setSpeed(int speed)
{
    m_speed = speed;
}

void TQtSlidingStackedWidget::setVerticalMode(bool vertical)
{
    m_vertical = vertical;
}

void TQtSlidingStackedWidget::setWrap(bool wrap)
{
    m_wrap = wrap;
}

bool TQtSlidingStackedWidget::slideInNext()
{
    int now = m_nowIdx;
    if (m_wrap || (now < count() - 1)) {
        slideInIdx(now + 1);
        return true;
    }
    return false;
}

bool TQtSlidingStackedWidget::slideInPrev()
{
    int now = m_nowIdx;
    if (m_wrap || (now > 0)) {
        slideInIdx(now - 1);
        return true;
    }
    return false;
}

void TQtSlidingStackedWidget::slideInIdx(int idx, Direction direction)
{
    if (m_active) return;

    int c = count();
    if (c <= 0) return;

    // Wrap index
    if (idx > c - 1) {
        if (direction == Automatic)
            direction = m_vertical ? Top2Bottom : Right2Left;
        idx = idx % c;
    } else if (idx < 0) {
        if (direction == Automatic)
            direction = m_vertical ? Bottom2Top : Left2Right;
        idx = (idx + c) % c;
    }

    if (idx == m_nowIdx) return;

    // Determine direction hint
    if (direction == Automatic) {
        if (m_nowIdx < idx)
            direction = m_vertical ? Top2Bottom : Right2Left;
        else
            direction = m_vertical ? Bottom2Top : Left2Right;
    }

    m_active = true;
    m_nextIdx = idx;
    m_elapsed = 0;

    int w = width();
    int h = height();

    TQWidget *nowWidget = m_widgets.at(m_nowIdx);
    TQWidget *nextWidget = m_widgets.at(m_nextIdx);

    // Compute offsets based on direction
    int offsetX = 0, offsetY = 0;
    switch (direction) {
        case Left2Right:  offsetX =  w; offsetY = 0; break;
        case Right2Left:  offsetX = -w; offsetY = 0; break;
        case Top2Bottom:  offsetX = 0; offsetY =  h; break;
        case Bottom2Top:  offsetX = 0; offsetY = -h; break;
        default: break;
    }

    // Current widget starts at (0,0) and slides out to (offsetX, offsetY)
    m_startPosNow = TQPoint(0, 0);
    m_endPosNow = TQPoint(offsetX, offsetY);

    // Next widget starts offscreen at (-offsetX, -offsetY) and slides to (0,0)
    m_startPosNext = TQPoint(-offsetX, -offsetY);
    m_endPosNext = TQPoint(0, 0);

    // Position the next widget offscreen and show it
    nextWidget->setGeometry(-offsetX, -offsetY, w, h);
    nextWidget->show();
    nextWidget->raise();

    // Start the animation timer
    m_timer->start(TICK_MS);
}

double TQtSlidingStackedWidget::easeOutQuart(double t)
{
    // Ease-Out Quartic: 1 - (1-t)^4
    double u = 1.0 - t;
    return 1.0 - u * u * u * u;
}

void TQtSlidingStackedWidget::onTimerTick()
{
    m_elapsed += TICK_MS;

    double progress = (double)m_elapsed / (double)m_speed;
    if (progress >= 1.0) progress = 1.0;

    double eased = easeOutQuart(progress);

    TQWidget *nowWidget = m_widgets.at(m_nowIdx);
    TQWidget *nextWidget = m_widgets.at(m_nextIdx);

    // Interpolate positions
    int nowX = m_startPosNow.x() + (int)(eased * (m_endPosNow.x() - m_startPosNow.x()));
    int nowY = m_startPosNow.y() + (int)(eased * (m_endPosNow.y() - m_startPosNow.y()));
    int nextX = m_startPosNext.x() + (int)(eased * (m_endPosNext.x() - m_startPosNext.x()));
    int nextY = m_startPosNext.y() + (int)(eased * (m_endPosNext.y() - m_startPosNext.y()));

    nowWidget->move(nowX, nowY);
    nextWidget->move(nextX, nextY);

    if (progress >= 1.0) {
        // Animation complete
        m_timer->stop();

        nowWidget->hide();
        nowWidget->move(0, 0);

        nextWidget->move(0, 0);
        raiseWidget(nextWidget);

        m_nowIdx = m_nextIdx;
        m_active = false;

        emit animationFinished();
    }
}

#include "tqtslidingstackedwidget.moc"
