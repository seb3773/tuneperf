#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <ntqmainwindow.h>
#include <ntqlabel.h>
#include <ntqpushbutton.h>
#include <ntqlistview.h>
#include <ntqcombobox.h>
#include <ntqcheckbox.h>
#include <ntqtextedit.h>
#include "customtabwidget.h"
#include <ntqscrollview.h>
#include <ntqlayout.h>
#include <ntqgroupbox.h>
#include <ntqframe.h>
#include "tqtslidingstackedwidget.h"
#include "tqttoggleswitch.h"
#include "tuneperf_runner.h"
#include "embedded_icons.h"

class TQtProgressCircle2;

class MainWindow : public TQMainWindow {
    TQ_OBJECT
public:
    MainWindow(TQWidget* parent = 0, const char* name = 0, const TQString& adminPassword = "");
    ~MainWindow();

protected:
    void resizeEvent(TQResizeEvent* e);

private slots:
    // Navigation
    void goNext();
    void goBack();
    void startTuningApply();
    void startRestore();

    // Event handlers from Runner
    void onDryRunFinished(bool success);
    void onApplyFinished(bool success);
    void onRestoreFinished(bool success);
    void onLogMessage(const TQString& msg);

    // Profile selections
    void onRoleChanged(TQListViewItem* item);
    
    void onExperimentalStateChanged(bool checked);
    void onCStatesStateChanged(bool checked);
    void onReviewSwitchToggled(bool checked);
    void onLogToggleChanged(bool checked);
    void onTabChanged(TQWidget* w);
    void slotLanguageClicked();
    void slotGithubClicked();
    void slotAboutClicked();

private:
    void createSidebar();
    void createWizardPages();
    void updateNavigationButtons();
    void populateHardwareInfo();
    void populateParametersTree();
    void saveUserCheckedParameters();
    void loadProfileSelection();
    void updateLogToggleLabels();
    void updateTexts();
    void populateRoles();

    // Wizard Screens
    TQWidget* createWelcomePage();
    TQWidget* createRolePage();
    TQWidget* createUsagePage();
    TQWidget* createPolicyPage();
    TQWidget* createReviewPage();
    TQWidget* createApplyPage();

    // Global state / Runner
    TunePerfRunner* m_runner;
    int m_currentPage;

    // UI Layout Widgets
    TQtSlidingStackedWidget* m_wizardStack;
    TQWidget* m_sidebarPanel;
    TQStringList m_sidebarSteps;
    TQValueList<TQLabel*> m_sidebarLabels;
    TQValueList<TQFrame*> m_sidebarFrames;
    TQValueList<TQLabel*> m_sidebarIconLabels;

    // Navigation buttons
    TQPushButton* m_btnBack;
    TQPushButton* m_btnNext;
    TQPushButton* m_btnRestore;
    TQPushButton* m_btnExit;

    // Page 0: Welcome
    TQLabel* m_lblWelcomeTitle;
    TQLabel* m_lblWelcomeDesc;
    TQGroupBox* m_gbHardwareBox;
    TQLabel* m_lblHardwareInfo;

    // Page 1: Role
    TQLabel* m_lblRoleTitle;
    TQLabel* m_lblRoleDesc;
    TQListView* m_lvRoles;

    // Page 2: Usage
    TQLabel* m_lblUsageTitle;
    TQLabel* m_lblUsageDesc;
    TQListView* m_lvUsages;

    // Page 3: Advanced
    TQLabel* m_lblPolicyTitle;
    TQLabel* m_lblIPv6;
    TQWidget* m_tab1Container;
    TQWidget* m_tab2Container;
    TQWidget* m_tab3Container;
    TQComboBox* m_cbIPv6;
    CustomTabWidget* m_tabAdvanced;
    TQtToggleSwitch* m_chkExperimental;
    TQtToggleSwitch* m_chkDisableCStates;
    TQLabel* m_lblExperimental;
    TQLabel* m_lblDisableCStates;
    
