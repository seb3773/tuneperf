/*
 *  TQtSlidingStackedWidget - Animated sliding page widget for TQt3
 *  Ported from SlidingStackedWidget (MIT License, Tim Schneeberger)
 *
 *  Uses TQWidgetStack with a TQTimer-driven animation engine
 *  to slide between child pages with smooth easing.
 */
#ifndef TQTSLIDINGSTACKEDWIDGET_H
#define TQTSLIDINGSTACKEDWIDGET_H

#include <ntqwidgetstack.h>
#include <ntqtimer.h>
#include <ntqpoint.h>
#include <ntqptrlist.h>

class TQtSlidingStackedWidget : public TQWidgetStack {
    TQ_OBJECT

public:
    enum Direction {
        Left2Right,
        Right2Left,
        Top2Bottom,
        Bottom2Top,
        Automatic
    };

    TQtSlidingStackedWidget(TQWidget *parent = 0, const char *name = 0);
    ~TQtSlidingStackedWidget();

    int addWidget(TQWidget *w);
    int count() const;
    int currentIndex() const;
    TQWidget *currentWidget() const;
    TQWidget *widgetAt(int index) const;

public slots:
    void setSpeed(int speed);
    void setVerticalMode(bool vertical = true);
    void setWrap(bool wrap);

    bool slideInNext();
    bool slideInPrev();
    void slideInIdx(int idx, Direction direction = Automatic);

signals:
    void animationFinished();

protected slots:
    void onTimerTick();

private:
    double easeOutQuart(double t);

    TQPtrList<TQWidget> m_widgets;
    TQTimer *m_timer;

    int m_speed;       // total animation duration in ms
    bool m_vertical;
    bool m_wrap;
    bool m_active;

    int m_nowIdx;
    int m_nextIdx;

    TQPoint m_startPosNow;
    TQPoint m_endPosNow;
    TQPoint m_startPosNext;
    TQPoint m_endPosNext;

    int m_elapsed;
    static const int TICK_MS = 16; // ~60 FPS
};

#endif // TQTSLIDINGSTACKEDWIDGET_H
