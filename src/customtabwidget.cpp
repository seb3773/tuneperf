#include "customtabwidget.h"
#include <ntqpainter.h>
#include <ntqframe.h>

CustomTabButton::CustomTabButton(const TQString& text, TQWidget* parent)
    : TQWidget(parent), m_text(text), m_active(false), m_hovered(false) {
    setMouseTracking(true);
}

void CustomTabButton::setText(const TQString& text) {
    if (m_text != text) {
        m_text = text;
        updateGeometry();
        repaint();
    }
}

void CustomTabButton::setActive(bool active) {
    if (m_active != active) {
        m_active = active;
        updateGeometry();
        repaint();
    }
}

TQSize CustomTabButton::sizeHint() const {
    TQFont f("Outfit", 10);
    f.setBold(m_active);
    TQFontMetrics fm(f);
    int w = fm.width(m_text) + 24; // 12px horizontal padding on each side
    return TQSize(w, 36);
}

TQSize CustomTabButton::minimumSizeHint() const {
    return sizeHint();
}

void CustomTabButton::paintEvent(TQPaintEvent*) {
    TQPainter p(this);
    
    TQRect r = rect();
    
    // Choose font
    TQFont f("Outfit", 10);
    f.setBold(m_active);
    p.setFont(f);
    
    // Choose text color
    TQColor textCol;
    if (m_active) {
        textCol = TQColor("#00D2FF"); // Neon Cyan
    } else if (m_hovered) {
        textCol = TQColor("#FFFFFF"); // White
    } else {
        textCol = TQColor("#A0A0AA"); // Sleek Gray
    }
    p.setPen(textCol);
    
    // Draw text centered
    p.drawText(r, TQt::AlignCenter, m_text);
    
    // Draw bottom active indicator bar
    if (m_active) {
        p.setPen(TQPen(TQColor("#00D2FF"), 2));
        p.drawLine(0, r.height() - 1, r.width(), r.height() - 1);
    }
}

void CustomTabButton::enterEvent(TQEvent*) {
    m_hovered = true;
    repaint();
}

void CustomTabButton::leaveEvent(TQEvent*) {
    m_hovered = false;
    repaint();
}

void CustomTabButton::mouseReleaseEvent(TQMouseEvent* e) {
    if (e->button() == TQt::LeftButton && rect().contains(e->pos())) {
        emit clicked();
    }
}


CustomTabWidget::CustomTabWidget(TQWidget* parent, const char* name)
    : TQWidget(parent, name), m_currentIndex(-1) {
    
    TQVBoxLayout* mainLay = new TQVBoxLayout(this, 0, 0);
    
    // 1. Header Container
    m_headerContainer = new TQWidget(this);
    m_headerContainer->setFixedHeight(36);
    m_headerLayout = new TQHBoxLayout(m_headerContainer, 0, 8); // 8px spacing between tabs
    m_headerLayout->addStretch(1);
    mainLay->addWidget(m_headerContainer);
    
    // 2. Horizontal Divider Line Frame
    TQFrame* lineFrame = new TQFrame(this);
    lineFrame->setFrameShape(TQFrame::HLine);
    lineFrame->setFrameShadow(TQFrame::Plain);
    lineFrame->setLineWidth(1);
    lineFrame->setFixedHeight(1);
    lineFrame->setPaletteForegroundColor(TQColor("#27272A")); // Sleek dark divider
    mainLay->addWidget(lineFrame);
    
    // 3. Widget Stack for pages
    m_stack = new TQWidgetStack(this);
    mainLay->addWidget(m_stack, 1); // Expand to fill space
}

CustomTabWidget::~CustomTabWidget() {
}