    TQtToggleSwitch* m_chkDisableJournald;
    TQtToggleSwitch* m_chkDisableRsyslog;
    TQtToggleSwitch* m_chkDisableXorg;
    TQtToggleSwitch* m_chkDisableBoot;
    TQtToggleSwitch* m_chkDisablePam;
    TQLabel* m_lblDisableJournald;
    TQLabel* m_lblDisableRsyslog;
    TQLabel* m_lblDisableXorg;
    TQLabel* m_lblDisableBoot;
    TQLabel* m_lblDisablePam;
    TQLabel* m_lblPolicyHelp;

    // Misc Toggles (Page 3 - Tab 3)
    TQtToggleSwitch* m_chkDisableCoredumps;
    TQtToggleSwitch* m_chkDisableModemManager;
    TQtToggleSwitch* m_chkDisableNmWait;
    TQtToggleSwitch* m_chkDisableSmbd;
    TQtToggleSwitch* m_chkDisableNmbd;
    TQtToggleSwitch* m_chkDisableSerialGetty;
    TQtToggleSwitch* m_chkDisableColord;
    TQtToggleSwitch* m_chkDisableSmartd;
    TQtToggleSwitch* m_chkDisableUsbLegacy;
    TQtToggleSwitch* m_chkDisablePcspkr;
    TQtToggleSwitch* m_chkDisableBluetooth;
    TQtToggleSwitch* m_chkDisablePrint;
    TQtToggleSwitch* m_chkDisableApparmor;
    TQtToggleSwitch* m_chkDisableUfw;
    TQtToggleSwitch* m_chkDisableBluezCups;

    TQLabel* m_lblDisableCoredumps;
    TQLabel* m_lblDisableModemManager;
    TQLabel* m_lblDisableNmWait;
    TQLabel* m_lblDisableSmbd;
    TQLabel* m_lblDisableNmbd;
    TQLabel* m_lblDisableSerialGetty;
    TQLabel* m_lblDisableColord;
    TQLabel* m_lblDisableSmartd;
    TQLabel* m_lblDisableUsbLegacy;
    TQLabel* m_lblDisablePcspkr;
    TQLabel* m_lblDisableBluetooth;
    TQLabel* m_lblDisablePrint;
    TQLabel* m_lblDisableApparmor;
    TQLabel* m_lblDisableUfw;
    TQLabel* m_lblDisableBluezCups;

    // Page 4: Review / Parameter list
    TQLabel* m_lblReviewTitle;
    TQLabel* m_lblReviewDesc;
    TQWidget* m_wSwappiness;
    TQWidget* m_wPerfs;
    TQWidget* m_wNetwork;
    TQWidget* m_wSecurity;
    TQWidget* m_wModules;
    TQWidget* m_wSysfs;
    CustomTabWidget* m_tabReview;
    TQValueList<TQtToggleSwitch*> m_reviewSwitches;
    
    // Mapping from switches to TuneParam references
    struct SwitchMap {
        TQtToggleSwitch* sw;
        TQLabel* lbl;
        TuneParam* param;
    };
    TQValueList<SwitchMap> m_paramMap;

    struct ScriptLineMap {
        TQtToggleSwitch* sw;
        TQLabel* lbl;
        ScriptLine* line;
    };
    TQValueList<ScriptLineMap> m_scriptLineMap;

    // Page 5: Apply
    TQLabel* m_lblApplyTitle;
    TQTextEdit* m_txtLog;
    TQLabel* m_lblApplyStatus;
    TQtProgressCircle2* m_progressCircle;
    TQString m_logBuffer;

    TQLabel* m_lblSidebarSubtitle;
    TQLabel* m_lblRootText;

    // Utility buttons & normal/hover icons
    TQPushButton* m_btnLangue;
    TQPushButton* m_btnGithub;
    TQPushButton* m_btnAbout;
    TQPixmap m_pixLangueNormal;
    TQPixmap m_pixLangueHover;
    TQPixmap m_pixGithubNormal;
    TQPixmap m_pixGithubHover;
    TQPixmap m_pixAboutNormal;
    TQPixmap m_pixAboutHover;


    TQLabel* m_lblHelpDescription;
    TQString m_adminPassword;

public:
    static TQPixmap loadRawIcon(int id);
    virtual bool eventFilter(TQObject* watched, TQEvent* event);
};

#endif // MAINWINDOW_H
