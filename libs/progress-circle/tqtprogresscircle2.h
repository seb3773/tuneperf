#ifndef TQTPROGRESSCIRCLE2_H
#define TQTPROGRESSCIRCLE2_H

#include <ntqwidget.h>
#include <ntqcolor.h>
#include <ntqstring.h>

class TQTimer;

class TQtProgressCircle2 : public TQWidget {
    TQ_OBJECT
public:
    TQtProgressCircle2(TQWidget* parent = 0, const char* name = 0);

    TQSize sizeHint() const;
    TQSizePolicy sizePolicy() const;

    // Value API
    float value() const;
    float maximum() const;

    // Spinning Mode API
    bool isSpinning() const;
    float spinSpeed() const;
    float spinBarLength() const;

    // Appearance API
    int barWidth() const;
    int rimWidth() const;
    TQColor barColor() const;
    TQColor rimColor() const;
    
    // Block Mode API
    int blockCount() const;
    float blockScale() const;
    
    // Text API
    TQString text() const;
    TQString unit() const;
    TQColor textColor() const;
    TQColor unitColor() const;
    int textSize() const;     // 0 means auto
    int unitSize() const;     // 0 means auto
    bool showPercent() const;
    bool showValue() const;

public slots:
    void setValue(float v);
    void setMaximum(float m);
    
    void spin();
    void stopSpinning();
    void setSpinSpeed(float s);
    void setSpinBarLength(float lenAngle);

    void setBarWidth(int w);
    void setRimWidth(int w);
    void setBarColor(const TQColor& c);
    void setRimColor(const TQColor& c);

    void setBlockCount(int count);
    void setBlockScale(float scale);

    void setText(const TQString& t);
    void setUnit(const TQString& u);
    void setTextColor(const TQColor& c);
    void setUnitColor(const TQColor& c);
    void setTextSize(int s);
    void setUnitSize(int s);
    void setShowPercent(bool on);
    void setShowValue(bool on);

signals:
    void valueChanged(float);
    void maximumChanged(float);

protected:
    void paintEvent(TQPaintEvent*);
    void drawBlocks(TQPainter& p, const TQRect& rect, int w, double startDeg, double spanDeg, const TQColor& c);

private slots:
    void onTick();

private:
    void updateTimers();

private:
    float m_value;
    float m_maximum;
    
    bool m_isSpinning;
    float m_spinSpeed;
    float m_spinBarLength;
    double m_spinPhase;

    int m_barWidth;
    int m_rimWidth;
    TQColor m_barColor;
    TQColor m_rimColor;
    
    int m_blockCount;
    float m_blockScale;

    TQString m_text;
    TQString m_unit;
    TQColor m_textColor;
    TQColor m_unitColor;
    int m_textSize;
    int m_unitSize;
    bool m_showPercent;
    bool m_showValue;

    float m_visibleValue;
    float m_targetValue;

    int m_animatingValue;
    unsigned int m_valueAnimStartMs;

    TQTimer* m_timer;
};

#endif
