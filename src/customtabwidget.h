#ifndef CUSTOMTABWIDGET_H
#define CUSTOMTABWIDGET_H

#include <ntqwidget.h>
#include <ntqwidgetstack.h>
#include <ntqlayout.h>
#include <ntqlabel.h>
#include <ntqvaluelist.h>
#include <ntqfont.h>
#include <ntqevent.h>
#include <ntqpainter.h>

class CustomTabButton : public TQWidget {
    TQ_OBJECT
public:
    CustomTabButton(const TQString& text, TQWidget* parent = 0);
    
    void setText(const TQString& text);
    TQString text() const { return m_text; }
    
    void setActive(bool active);
    bool isActive() const { return m_active; }

    virtual TQSize sizeHint() const;
    virtual TQSize minimumSizeHint() const;

signals:
    void clicked();

protected:
    virtual void paintEvent(TQPaintEvent* event);
    virtual void enterEvent(TQEvent* event);
    virtual void leaveEvent(TQEvent* event);
    virtual void mouseReleaseEvent(TQMouseEvent* event);

private:
    TQString m_text;
    bool m_active;
    bool m_hovered;
};

class CustomTabWidget : public TQWidget {
    TQ_OBJECT
public:
    CustomTabWidget(TQWidget* parent = 0, const char* name = 0);
    ~CustomTabWidget();

    void addTab(TQWidget* page, const TQString& label);
    int count() const;
    TQWidget* page(int index) const;
    TQString label(int index) const;
    void setTabLabel(TQWidget* page, const TQString& label);
    
    int currentPageIndex() const;
    void setCurrentPageIndex(int index);
    
    void removePage(TQWidget* w);
    
signals:
    void currentChanged(TQWidget* page);

private slots:
    void onTabClicked();

private:
    struct TabInfo {
        TQWidget* page;
        TQString origLabel;
        CustomTabButton* btn;
    };
    
    TQHBoxLayout* m_headerLayout;
    TQWidget* m_headerContainer;
    TQWidgetStack* m_stack;
    TQValueList<TabInfo> m_tabs;
    int m_currentIndex;

    void updateTabStyles();
};

#endif // CUSTOMTABWIDGET_H