void CustomTabWidget::addTab(TQWidget* page, const TQString& label) {
    CustomTabButton* btn = new CustomTabButton(label, m_headerContainer);
    connect(btn, SIGNAL(clicked()), this, SLOT(onTabClicked()));
    
    m_stack->addWidget(page);
    
    TabInfo info;
    info.page = page;
    info.origLabel = label;
    info.btn = btn;
    m_tabs.append(info);
    
    // Rebuild the horizontal header layout to place buttons in order before stretch
    TQLayout* oldLay = m_headerContainer->layout();
    if (oldLay) {
        delete oldLay;
    }
    m_headerLayout = new TQHBoxLayout(m_headerContainer, 0, 8);
    for (TQValueList<TabInfo>::Iterator it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        m_headerLayout->addWidget((*it).btn);
        (*it).btn->show();
    }
    m_headerLayout->addStretch(1);
    
    if (m_tabs.count() == 1) {
        setCurrentPageIndex(0);
    } else {
        updateTabStyles();
    }
}

int CustomTabWidget::count() const {
    return m_tabs.count();
}

TQWidget* CustomTabWidget::page(int index) const {
    if (index >= 0 && index < (int)m_tabs.count()) {
        return m_tabs[index].page;
    }
    return 0;
}

TQString CustomTabWidget::label(int index) const {
    if (index >= 0 && index < (int)m_tabs.count()) {
        return m_tabs[index].origLabel;
    }
    return TQString::null;
}

void CustomTabWidget::setTabLabel(TQWidget* page, const TQString& label) {
    for (TQValueList<TabInfo>::Iterator it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        if ((*it).page == page) {
            (*it).origLabel = label;
            TQString displayLabel = label;
            TQString bullet = TQString::fromUtf8("• ");
            if (displayLabel.startsWith(bullet)) {
                displayLabel = displayLabel.mid(bullet.length());
            }
            (*it).btn->setText(displayLabel);
            return;
        }
    }
}

int CustomTabWidget::currentPageIndex() const {
    return m_currentIndex;
}

void CustomTabWidget::setCurrentPageIndex(int index) {
    if (index < 0 || index >= (int)m_tabs.count()) return;
    
    m_currentIndex = index;
    m_stack->raiseWidget(m_tabs[index].page);
    updateTabStyles();
    
    emit currentChanged(m_tabs[index].page);
}

void CustomTabWidget::removePage(TQWidget* w) {
    for (TQValueList<TabInfo>::Iterator it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        if ((*it).page == w) {
            delete (*it).btn;
            m_stack->removeWidget((*it).page);
            
            m_tabs.remove(it);
            
            if (m_currentIndex >= (int)m_tabs.count()) {
                m_currentIndex = (int)m_tabs.count() - 1;
            }
            if (m_currentIndex < 0) m_currentIndex = 0;
            
            // Rebuild layout without the deleted button
            TQLayout* oldLay = m_headerContainer->layout();
            if (oldLay) {
                delete oldLay;
            }
            m_headerLayout = new TQHBoxLayout(m_headerContainer, 0, 8);
            for (TQValueList<TabInfo>::Iterator jt = m_tabs.begin(); jt != m_tabs.end(); ++jt) {
                m_headerLayout->addWidget((*jt).btn);
                (*jt).btn->show();
            }
            m_headerLayout->addStretch(1);
            
            if (m_tabs.count() > 0) {
                m_stack->raiseWidget(m_tabs[m_currentIndex].page);
            }
            
            updateTabStyles();
            return;
        }
    }
}

void CustomTabWidget::onTabClicked() {
    CustomTabButton* clickedBtn = dynamic_cast<CustomTabButton*>(const_cast<TQObject*>(sender()));
    if (!clickedBtn) return;
    
    for (int i = 0; i < (int)m_tabs.count(); ++i) {
        if (m_tabs[i].btn == clickedBtn) {
            setCurrentPageIndex(i);
            return;
        }
    }
}

void CustomTabWidget::updateTabStyles() {
    for (int i = 0; i < (int)m_tabs.count(); ++i) {
        m_tabs[i].btn->setActive(i == m_currentIndex);
    }
}

#include "customtabwidget.moc"

