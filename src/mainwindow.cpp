#include "mainwindow.h"
#include <ntqapplication.h>
#include <ntqpainter.h>
#include <ntqfont.h>
#include <ntqcolor.h>
#include <ntqdir.h>
#include <ntqfile.h>
#include <ntqtextstream.h>
#include <ntqmessagebox.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

#include <ntqevent.h>
#include <ntqmap.h>
#include <ntqimage.h>
#include "translationmanager.h"
#include <ntqpopupmenu.h>
#include <ntqdialog.h>
#include <ntqcursor.h>
#include "tqtprogresscircle2.h"

static void openUrl(const TQString& url) {
    // Since the GUI is run as the normal user, we can call xdg-open directly!
    TQString cmd = TQString("xdg-open \"%1\" &").arg(url);
    ::system(cmd.latin1());
}

static TQPixmap brightenPixmap(const TQPixmap& pix, double factor = 1.3) {
    TQImage img = pix.convertToImage();
    if (img.isNull()) return pix;
    
    if (img.depth() != 32) {
        img = img.convertDepth(32);
    }
    
    int w = img.width();
    int h = img.height();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            TQRgb p = img.pixel(x, y);
            int a = tqAlpha(p);
            int r = int(tqRed(p) * factor); if (r > 255) r = 255;
            int g = int(tqGreen(p) * factor); if (g > 255) g = 255;
            int b = int(tqBlue(p) * factor); if (b > 255) b = 255;
            img.setPixel(x, y, tqRgba(r, g, b, a));
        }
    }
    TQPixmap result;
    result.convertFromImage(img);
    return result;
}

static TQWidget* createScrollContainer(CustomTabWidget* tab, const TQString& tabTitle, TQVBoxLayout*& outLayout, TQObject* eventFilterObj = NULL);

static TQString getServiceCurrentState(const TQString& serviceName) {
    TQString cmd = "systemctl is-active " + serviceName + " 2>/dev/null";
    FILE* f = popen(cmd.latin1(), "r");
    if (!f) return "Enabled";
    char buf[256];
    if (fgets(buf, sizeof(buf), f)) {
        TQString res(buf);
        res = res.stripWhiteSpace();
        pclose(f);
        if (res == "active") {
            return "Enabled";
        }
    } else {
        pclose(f);
    }
    return "Disabled";
}

static TQString getCoredumpCurrentState() {
    TQFile f("/proc/sys/fs/suid_dumpable");
    if (f.open(IO_ReadOnly)) {
        TQString val = TQTextStream(&f).readLine().stripWhiteSpace();
        f.close();
        if (val == "0") return "Disabled";
    }
    return "Enabled";
}

static TQString getUsbLegacyCurrentState() {
    TQFile f("/etc/modprobe.d/tuneperf-blacklist.conf");
    if (f.open(IO_ReadOnly)) {
        TQString content = TQTextStream(&f).read();
        f.close();
        if (content.contains("blacklist usbmouse") && content.contains("blacklist usbkbd")) {
            return "Disabled";
        }
    }
    return "Enabled";
}

static TQString getPcspkrCurrentState() {
    TQFile f("/etc/modprobe.d/tuneperf-blacklist.conf");
    if (f.open(IO_ReadOnly)) {
        TQString content = TQTextStream(&f).read();
        f.close();
        if (content.contains("blacklist pcspkr")) {
            return "Disabled";
        }
    }
    return "Enabled";
}

static bool isUfwInstalled() {
    int res = system("command -v ufw >/dev/null 2>&1");
    return (res == 0);
}

static TQString getUfwCurrentState() {
    if (!isUfwInstalled()) return "Not Installed";
    FILE* f = popen("ufw status 2>/dev/null", "r");
    if (!f) return "Enabled";
    char buf[256];
    if (fgets(buf, sizeof(buf), f)) {
        TQString res(buf);
        pclose(f);
        if (res.contains("inactive")) return "Disabled";
        return "Enabled";
    }
    pclose(f);
    return "Enabled";
}

static bool isBluezCupsInstalled() {
    return TQFileInfo("/usr/lib/cups/backend/bluetooth").exists();
}

static TQString getBluezCupsCurrentState() {
    if (!isBluezCupsInstalled()) return "Not Installed";
    TQFileInfo fi("/usr/lib/cups/backend/bluetooth");
    if (fi.exists() && !fi.isExecutable()) {
        return "Disabled";
    }
    return "Enabled";
}

static TQString getLogCurrentState(const TQString& logFilePath) {
    char buf[1024];
    ssize_t len = ::readlink(logFilePath.latin1(), buf, sizeof(buf)-1);
    if (len != -1) {
        buf[len] = '\0';
        TQString target(buf);
        if (target == "/dev/null") {
            return "Disabled";
        }
    }
    if (logFilePath == "/var/log/journal") {
        TQFile f("/etc/systemd/journald.conf");
        if (f.open(IO_ReadOnly)) {
            TQString content;
            TQTextStream sts(&f);
            while (!sts.atEnd()) {
                content += sts.readLine() + "\n";
            }
            f.close();
            if (content.contains("Storage=none") || content.contains("Storage=volatile")) {
                return "Disabled";
            }
        }
    }
    return "Enabled";
}

static TQString getCStatesCurrentState() {
    bool currentCStatesDisabled = false;
    TQFile cmdlineFile("/proc/cmdline");
    if (cmdlineFile.open(IO_ReadOnly)) {
        TQString cmdline = TQTextStream(&cmdlineFile).readLine();
        cmdlineFile.close();
        if (cmdline.contains("intel_idle.max_cstate=0") || cmdline.contains("processor.max_cstate=1")) {
            currentCStatesDisabled = true;
        }
    }
    return currentCStatesDisabled ? "Disabled" : "Enabled";
}

static TQString getIPv6CurrentState() {
    TQFile fDisable("/proc/sys/net/ipv6/conf/all/disable_ipv6");
    TQString disableVal = "0";
    if (fDisable.open(IO_ReadOnly)) {
        disableVal = TQTextStream(&fDisable).readLine().stripWhiteSpace();
        fDisable.close();
    }
    if (disableVal == "1") {
        return "Disable Completely";
    }
    
    TQFile fRA("/proc/sys/net/ipv6/conf/all/accept_ra");
    TQString raVal = "1";
    if (fRA.open(IO_ReadOnly)) {
        raVal = TQTextStream(&fRA).readLine().stripWhiteSpace();
        fRA.close();
    }
    if (raVal == "0") {
        return "Local-Only";
    }
    return "Keep Enabled";
}

TQPixmap MainWindow::loadRawIcon(int id)
{
    int w = 0, h = 0, pixfmt = 0;
    const unsigned char *pixels = zx0em_get_image(id, &w, &h, &pixfmt);
    if (!pixels) return TQPixmap();
    
    TQImage img;
    if (pixfmt == ZX0EM_PIXFMT_RGBA) {
        img = TQImage(w, h, 32);
        img.setAlphaBuffer(true);
        for (int y = 0; y < h; ++y) {
            TQRgb *dest = (TQRgb*)img.scanLine(y);
            const unsigned char *src = pixels + y * w * 4;
            for (int x = 0; x < w; ++x) {
                dest[x] = tqRgba(src[x*4], src[x*4+1], src[x*4+2], src[x*4+3]);
            }
        }
    } else if (pixfmt == ZX0EM_PIXFMT_GRAY_ALPHA) {
        img = TQImage(w, h, 32);
        img.setAlphaBuffer(true);
        for (int y = 0; y < h; ++y) {
            TQRgb *dest = (TQRgb*)img.scanLine(y);
            const unsigned char *src = pixels + y * w * 2;
            for (int x = 0; x < w; ++x) {
                unsigned char g = src[x*2];
                unsigned char a = src[x*2+1];
                dest[x] = tqRgba(g, g, g, a);
            }
        }
    } else if (pixfmt == ZX0EM_PIXFMT_PALETTE) {
        img = TQImage(w, h, 32);
        img.setAlphaBuffer(true);
        const zx0em_entry_t *e = &zx0em_entries[id];
        const unsigned char *pal_data = zx0em_buf_ + e->offset;
        int pal_count = pal_data[0];
        const unsigned char *colors = pal_data + 1;
        const unsigned char *indices = pixels + 1 + pal_count * 4;
        for (int y = 0; y < h; ++y) {
            TQRgb *dest = (TQRgb*)img.scanLine(y);
            const unsigned char *src = indices + y * w;
            for (int x = 0; x < w; ++x) {
                int idx = src[x];
                int r = colors[idx*4];
                int g = colors[idx*4+1];
                int b = colors[idx*4+2];
                int a = colors[idx*4+3];
                dest[x] = tqRgba(r, g, b, a);
            }
        }
    } else if (pixfmt == ZX0EM_PIXFMT_1BIT) {
        img = TQImage(w, h, 32);
        img.setAlphaBuffer(true);
        const zx0em_entry_t *e = &zx0em_entries[id];
        const unsigned char *pal_data = zx0em_buf_ + e->offset;
        const unsigned char *colors = pal_data + 1;
        const unsigned char *bits = pixels + 9;
        unsigned int pitch = (w + 7) / 8;
        for (int y = 0; y < h; ++y) {
            TQRgb *dest = (TQRgb*)img.scanLine(y);
            const unsigned char *src = bits + y * pitch;
            for (int x = 0; x < w; ++x) {
                int bit = (src[x / 8] >> (7 - (x % 8))) & 1;
                int r = colors[bit*4];
                int g = colors[bit*4+1];
                int b = colors[bit*4+2];
                int a = colors[bit*4+3];
                dest[x] = tqRgba(r, g, b, a);
            }
        }
    }
    
    return TQPixmap(img);
}

static TQPixmap modifyIconBackground(int id, bool active, int size = 20) {
    TQPixmap basePix = MainWindow::loadRawIcon(id);
    TQImage img = basePix.convertToImage();
    img = img.smoothScale(size, size);
    
    TQColor bgCol = active ? TQColor("#2A2A35") : TQColor("#16161C");
    TQRgb newBg = tqRgb(bgCol.red(), bgCol.green(), bgCol.blue());
    
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            TQRgb p = img.pixel(x, y);
            int r = tqRed(p);
            int g = tqGreen(p);
            int b = tqBlue(p);
            if (r < 45 && g < 45 && b < 45) {
                img.setPixel(x, y, newBg);
            }
        }
    }
    
    TQPixmap pix;
    pix.convertFromImage(img);
    return pix;
}

MainWindow::MainWindow(TQWidget* parent, const char* name, const TQString& adminPassword)
    : TQMainWindow(parent, name), m_currentPage(0), m_lvUsages(0), m_lblHelpDescription(0), m_adminPassword(adminPassword)
{

    TQPixmap appIcon = loadRawIcon(icons_tuneperfs_png);
    setIcon(appIcon);

    setCaption("TunePerf - Intelligent Linux System Optimizer");
    resize(1280, 720);

    // Apply main window background color
    setPaletteBackgroundColor(TQColor("#121214"));

    // Main layout container
    TQWidget* central = new TQWidget(this);
    setCentralWidget(central);
    TQHBoxLayout* mainLayout = new TQHBoxLayout(central, 0, 0);

    // Create Sidebar and Wizard Stack
    createSidebar();
    
    TQVBoxLayout* contentCol = new TQVBoxLayout();
    m_wizardStack = new TQtSlidingStackedWidget(central);
    m_wizardStack->setSpeed(350); // smooth animation duration (ms)
    
    contentCol->addWidget(m_wizardStack, 1);
    
    // Bottom Buttons Panel
    TQHBoxLayout* btnPanel = new TQHBoxLayout();
    btnPanel->setMargin(16);
    btnPanel->setSpacing(12);
    
    m_btnRestore = new TQPushButton("Restore Backup", central);
    m_btnRestore->setPaletteBackgroundColor(TQColor("#E11D48"));
    m_btnRestore->setPaletteForegroundColor(TQColor("#FFFFFF"));
    m_btnRestore->installEventFilter(this);
    connect(m_btnRestore, SIGNAL(clicked()), this, SLOT(startRestore()));
    btnPanel->addWidget(m_btnRestore);
    
    btnPanel->addStretch();
    
    m_btnBack = new TQPushButton("< Back", central);
    m_btnBack->setPaletteBackgroundColor(TQColor("#252530"));
    m_btnBack->setPaletteForegroundColor(TQColor("#E2E8F0"));
    m_btnBack->installEventFilter(this);
    connect(m_btnBack, SIGNAL(clicked()), this, SLOT(goBack()));
    btnPanel->addWidget(m_btnBack);
    
    m_btnNext = new TQPushButton("Next >", central);
    m_btnNext->installEventFilter(this);
    connect(m_btnNext, SIGNAL(clicked()), this, SLOT(goNext()));
    btnPanel->addWidget(m_btnNext);
    
    m_btnExit = new TQPushButton("Exit", central);
    m_btnExit->setPaletteBackgroundColor(TQColor("#252530"));
    m_btnExit->setPaletteForegroundColor(TQColor("#E2E8F0"));
    m_btnExit->installEventFilter(this);
    connect(m_btnExit, SIGNAL(clicked()), tqApp, SLOT(quit()));
    btnPanel->addWidget(m_btnExit);
    
    contentCol->addLayout(btnPanel);
    
    mainLayout->addLayout(contentCol, 1);

    // Initialize runner and load
    m_runner = new TunePerfRunner(this, m_adminPassword);
    connect(m_runner, SIGNAL(dryRunFinished(bool)), this, SLOT(onDryRunFinished(bool)));
    connect(m_runner, SIGNAL(applyFinished(bool)), this, SLOT(onApplyFinished(bool)));
    connect(m_runner, SIGNAL(restoreFinished(bool)), this, SLOT(onRestoreFinished(bool)));
    connect(m_runner, SIGNAL(logMessage(const TQString&)), this, SLOT(onLogMessage(const TQString&)));

    // Create pages inside stack
    createWizardPages();
    loadProfileSelection();
    updateTexts();
    updateNavigationButtons();

    // Trigger initial dry-run to parse hardware info and load default configs
    m_runner->runDryRun();
}

MainWindow::~MainWindow()
{
    delete m_runner;
}

void MainWindow::resizeEvent(TQResizeEvent* e)
{
    TQMainWindow::resizeEvent(e);
}

void MainWindow::createSidebar()
{
    m_sidebarPanel = new TQWidget(centralWidget());
    m_sidebarPanel->setFixedWidth(280);
    m_sidebarPanel->setPaletteBackgroundColor(TQColor("#16161C"));
    
    TQVBoxLayout* lay = new TQVBoxLayout(m_sidebarPanel);
    lay->setMargin(16);
    lay->setSpacing(16);
    
    // Header
    lay->addSpacing(16); // Push the logo down to prevent visual clipping
    
    TQHBoxLayout* centerLay = new TQHBoxLayout();
    centerLay->addStretch(1);
    
    TQHBoxLayout* headerLay = new TQHBoxLayout();
    headerLay->setSpacing(8);
    headerLay->setMargin(0);
    
    TQLabel* logoIcon = new TQLabel(m_sidebarPanel);
    TQPixmap logoPix = loadRawIcon(icons_tuneperfs_png);
    logoIcon->setPixmap(logoPix);
    headerLay->addWidget(logoIcon, 0, TQt::AlignVCenter);
    
    TQVBoxLayout* titleTextLay = new TQVBoxLayout();
    titleTextLay->setSpacing(0);
    titleTextLay->setMargin(0);
    
    TQLabel* logoText = new TQLabel("<b><font color='#FFFFFF'>TUNE</font><font color='#00D2FF'>PERF</font></b>", m_sidebarPanel);
    logoText->setFont(TQFont("Outfit", 18, TQFont::Bold));
    logoText->setAlignment(TQt::AlignVCenter | TQt::AlignLeft);
    titleTextLay->addWidget(logoText);
    
    TQLabel* versionText = new TQLabel(TQString("v") + TUNEPERF_VERSION, m_sidebarPanel);
    versionText->setFont(TQFont("Inter", 8, TQFont::Normal));
    versionText->setPaletteForegroundColor(TQColor("#71717A"));
    versionText->setAlignment(TQt::AlignVCenter | TQt::AlignLeft);
    titleTextLay->addWidget(versionText);
    
    headerLay->addLayout(titleTextLay);
    
    centerLay->addLayout(headerLay);
    centerLay->addStretch(1);
    lay->addLayout(centerLay);
    
    m_lblSidebarSubtitle = new TQLabel("System Optimization", m_sidebarPanel);
    m_lblSidebarSubtitle->setFont(TQFont("Inter", 9, TQFont::Normal));
    m_lblSidebarSubtitle->setPaletteForegroundColor(TQColor("#71717A"));
    m_lblSidebarSubtitle->setAlignment(TQt::AlignCenter);
    lay->addWidget(m_lblSidebarSubtitle);
    
    lay->addSpacing(24);
    
    // Steps List
    m_sidebarSteps << "Welcome" << "System Role" << "Usage Profile" << "Policies" << "Parameters Editor" << "Optimization";
    
    int step_icons[] = {
        icons_home_png,
        icons_role_png,
        icons_profil_png,
        icons_policies_png,
        icons_parameters_png,
        icons_opti_png
    };
    
    for (int i = 0; i < (int)m_sidebarSteps.count(); ++i) {
        TQFrame* rowFrame = new TQFrame(m_sidebarPanel);
        rowFrame->setFrameStyle(TQFrame::NoFrame);
        rowFrame->setPaletteBackgroundColor(TQColor("#16161C"));
        
        TQHBoxLayout* rowLay = new TQHBoxLayout(rowFrame);
        rowLay->setMargin(6);
        rowLay->setSpacing(8);
        
        TQLabel* iconLbl = new TQLabel(rowFrame);
        TQPixmap iconPix = loadRawIcon(step_icons[i]);
        TQImage img = iconPix.convertToImage();
        iconPix.convertFromImage(img.smoothScale(32, 32));
        iconLbl->setPixmap(iconPix);
        iconLbl->setFixedWidth(32);
        iconLbl->setAlignment(TQt::AlignCenter);
        rowLay->addWidget(iconLbl);
        
        TQLabel* lbl = new TQLabel(m_sidebarSteps[i], rowFrame);
        lbl->setFont(TQFont("Inter", 12, TQFont::Normal));
        lbl->setPaletteForegroundColor(TQColor("#71717A"));
        lbl->setAlignment(TQt::AlignVCenter | TQt::AlignLeft);
        rowLay->addWidget(lbl, 1);
        
        lay->addWidget(rowFrame);
        m_sidebarLabels.append(lbl);
        m_sidebarIconLabels.append(iconLbl);
        m_sidebarFrames.append(rowFrame);
    }
    
    lay->addStretch();
    
    m_pixLangueNormal = modifyIconBackground(icons_langue_png, false);
    m_pixLangueHover = modifyIconBackground(icons_langue_png, true);
    
    m_pixGithubNormal = modifyIconBackground(icons_github_png, false);
    m_pixGithubHover = modifyIconBackground(icons_github_png, true);
    
    m_pixAboutNormal = modifyIconBackground(icons_about_png, false);
    m_pixAboutHover = modifyIconBackground(icons_about_png, true);

    // 3 Buttons Column
    TQVBoxLayout* btnCol = new TQVBoxLayout();
    btnCol->setSpacing(6);
    btnCol->setAlignment(TQt::AlignCenter);

    m_btnLangue = new TQPushButton(m_sidebarPanel);
    m_btnLangue->setFlat(true);
    m_btnLangue->setFixedSize(30, 30);
    m_btnLangue->setPaletteBackgroundColor(TQColor("#16161C"));
    m_btnLangue->setIconSet(TQIconSet(m_pixLangueNormal));
    m_btnLangue->installEventFilter(this);
    connect(m_btnLangue, SIGNAL(clicked()), this, SLOT(slotLanguageClicked()));
    btnCol->addWidget(m_btnLangue);

    m_btnGithub = new TQPushButton(m_sidebarPanel);
    m_btnGithub->setFlat(true);
    m_btnGithub->setFixedSize(30, 30);
    m_btnGithub->setPaletteBackgroundColor(TQColor("#16161C"));
    m_btnGithub->setIconSet(TQIconSet(m_pixGithubNormal));
    m_btnGithub->installEventFilter(this);
    connect(m_btnGithub, SIGNAL(clicked()), this, SLOT(slotGithubClicked()));
    btnCol->addWidget(m_btnGithub);

    m_btnAbout = new TQPushButton(m_sidebarPanel);
    m_btnAbout->setFlat(true);
    m_btnAbout->setFixedSize(30, 30);
    m_btnAbout->setPaletteBackgroundColor(TQColor("#16161C"));
    m_btnAbout->setIconSet(TQIconSet(m_pixAboutNormal));
    m_btnAbout->installEventFilter(this);
    connect(m_btnAbout, SIGNAL(clicked()), this, SLOT(slotAboutClicked()));
    btnCol->addWidget(m_btnAbout);

    // Footer / Root indicator container box
    TQFrame* rootContainer = new TQFrame(m_sidebarPanel);
    rootContainer->setFrameStyle(TQFrame::StyledPanel | TQFrame::Sunken);
    rootContainer->setPaletteBackgroundColor(TQColor("#064E3B"));
    
    TQVBoxLayout* rootLay = new TQVBoxLayout(rootContainer, 10, 6);
    
    TQLabel* rootIcon = new TQLabel(rootContainer);
    TQPixmap secuPix = loadRawIcon(icons_secu_ok_png);
    rootIcon->setPixmap(secuPix);
    rootIcon->setAlignment(TQt::AlignCenter);
    rootLay->addWidget(rootIcon);
    
    m_lblRootText = new TQLabel("Root Privileges", rootContainer);
    m_lblRootText->setFont(TQFont("Inter", 9, TQFont::Bold));
    m_lblRootText->setPaletteForegroundColor(TQColor("#10B981"));
    m_lblRootText->setAlignment(TQt::AlignCenter);
    rootLay->addWidget(m_lblRootText);
    
    TQHBoxLayout* footerRow = new TQHBoxLayout();
    footerRow->setSpacing(8);
    footerRow->addLayout(btnCol);
    footerRow->addWidget(rootContainer, 1);
    
    lay->addLayout(footerRow);

    TQHBoxLayout* centralLayout = static_cast<TQHBoxLayout*>(centralWidget()->layout());
    centralLayout->addWidget(m_sidebarPanel, 0);
}

void MainWindow::createWizardPages()
{
    m_wizardStack->addWidget(createWelcomePage()); // Page 0
    m_wizardStack->addWidget(createRolePage());    // Page 1
    m_wizardStack->addWidget(createUsagePage());   // Page 2
    m_wizardStack->addWidget(createPolicyPage());  // Page 3
    m_wizardStack->addWidget(createReviewPage());  // Page 4
    m_wizardStack->addWidget(createApplyPage());   // Page 5
    
    // Trigger initial population of usage list now that all pages are created
    onRoleChanged(m_lvRoles->firstChild());
}

void MainWindow::updateNavigationButtons()
{
    int step_icons[] = {
        icons_home_png,
        icons_role_png,
        icons_profil_png,
        icons_policies_png,
        icons_parameters_png,
        icons_opti_png
    };

    // Update sidebar labels, icons and frames to highlight active step
    for (int i = 0; i < (int)m_sidebarLabels.count(); ++i) {
        bool active = (i == m_currentPage);
        TQColor bgCol = active ? TQColor("#2A2A35") : TQColor("#16161C");
        TQColor fgCol = active ? TQColor("#00D2FF") : TQColor("#71717A");
        
        m_sidebarFrames[i]->setPaletteBackgroundColor(bgCol);
        m_sidebarLabels[i]->setPaletteForegroundColor(fgCol);
        m_sidebarLabels[i]->setPaletteBackgroundColor(bgCol);
        m_sidebarIconLabels[i]->setPaletteBackgroundColor(bgCol);
        
        TQPixmap iconPix = modifyIconBackground(step_icons[i], active, 32);
        m_sidebarIconLabels[i]->setPixmap(iconPix);
        
        TQFont font = m_sidebarLabels[i]->font();
        font.setBold(active);
        m_sidebarLabels[i]->setFont(font);
    }

    m_btnBack->setEnabled(m_currentPage > 0 && m_currentPage < 5);
    
    if (m_currentPage == 4) {
        m_btnNext->setText(TRANS("ui.btn_apply"));
        m_btnNext->setPaletteBackgroundColor(TQColor("#10B981"));
        m_btnNext->setPaletteForegroundColor(TQColor("#FFFFFF"));
    } else if (m_currentPage == 5) {
        m_btnNext->setText(TRANS("ui.btn_finish"));
        m_btnNext->setEnabled(m_lblApplyStatus->text().contains("successfully") || m_lblApplyStatus->text().contains("failed") || m_lblApplyStatus->text().contains("réussir") || m_lblApplyStatus->text().contains("échoué") || m_lblApplyStatus->text().contains("succès") || m_lblApplyStatus->text().contains("échec"));
        m_btnNext->setPaletteBackgroundColor(TQColor("#3B82F6"));
        m_btnNext->setPaletteForegroundColor(TQColor("#FFFFFF"));
    } else {
        m_btnNext->setText(TRANS("ui.btn_next") + " >");
        m_btnNext->setEnabled(true);
        m_btnNext->setPaletteBackgroundColor(TQColor("#252530"));
        m_btnNext->setPaletteForegroundColor(TQColor("#E2E8F0"));
    }
    
    m_btnRestore->setShown(m_currentPage == 0);
    m_btnRestore->setEnabled(m_runner->hasBackup());
}

TQWidget* MainWindow::createWelcomePage()
{
    TQWidget* page = new TQWidget(m_wizardStack);
    TQVBoxLayout* lay = new TQVBoxLayout(page, 24, 16);
    
    m_lblWelcomeTitle = new TQLabel("Welcome to TunePerf", page);
    m_lblWelcomeTitle->setFont(TQFont("Outfit", 22, TQFont::Bold));
    m_lblWelcomeTitle->setPaletteForegroundColor(TQColor("#FFFFFF"));
    lay->addWidget(m_lblWelcomeTitle);
    
    m_lblWelcomeDesc = new TQLabel(
        "TunePerf is a freestanding performance optimizer for Linux systems. "
        "It scans your system's complete hardware topology (CPU cores, RAM size, NVMe/SSD channels, GPU, NUMA nodes, ZRAM) "
        "and builds tailormade configurations to minimize latency and maximize throughput.<br><br>"
        "Click <b>Next</b> to begin selecting your system profile role.", page);
    m_lblWelcomeDesc->setFont(TQFont("Inter", 11));
    m_lblWelcomeDesc->setPaletteForegroundColor(TQColor("#A0A0AA"));
    lay->addWidget(m_lblWelcomeDesc);
    
    // Hardware info card
    m_gbHardwareBox = new TQGroupBox("Detected Hardware Topology", page);
    TQVBoxLayout* hwLay = new TQVBoxLayout(m_gbHardwareBox, 12, 8);
    
    m_lblHardwareInfo = new TQLabel("Scanning system hardware, please wait...", m_gbHardwareBox);
    m_lblHardwareInfo->setFont(TQFont("Inter", 10));
    m_lblHardwareInfo->setPaletteForegroundColor(TQColor("#00D2FF"));
    hwLay->addWidget(m_lblHardwareInfo);
    
    lay->addWidget(m_gbHardwareBox);
    lay->addStretch();
    
    return page;
}

TQWidget* MainWindow::createRolePage()
{
    TQWidget* page = new TQWidget(m_wizardStack);
    TQVBoxLayout* lay = new TQVBoxLayout(page, 24, 16);
    
    m_lblRoleTitle = new TQLabel("Select System Role", page);
    m_lblRoleTitle->setFont(TQFont("Outfit", 18, TQFont::Bold));
    m_lblRoleTitle->setPaletteForegroundColor(TQColor("#FFFFFF"));
    lay->addWidget(m_lblRoleTitle);
    
    m_lblRoleDesc = new TQLabel("Optimize parameters based on the primary function of this machine:", page);
    m_lblRoleDesc->setPaletteForegroundColor(TQColor("#A0A0AA"));
    lay->addWidget(m_lblRoleDesc);
    
    m_lvRoles = new TQListView(page);
    m_lvRoles->addColumn("Role Profile");
    m_lvRoles->addColumn("Description");
    m_lvRoles->setRootIsDecorated(false);
    m_lvRoles->setAllColumnsShowFocus(true);
    m_lvRoles->setPaletteBackgroundColor(TQColor("#1E1E26"));
    m_lvRoles->setPaletteForegroundColor(TQColor("#E2E8F0"));
    
    connect(m_lvRoles, SIGNAL(selectionChanged(TQListViewItem*)), this, SLOT(onRoleChanged(TQListViewItem*)));
    
    // Select first item by default and trigger initial usage population (deferred to wizard page setup end)
    m_lvRoles->setSelected(m_lvRoles->firstChild(), true);
    
    lay->addWidget(m_lvRoles, 1);
    
    return page;
}

TQWidget* MainWindow::createUsagePage()
{
    TQWidget* page = new TQWidget(m_wizardStack);
    TQVBoxLayout* lay = new TQVBoxLayout(page, 24, 16);
    
    m_lblUsageTitle = new TQLabel("Select Usage Profile", page);
    m_lblUsageTitle->setFont(TQFont("Outfit", 18, TQFont::Bold));
    m_lblUsageTitle->setPaletteForegroundColor(TQColor("#FFFFFF"));
    lay->addWidget(m_lblUsageTitle);
    
    m_lblUsageDesc = new TQLabel("Refine optimizations based on the specific work character:", page);
    m_lblUsageDesc->setPaletteForegroundColor(TQColor("#A0A0AA"));
    lay->addWidget(m_lblUsageDesc);
    
    m_lvUsages = new TQListView(page);
    m_lvUsages->addColumn("Usage Profile");
    m_lvUsages->addColumn("Optimization Target");
    m_lvUsages->setRootIsDecorated(false);
    m_lvUsages->setAllColumnsShowFocus(true);
    m_lvUsages->setPaletteBackgroundColor(TQColor("#1E1E26"));
    m_lvUsages->setPaletteForegroundColor(TQColor("#E2E8F0"));
    
    lay->addWidget(m_lvUsages, 1);
    
    return page;
}

void MainWindow::onRoleChanged(TQListViewItem*)
{
    if (!m_lvUsages) return;
    m_lvUsages->clear();
    
    TQListViewItem* sel = m_lvRoles->selectedItem();
    if (!sel) return;
    
    TQString roleText = sel->text(0);
    int roleNum = roleText.left(1).toInt();
    
    switch (roleNum) {
        case 1: // Desktop
            new TQListViewItem(m_lvUsages, TRANS("usage.desktop.1.name"), TRANS("usage.desktop.1.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.desktop.2.name"), TRANS("usage.desktop.2.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.desktop.3.name"), TRANS("usage.desktop.3.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.desktop.4.name"), TRANS("usage.desktop.4.desc"));
            break;
        case 2: // Workstation
            new TQListViewItem(m_lvUsages, TRANS("usage.workstation.1.name"), TRANS("usage.workstation.1.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.workstation.2.name"), TRANS("usage.workstation.2.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.workstation.3.name"), TRANS("usage.workstation.3.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.workstation.4.name"), TRANS("usage.workstation.4.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.workstation.5.name"), TRANS("usage.workstation.5.desc"));
            break;
        case 3: // Gaming
            new TQListViewItem(m_lvUsages, TRANS("usage.gaming.1.name"), TRANS("usage.gaming.1.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.gaming.2.name"), TRANS("usage.gaming.2.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.gaming.3.name"), TRANS("usage.gaming.3.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.gaming.4.name"), TRANS("usage.gaming.4.desc"));
            break;
        case 4: // Server Light
            new TQListViewItem(m_lvUsages, TRANS("usage.server_light.1.name"), TRANS("usage.server_light.1.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.server_light.2.name"), TRANS("usage.server_light.2.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.server_light.3.name"), TRANS("usage.server_light.3.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.server_light.4.name"), TRANS("usage.server_light.4.desc"));
            break;
        case 5: // Server DB
            new TQListViewItem(m_lvUsages, TRANS("usage.server_heavy.1.name"), TRANS("usage.server_heavy.1.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.server_heavy.2.name"), TRANS("usage.server_heavy.2.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.server_heavy.3.name"), TRANS("usage.server_heavy.3.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.server_heavy.4.name"), TRANS("usage.server_heavy.4.desc"));
            break;
        case 6: // VM Guest
            new TQListViewItem(m_lvUsages, TRANS("usage.vm_guest.1.name"), TRANS("usage.vm_guest.1.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.vm_guest.2.name"), TRANS("usage.vm_guest.2.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.vm_guest.3.name"), TRANS("usage.vm_guest.3.desc"));
            new TQListViewItem(m_lvUsages, TRANS("usage.vm_guest.4.name"), TRANS("usage.vm_guest.4.desc"));
            break;
    }
    
    // Select first item by default
    if (m_lvUsages->firstChild()) {
        m_lvUsages->setSelected(m_lvUsages->firstChild(), true);
    }
}

TQWidget* MainWindow::createPolicyPage()
{
    TQWidget* page = new TQWidget(m_wizardStack);
    TQVBoxLayout* lay = new TQVBoxLayout(page, 24, 16);
    
    m_lblPolicyTitle = new TQLabel("Policies & Advanced Toggles", page);
    m_lblPolicyTitle->setFont(TQFont("Outfit", 18, TQFont::Bold));
    m_lblPolicyTitle->setPaletteForegroundColor(TQColor("#FFFFFF"));
    lay->addWidget(m_lblPolicyTitle);
    
    m_tabAdvanced = new CustomTabWidget(page);
    connect(m_tabAdvanced, SIGNAL(currentChanged(TQWidget*)), this, SLOT(onTabChanged(TQWidget*)));
    
    // Tab 1: System Policies
    TQVBoxLayout* tab1Lay = NULL;
    m_tab1Container = createScrollContainer(m_tabAdvanced, "System Policies", tab1Lay);
    tab1Lay->setSpacing(12);
    
    // IPv6 Policy
    TQHBoxLayout* row1 = new TQHBoxLayout();
    m_lblIPv6 = new TQLabel(TQString("IPv6 Protocol Configuration: <font color='#F59E0B'>(current: %1)</font>").arg(getIPv6CurrentState()), m_tab1Container);
    m_lblIPv6->setFixedWidth(380);
    m_lblIPv6->setPaletteForegroundColor(TQColor("#E2E8F0"));
    
    m_cbIPv6 = new TQComboBox(m_tab1Container);
    m_cbIPv6->insertItem("Keep Enabled (Default)");
    m_cbIPv6->insertItem("Disable Completely");
    m_cbIPv6->insertItem("Local-Only (IPv6 Autoconf disabled)");
    m_cbIPv6->setFixedWidth(300);
    m_cbIPv6->setPaletteBackgroundColor(TQColor("#1E1E26"));
    m_cbIPv6->setPaletteForegroundColor(TQColor("#E2E8F0"));
    m_cbIPv6->installEventFilter(this);
    
    row1->addWidget(m_lblIPv6);
    row1->addWidget(m_cbIPv6);
    row1->addStretch();
    tab1Lay->addLayout(row1);
    
    // Sub-container for policy parameters that will trigger the Leave event filter when exited
    TQWidget* policyParamsContainer = new TQWidget(m_tab1Container, "scroll_container");
    policyParamsContainer->installEventFilter(this);
    TQVBoxLayout* policyParamsLay = new TQVBoxLayout(policyParamsContainer, 0, 12);
    
    // Experimental Tweaks
    TQHBoxLayout* rowExp = new TQHBoxLayout();
    m_chkExperimental = new TQtToggleSwitch(TQtToggleSwitch::BorderSolidSoftRectNoText, false, policyParamsContainer);
    m_chkExperimental->setSuitableHeight(24);
    m_lblExperimental = new TQLabel("<b><font color='#A0A0AA'>Enable Experimental Tweaks</font></b>", policyParamsContainer);
    connect(m_chkExperimental, SIGNAL(stateChanged(bool)), this, SLOT(onExperimentalStateChanged(bool)));
    rowExp->addWidget(m_chkExperimental);
    rowExp->addSpacing(8);
    rowExp->addWidget(m_lblExperimental, 1);
    policyParamsLay->addLayout(rowExp);

    m_chkExperimental->installEventFilter(this);
    m_lblExperimental->installEventFilter(this);
    
    
    // C-States
    TQHBoxLayout* rowCStates = new TQHBoxLayout();
    m_chkDisableCStates = new TQtToggleSwitch(TQtToggleSwitch::BorderSolidSoftRectNoText, false, policyParamsContainer);
    m_chkDisableCStates->setSuitableHeight(24);
    m_lblDisableCStates = new TQLabel(TQString("<b><font color='#A0A0AA'>Disable CPU C-States (Sleep states)</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getCStatesCurrentState()), policyParamsContainer);
    connect(m_chkDisableCStates, SIGNAL(stateChanged(bool)), this, SLOT(onCStatesStateChanged(bool)));
    rowCStates->addWidget(m_chkDisableCStates);
    rowCStates->addSpacing(8);
    rowCStates->addWidget(m_lblDisableCStates, 1);
    policyParamsLay->addLayout(rowCStates);

    m_chkDisableCStates->installEventFilter(this);
    m_lblDisableCStates->installEventFilter(this);
    
    
    tab1Lay->addWidget(policyParamsContainer);
    tab1Lay->addStretch(1);
    
    // Tab 2: Log Management
    TQVBoxLayout* tab2Lay = NULL;
    m_tab2Container = createScrollContainer(m_tabAdvanced, "Log Management", tab2Lay);
    tab2Lay->setSpacing(12);
    
    // Sub-container for log parameters that will trigger the Leave event filter when exited
    TQWidget* logParamsContainer = new TQWidget(m_tab2Container, "scroll_container");
    logParamsContainer->installEventFilter(this);
    TQVBoxLayout* logParamsLay = new TQVBoxLayout(logParamsContainer, 0, 12);
    
    // 1. Journald
    TQHBoxLayout* rowJournald = new TQHBoxLayout();
    m_chkDisableJournald = new TQtToggleSwitch(TQtToggleSwitch::BorderSolidSoftRectNoText, false, logParamsContainer);
    m_chkDisableJournald->setSuitableHeight(24);
    m_lblDisableJournald = new TQLabel(TQString("<b><font color='#A0A0AA'>Disable Systemd Journald Logs</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getLogCurrentState("/var/log/journal")), logParamsContainer);
    connect(m_chkDisableJournald, SIGNAL(stateChanged(bool)), this, SLOT(onLogToggleChanged(bool)));
    rowJournald->addWidget(m_chkDisableJournald);
    rowJournald->addSpacing(8);
    rowJournald->addWidget(m_lblDisableJournald, 1);
    logParamsLay->addLayout(rowJournald);
    m_chkDisableJournald->installEventFilter(this);
    m_lblDisableJournald->installEventFilter(this);

    // 2. Rsyslog
    TQHBoxLayout* rowRsyslog = new TQHBoxLayout();
    m_chkDisableRsyslog = new TQtToggleSwitch(TQtToggleSwitch::BorderSolidSoftRectNoText, false, logParamsContainer);
    m_chkDisableRsyslog->setSuitableHeight(24);
    m_lblDisableRsyslog = new TQLabel(TQString("<b><font color='#A0A0AA'>Disable Syslog & Kernel Logs (rsyslog)</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getLogCurrentState("/var/log/syslog")), logParamsContainer);
    connect(m_chkDisableRsyslog, SIGNAL(stateChanged(bool)), this, SLOT(onLogToggleChanged(bool)));
    rowRsyslog->addWidget(m_chkDisableRsyslog);
    rowRsyslog->addSpacing(8);
    rowRsyslog->addWidget(m_lblDisableRsyslog, 1);
    logParamsLay->addLayout(rowRsyslog);
    m_chkDisableRsyslog->installEventFilter(this);
    m_lblDisableRsyslog->installEventFilter(this);

    // 3. Xorg
    TQHBoxLayout* rowXorg = new TQHBoxLayout();
    m_chkDisableXorg = new TQtToggleSwitch(TQtToggleSwitch::BorderSolidSoftRectNoText, false, logParamsContainer);
    m_chkDisableXorg->setSuitableHeight(24);
    m_lblDisableXorg = new TQLabel(TQString("<b><font color='#A0A0AA'>Disable Xorg & TDM Display Logs</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getLogCurrentState("/var/log/Xorg.0.log")), logParamsContainer);
    connect(m_chkDisableXorg, SIGNAL(stateChanged(bool)), this, SLOT(onLogToggleChanged(bool)));
    rowXorg->addWidget(m_chkDisableXorg);
    rowXorg->addSpacing(8);
    rowXorg->addWidget(m_lblDisableXorg, 1);
    logParamsLay->addLayout(rowXorg);
    m_chkDisableXorg->installEventFilter(this);
    m_lblDisableXorg->installEventFilter(this);

    // 4. Boot
    TQHBoxLayout* rowBoot = new TQHBoxLayout();
    m_chkDisableBoot = new TQtToggleSwitch(TQtToggleSwitch::BorderSolidSoftRectNoText, false, logParamsContainer);
    m_chkDisableBoot->setSuitableHeight(24);
    m_lblDisableBoot = new TQLabel(TQString("<b><font color='#A0A0AA'>Disable Boot & Service Logs</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getLogCurrentState("/var/log/boot.log")), logParamsContainer);
    connect(m_chkDisableBoot, SIGNAL(stateChanged(bool)), this, SLOT(onLogToggleChanged(bool)));
    rowBoot->addWidget(m_chkDisableBoot);
    rowBoot->addSpacing(8);
    rowBoot->addWidget(m_lblDisableBoot, 1);
    logParamsLay->addLayout(rowBoot);
    m_chkDisableBoot->installEventFilter(this);
    m_lblDisableBoot->installEventFilter(this);

    // 5. PAM
    TQHBoxLayout* rowPam = new TQHBoxLayout();
    m_chkDisablePam = new TQtToggleSwitch(TQtToggleSwitch::BorderSolidSoftRectNoText, false, logParamsContainer);
    m_chkDisablePam->setSuitableHeight(24);
    m_lblDisablePam = new TQLabel(TQString("<b><font color='#A0A0AA'>Disable Login & Mail Notification Logs</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getLogCurrentState("/var/log/btmp")), logParamsContainer);
    connect(m_chkDisablePam, SIGNAL(stateChanged(bool)), this, SLOT(onLogToggleChanged(bool)));
    rowPam->addWidget(m_chkDisablePam);
    rowPam->addSpacing(8);
    rowPam->addWidget(m_lblDisablePam, 1);
    logParamsLay->addLayout(rowPam);
    m_chkDisablePam->installEventFilter(this);
    m_lblDisablePam->installEventFilter(this);
    
    tab2Lay->addWidget(logParamsContainer);
    tab2Lay->addStretch(1);
    
    // Tab 3: Misc. Optimizations
    TQVBoxLayout* tab3Lay = NULL;
    m_tab3Container = createScrollContainer(m_tabAdvanced, "Misc. Optimizations", tab3Lay);
    tab3Lay->setSpacing(12);
    
    TQWidget* miscParamsContainer = new TQWidget(m_tab3Container, "scroll_container");
    miscParamsContainer->installEventFilter(this);
    TQVBoxLayout* miscParamsLay = new TQVBoxLayout(miscParamsContainer, 0, 12);
    
    // Helper lambda-style setup macros/loops to avoid massive boilerplate
    struct MiscItemSetup {
        TQtToggleSwitch** chk;
        TQLabel** lbl;
        TQString key;
        TQString text;
        TQString curVal;
        bool enabled;
    };
    
    MiscItemSetup items[] = {
        {&m_chkDisableCoredumps, &m_lblDisableCoredumps, "disable_coredumps", "Disable Core Dumps", getCoredumpCurrentState(), true},
        {&m_chkDisableModemManager, &m_lblDisableModemManager, "disable_modemmanager", "Disable ModemManager service", getServiceCurrentState("ModemManager"), true},
        {&m_chkDisableNmWait, &m_lblDisableNmWait, "disable_nm_wait", "Disable NetworkManager-wait-online service", getServiceCurrentState("NetworkManager-wait-online"), true},
        {&m_chkDisableSmbd, &m_lblDisableSmbd, "disable_smbd", "Disable Samba smbd service", getServiceCurrentState("smbd"), true},
        {&m_chkDisableNmbd, &m_lblDisableNmbd, "disable_nmbd", "Disable Samba nmbd service", getServiceCurrentState("nmbd"), true},
        {&m_chkDisableSerialGetty, &m_lblDisableSerialGetty, "disable_serial_getty", "Disable serial-getty service", getServiceCurrentState("serial-getty@ttyS0"), true},
        {&m_chkDisableColord, &m_lblDisableColord, "disable_colord", "Disable colord service", getServiceCurrentState("colord"), true},
        {&m_chkDisableSmartd, &m_lblDisableSmartd, "disable_smartd", "Disable smartd service", getServiceCurrentState("smartd"), true},
        {&m_chkDisableUsbLegacy, &m_lblDisableUsbLegacy, "disable_usb_legacy", "Disable legacy usbmouse/usbkbd modules (in favor of hid)", getUsbLegacyCurrentState(), true},
        {&m_chkDisablePcspkr, &m_lblDisablePcspkr, "disable_pcspkr", "Disable pc speaker module", getPcspkrCurrentState(), true},
        {&m_chkDisableBluetooth, &m_lblDisableBluetooth, "disable_bluetooth", "Disable Bluetooth service", getServiceCurrentState("bluetooth"), true},
        {&m_chkDisablePrint, &m_lblDisablePrint, "disable_print", "Disable print services (CUPS/Avahi)", getServiceCurrentState("cups"), true},
        {&m_chkDisableApparmor, &m_lblDisableApparmor, "disable_apparmor", "Disable AppArmor", getServiceCurrentState("apparmor"), true},
        {&m_chkDisableUfw, &m_lblDisableUfw, "disable_ufw", "Disable UFW Firewall", getUfwCurrentState(), isUfwInstalled()},
        {&m_chkDisableBluezCups, &m_lblDisableBluezCups, "disable_bluez_cups", "Disable Bluez-cups printing backend", getBluezCupsCurrentState(), isBluezCupsInstalled()}
    };
    
    for (int i = 0; i < 15; ++i) {
        TQHBoxLayout* row = new TQHBoxLayout();
        *(items[i].chk) = new TQtToggleSwitch(TQtToggleSwitch::BorderSolidSoftRectNoText, false, miscParamsContainer);
        (*(items[i].chk))->setSuitableHeight(24);
        
        TQString labelText;
        if (items[i].enabled) {
            labelText = TQString("<b><font color='#A0A0AA'>%1</font></b> <font color='#F59E0B'>(current: %2)</font>").arg(items[i].text).arg(items[i].curVal);
        } else {
            labelText = TQString("<b><font color='#71717A'>%1</font></b> <font color='#71717A'>(current: %2)</font>").arg(items[i].text).arg(items[i].curVal);
            (*(items[i].chk))->setEnabled(false);
        }
        
        *(items[i].lbl) = new TQLabel(labelText, miscParamsContainer);
        connect(*(items[i].chk), SIGNAL(stateChanged(bool)), this, SLOT(onLogToggleChanged(bool)));
        
        row->addWidget(*(items[i].chk));
        row->addSpacing(8);
        row->addWidget(*(items[i].lbl), 1);
        miscParamsLay->addLayout(row);
        
        (*(items[i].chk))->installEventFilter(this);
        (*(items[i].lbl))->installEventFilter(this);
    }
    
    tab3Lay->addWidget(miscParamsContainer);
    tab3Lay->addStretch(1);
    
    lay->addWidget(m_tabAdvanced, 1);
    
    // Help panel at the bottom
    TQFrame* helpPanel = new TQFrame(page);
    helpPanel->setFrameStyle(TQFrame::StyledPanel | TQFrame::Sunken);
    helpPanel->setPaletteBackgroundColor(TQColor("#1A1A22"));
    helpPanel->setFixedHeight(75);
    
    TQHBoxLayout* helpLay = new TQHBoxLayout(helpPanel, 10, 10);
    
    TQLabel* helpIconLbl = new TQLabel(helpPanel);
    TQPixmap helpPix = loadRawIcon(icons_help_png);
    helpIconLbl->setPixmap(helpPix);
    helpIconLbl->setFixedWidth(helpPix.width());
    helpIconLbl->setAlignment(TQt::AlignCenter);
    helpLay->addWidget(helpIconLbl);
    
    m_lblPolicyHelp = new TQLabel("Hover over any policy or log switch to read its detailed optimization purpose.", helpPanel);
    m_lblPolicyHelp->setPaletteForegroundColor(TQColor("#A0A0AA"));
    m_lblPolicyHelp->setFont(TQFont("Inter", 10, TQFont::Normal));
    m_lblPolicyHelp->setAlignment(TQt::AlignVCenter | TQt::AlignLeft | TQt::WordBreak);
    helpLay->addWidget(m_lblPolicyHelp, 1);
    
    lay->addWidget(helpPanel);
    
    return page;
}

TQWidget* MainWindow::createReviewPage()
{
    TQWidget* page = new TQWidget(m_wizardStack);
    TQVBoxLayout* lay = new TQVBoxLayout(page, 24, 16);
    
    m_lblReviewTitle = new TQLabel("Calculated Optimizations Review", page);
    m_lblReviewTitle->setFont(TQFont("Outfit", 18, TQFont::Bold));
    m_lblReviewTitle->setPaletteForegroundColor(TQColor("#FFFFFF"));
    lay->addWidget(m_lblReviewTitle);
    
    m_lblReviewDesc = new TQLabel("Review and check/uncheck parameters before writing to the system configuration:", page);
    m_lblReviewDesc->setPaletteForegroundColor(TQColor("#A0A0AA"));
    lay->addWidget(m_lblReviewDesc);
    
    m_tabReview = new CustomTabWidget(page);
    lay->addWidget(m_tabReview, 1);
    connect(m_tabReview, SIGNAL(currentChanged(TQWidget*)), this, SLOT(onTabChanged(TQWidget*)));

    TQFrame* helpPanel = new TQFrame(page);
    helpPanel->setFrameStyle(TQFrame::StyledPanel | TQFrame::Sunken);
    helpPanel->setPaletteBackgroundColor(TQColor("#1A1A22"));
    helpPanel->setFixedHeight(75);
    
    TQHBoxLayout* helpLay = new TQHBoxLayout(helpPanel, 10, 10);
    
    TQLabel* helpIconLbl = new TQLabel(helpPanel);
    TQPixmap helpPix = loadRawIcon(icons_help_png);
    helpIconLbl->setPixmap(helpPix);
    helpIconLbl->setFixedWidth(helpPix.width());
    helpIconLbl->setAlignment(TQt::AlignCenter);
    helpLay->addWidget(helpIconLbl);
    
    m_lblHelpDescription = new TQLabel("Hover over any parameter or switch to read its detailed optimization purpose.", helpPanel);
    m_lblHelpDescription->setPaletteForegroundColor(TQColor("#A0A0AA"));
    m_lblHelpDescription->setFont(TQFont("Inter", 10, TQFont::Normal));
    m_lblHelpDescription->setAlignment(TQt::AlignVCenter | TQt::AlignLeft | TQt::WordBreak);
    helpLay->addWidget(m_lblHelpDescription, 1);
    
    lay->addWidget(helpPanel);
    
    return page;
}

TQWidget* MainWindow::createApplyPage()
{
    TQWidget* page = new TQWidget(m_wizardStack);
    TQVBoxLayout* lay = new TQVBoxLayout(page, 24, 16);
    
    m_lblApplyTitle = new TQLabel("Applying Performance Optimizations", page);
    m_lblApplyTitle->setFont(TQFont("Outfit", 18, TQFont::Bold));
    m_lblApplyTitle->setPaletteForegroundColor(TQColor("#FFFFFF"));
    lay->addWidget(m_lblApplyTitle);
    
    TQHBoxLayout* statusLay = new TQHBoxLayout();
    statusLay->setMargin(0);
    statusLay->setSpacing(8);
    statusLay->setAlignment(TQt::AlignLeft | TQt::AlignVCenter);
    
    m_progressCircle = new TQtProgressCircle2(page);
    m_progressCircle->setFixedSize(30, 30);
    m_progressCircle->setSizePolicy(TQSizePolicy::Fixed, TQSizePolicy::Fixed);
    m_progressCircle->setBarWidth(4);
    m_progressCircle->setRimWidth(4);
    m_progressCircle->setBarColor(TQColor("#00D2FF"));
    m_progressCircle->setRimColor(TQColor("#1E293B"));
    m_progressCircle->setSpinSpeed(4.0f);
    m_progressCircle->setSpinBarLength(60.0f);
    m_progressCircle->hide();
    statusLay->addWidget(m_progressCircle);
    
    m_lblApplyStatus = new TQLabel("Starting execution...", page);
    m_lblApplyStatus->setFont(TQFont("Inter", 12, TQFont::Bold));
    m_lblApplyStatus->setPaletteForegroundColor(TQColor("#00D2FF"));
    statusLay->addWidget(m_lblApplyStatus);
    
    statusLay->addStretch(1);
    
    lay->addLayout(statusLay);
    
    m_txtLog = new TQTextEdit(page);
    m_txtLog->setTextFormat(TQt::RichText);
    m_txtLog->setReadOnly(true);
    m_txtLog->setPaletteBackgroundColor(TQColor("#0F0F12"));
    m_txtLog->setPaletteForegroundColor(TQColor("#39D353"));
    lay->addWidget(m_txtLog, 1);
    
    return page;
}

void MainWindow::goNext()
{
    if (m_currentPage == 0) {
        m_currentPage = 1;
        m_wizardStack->slideInIdx(1, TQtSlidingStackedWidget::Left2Right);
        updateNavigationButtons();
    } 
    else if (m_currentPage == 1) {
        m_currentPage = 2;
        m_wizardStack->slideInIdx(2, TQtSlidingStackedWidget::Left2Right);
        updateNavigationButtons();
    } 
    else if (m_currentPage == 2) {
        m_currentPage = 3;
        m_wizardStack->slideInIdx(3, TQtSlidingStackedWidget::Left2Right);
        updateNavigationButtons();
    } 
    else if (m_currentPage == 3) {
        // Build profile file and run dry-run to get custom configurations
        TQListViewItem* roleSel = m_lvRoles->selectedItem();
        TQListViewItem* usageSel = m_lvUsages->selectedItem();
        
        int roleVal = roleSel ? roleSel->text(0).left(1).toInt() : 1;
        int usageVal = usageSel ? usageSel->text(0).left(1).toInt() : 1;
        int ipv6Val = m_cbIPv6->currentItem() + 1;
        bool expVal = m_chkExperimental->isChecked();
        bool cstatesVal = m_chkDisableCStates->isChecked();
        
        m_runner->setLogValue("disable_journald", m_chkDisableJournald->isChecked() ? "1" : "0");
        m_runner->setLogValue("disable_rsyslog", m_chkDisableRsyslog->isChecked() ? "1" : "0");
        m_runner->setLogValue("disable_xorg", m_chkDisableXorg->isChecked() ? "1" : "0");
        m_runner->setLogValue("disable_boot", m_chkDisableBoot->isChecked() ? "1" : "0");
        m_runner->setLogValue("disable_pam", m_chkDisablePam->isChecked() ? "1" : "0");
        
        m_runner->setLogValue("disable_coredumps", m_chkDisableCoredumps->isChecked() ? "1" : "0");
        m_runner->setLogValue("disable_modemmanager", m_chkDisableModemManager->isChecked() ? "1" : "0");
        m_runner->setLogValue("disable_nm_wait", m_chkDisableNmWait->isChecked() ? "1" : "0");
        m_runner->setLogValue("disable_smbd", m_chkDisableSmbd->isChecked() ? "1" : "0");
        m_runner->setLogValue("disable_nmbd", m_chkDisableNmbd->isChecked() ? "1" : "0");
        m_runner->setLogValue("disable_serial_getty", m_chkDisableSerialGetty->isChecked() ? "1" : "0");
        m_runner->setLogValue("disable_colord", m_chkDisableColord->isChecked() ? "1" : "0");
        m_runner->setLogValue("disable_smartd", m_chkDisableSmartd->isChecked() ? "1" : "0");
        m_runner->setLogValue("disable_usb_legacy", m_chkDisableUsbLegacy->isChecked() ? "1" : "0");
        m_runner->setLogValue("disable_pcspkr", m_chkDisablePcspkr->isChecked() ? "1" : "0");
        m_runner->setLogValue("disable_bluetooth", m_chkDisableBluetooth->isChecked() ? "1" : "0");
        m_runner->setLogValue("disable_print", m_chkDisablePrint->isChecked() ? "1" : "0");
        m_runner->setLogValue("disable_apparmor", m_chkDisableApparmor->isChecked() ? "1" : "0");
        m_runner->setLogValue("disable_ufw", m_chkDisableUfw->isChecked() ? "1" : "0");
        m_runner->setLogValue("disable_bluez_cups", m_chkDisableBluezCups->isChecked() ? "1" : "0");
        
        m_runner->writeProfile(TQString::number(roleVal), TQString::number(usageVal), TQString::number(ipv6Val), expVal, cstatesVal);
        m_runner->saveConfigs();
        
        m_btnNext->setEnabled(false);
        m_runner->runDryRun();
    } 
    else if (m_currentPage == 4) {
        // User confirmed configurations, let's save and apply
        saveUserCheckedParameters();
        
        m_currentPage = 5;
        m_wizardStack->slideInIdx(5, TQtSlidingStackedWidget::Left2Right);
        updateNavigationButtons();
        
        startTuningApply();
    } 
    else if (m_currentPage == 5) {
        // Finish clicked
        tqApp->quit();
    }
}

void MainWindow::goBack()
{
    if (m_currentPage > 0) {
        m_currentPage--;
        m_wizardStack->slideInIdx(m_currentPage, TQtSlidingStackedWidget::Right2Left);
        updateNavigationButtons();
    }
}

void MainWindow::onDryRunFinished(bool success)
{
    m_btnNext->setEnabled(true);
    if (!success) {
        TQMessageBox::critical(this,
            TRANS("ui.msg_hw_scan_error_title", "Hardware Scan Error"),
            TRANS("ui.msg_hw_scan_error_text", "Could not successfully run tuneperf.sh to detect hardware or build parameters."));
        return;
    }
    
    populateHardwareInfo();
    
    m_chkDisableJournald->setState(m_runner->getLogValue("disable_journald") == "1");
    m_chkDisableRsyslog->setState(m_runner->getLogValue("disable_rsyslog") == "1");
    m_chkDisableXorg->setState(m_runner->getLogValue("disable_xorg") == "1");
    m_chkDisableBoot->setState(m_runner->getLogValue("disable_boot") == "1");
    m_chkDisablePam->setState(m_runner->getLogValue("disable_pam") == "1");
    
    m_chkDisableCoredumps->setState(m_runner->getLogValue("disable_coredumps") == "1");
    m_chkDisableModemManager->setState(m_runner->getLogValue("disable_modemmanager") == "1");
    m_chkDisableNmWait->setState(m_runner->getLogValue("disable_nm_wait") == "1");
    m_chkDisableSmbd->setState(m_runner->getLogValue("disable_smbd") == "1");
    m_chkDisableNmbd->setState(m_runner->getLogValue("disable_nmbd") == "1");
    m_chkDisableSerialGetty->setState(m_runner->getLogValue("disable_serial_getty") == "1");
    m_chkDisableColord->setState(m_runner->getLogValue("disable_colord") == "1");
    m_chkDisableSmartd->setState(m_runner->getLogValue("disable_smartd") == "1");
    m_chkDisableUsbLegacy->setState(m_runner->getLogValue("disable_usb_legacy") == "1");
    m_chkDisablePcspkr->setState(m_runner->getLogValue("disable_pcspkr") == "1");
    m_chkDisableBluetooth->setState(m_runner->getLogValue("disable_bluetooth") == "1");
    m_chkDisablePrint->setState(m_runner->getLogValue("disable_print") == "1");
    m_chkDisableApparmor->setState(m_runner->getLogValue("disable_apparmor") == "1");
    m_chkDisableUfw->setState(m_runner->getLogValue("disable_ufw") == "1");
    m_chkDisableBluezCups->setState(m_runner->getLogValue("disable_bluez_cups") == "1");
    updateLogToggleLabels();
    onTabChanged(NULL);
    
    if (m_currentPage == 3) {
        // Populate tabs with the generated configs
        populateParametersTree();
        
        m_currentPage = 4;
        m_wizardStack->slideInIdx(4, TQtSlidingStackedWidget::Left2Right);
        updateNavigationButtons();
    }
}

void MainWindow::populateHardwareInfo()
{
    TQStringList hw = m_runner->hardwareInfo();
    TQString formatted = "<b>System Hardware Details Detected:</b><br><ul>";
    for (TQStringList::ConstIterator it = hw.begin(); it != hw.end(); ++it) {
        TQString item = *it;
        int colon = item.find(':');
        if (colon != -1) {
            TQString name = item.left(colon).stripWhiteSpace();
            TQString value = item.mid(colon + 1).stripWhiteSpace();
            formatted += TQString("<li><b><font color='#0096C7'>%1</font></b>: %2</li>").arg(name).arg(value);
        } else {
            formatted += TQString("<li>%1</li>").arg(item);
        }
    }
    formatted += "</ul>";
    m_lblHardwareInfo->setText(formatted);
}

static TQWidget* createScrollContainer(CustomTabWidget* tab, const TQString& tabTitle, TQVBoxLayout*& outLayout, TQObject* eventFilterObj) {
    TQScrollView* sv = new TQScrollView(tab);
    sv->setResizePolicy(TQScrollView::AutoOneFit);
    sv->setFrameShape(TQScrollView::NoFrame);
    
    // Customize scroll view palette to make the scrollbar handles visible (light gray)
    TQPalette svPal = sv->palette();
    TQColorGroup svActive = svPal.active();
    svActive.setColor(TQColorGroup::Button, TQColor("#3F3F46")); // lighter gray for scrollbar handle/buttons
    svActive.setColor(TQColorGroup::Background, TQColor("#121214")); // background dark
    svActive.setColor(TQColorGroup::Mid, TQColor("#27272A")); // mid gray
    
    TQColorGroup svInactive = svPal.inactive();
    svInactive.setColor(TQColorGroup::Button, TQColor("#3F3F46"));
    svInactive.setColor(TQColorGroup::Background, TQColor("#121214"));
    svInactive.setColor(TQColorGroup::Mid, TQColor("#27272A"));
    
    sv->setPalette(TQPalette(svActive, svPal.disabled(), svInactive));
    
    TQWidget* container = new TQWidget(sv->viewport(), "scroll_container");
    sv->addChild(container);
    if (eventFilterObj) {
        container->installEventFilter(eventFilterObj);
    }
    
    outLayout = new TQVBoxLayout(container, 12, 10);
    
    tab->addTab(sv, tabTitle);
    return container;
}

void MainWindow::populateParametersTree()
{
    // Clear tabs
    while (m_tabReview->count() > 0) {
        TQWidget* w = m_tabReview->page(0);
        m_tabReview->removePage(w);
        delete w;
    }
    m_reviewSwitches.clear();
    m_paramMap.clear();
    m_scriptLineMap.clear();

    TQVBoxLayout* laySwappiness = 0;
    TQVBoxLayout* layPerfs = 0;
    TQVBoxLayout* layNetwork = 0;
    TQVBoxLayout* laySecurity = 0;
    TQVBoxLayout* layModules = 0;
    TQVBoxLayout* laySysfs = 0;

    TQWidget* wSwappiness = createScrollContainer(m_tabReview, "Virtual Memory (Swap)", laySwappiness, this);
    TQWidget* wPerfs = createScrollContainer(m_tabReview, "System & Sched", layPerfs, this);
    TQWidget* wNetwork = createScrollContainer(m_tabReview, "Network TCP/IP", layNetwork, this);
    TQWidget* wSecurity = createScrollContainer(m_tabReview, "Security Hardening", laySecurity, this);
    TQWidget* wModules = createScrollContainer(m_tabReview, "Kernel Modules", layModules, this);
    TQWidget* wSysfs = createScrollContainer(m_tabReview, "Hardware & Sysfs", laySysfs, this);

    // Helpers to populate a list
    auto popList = [this](TQWidget* parent, TQVBoxLayout* lay, TuneParamList& list) {
        for (TuneParamList::Iterator it = list.begin(); it != list.end(); ++it) {
            TuneParam& p = *it;
            TQHBoxLayout* row = new TQHBoxLayout();
            row->setSpacing(8);
            
            TQtToggleSwitch* sw = new TQtToggleSwitch(TQtToggleSwitch::BorderSolidSoftRectNoText, p.enabled, parent);
            sw->setSuitableHeight(24);
            TQLabel* lbl;
            TQString text = p.key;
            if (!p.value.isEmpty()) {
                text = TQString("%1 = %2").arg(p.key).arg(p.value);
            }
            if (!p.currentValue.isEmpty()) {
                text += TQString(" <font color='#F59E0B'>(current: %1)</font>").arg(p.currentValue);
            }
            lbl = new TQLabel(text, parent);
            if (p.currentValue == "Unsupported") {
                sw->setState(false);
                sw->setEnabled(false);
                lbl->setEnabled(false);
                lbl->setPaletteForegroundColor(TQColor("#71717A"));
            } else {
                lbl->setPaletteForegroundColor(p.enabled ? TQColor("#00D2FF") : TQColor("#A0A0AA"));
            }
            
            sw->installEventFilter(this);
            lbl->installEventFilter(this);
            
            connect(sw, SIGNAL(stateChanged(bool)), this, SLOT(onReviewSwitchToggled(bool)));
            
            row->addWidget(sw);
            row->addWidget(lbl, 1);
            
            lay->addLayout(row);
            m_reviewSwitches.append(sw);
            
            SwitchMap m;
            m.sw = sw;
            m.lbl = lbl;
            m.param = &p;
            m_paramMap.append(m);
        }
        lay->addStretch(1);
    };

    TuneParamList swappinessList = m_runner->swappinessParams();
    popList(wSwappiness, laySwappiness, swappinessList);
    m_runner->setSwappinessParams(swappinessList);

    TuneParamList perfsList = m_runner->perfsParams();
    popList(wPerfs, layPerfs, perfsList);
    m_runner->setPerfsParams(perfsList);

    TuneParamList networkList = m_runner->networkParams();
    popList(wNetwork, layNetwork, networkList);
    m_runner->setNetworkParams(networkList);

    TuneParamList securityList = m_runner->securityParams();
    popList(wSecurity, laySecurity, securityList);
    m_runner->setSecurityParams(securityList);

    TuneParamList modulesList = m_runner->modulesParams();
    if (modulesList.isEmpty()) {
        layModules->addStretch(1);
        TQLabel* emptyLbl = new TQLabel(
            "No custom kernel modules are configured.\n\n"
            "Enable 'Experimental Tweaks' in the previous step\n"
            "to activate high-performance modules (BBR, FQ).", wModules);
        emptyLbl->setFont(TQFont("Inter", 11, TQFont::Bold));
        emptyLbl->setPaletteForegroundColor(TQColor("#71717A"));
        emptyLbl->setAlignment(TQt::AlignCenter);
        layModules->addWidget(emptyLbl);
        layModules->addStretch(1);
    } else {
        popList(wModules, layModules, modulesList);
    }
    m_runner->setModulesParams(modulesList);

    // Populate Sysfs script lines
    ScriptLineList scriptLines = m_runner->sysfsScriptLines();
    for (ScriptLineList::Iterator it = scriptLines.begin(); it != scriptLines.end(); ++it) {
        ScriptLine& s = *it;
        if (s.isCommand) {
            TQHBoxLayout* row = new TQHBoxLayout();
            row->setSpacing(8);
            
            TQtToggleSwitch* sw = new TQtToggleSwitch(TQtToggleSwitch::BorderSolidSoftRectNoText, s.enabled, wSysfs);
            sw->setSuitableHeight(24);
            TQString labelText = s.label;
            if (!s.currentValue.isEmpty()) {
                labelText += TQString(" <font color='#F59E0B'>(current: %1)</font>").arg(s.currentValue);
            }
            TQLabel* lbl = new TQLabel(labelText, wSysfs);
            if (s.currentValue == "Unsupported") {
                sw->setState(false);
                sw->setEnabled(false);
                lbl->setEnabled(false);
                lbl->setPaletteForegroundColor(TQColor("#71717A"));
            } else {
                lbl->setPaletteForegroundColor(s.enabled ? TQColor("#00D2FF") : TQColor("#A0A0AA"));
            }
            
            sw->installEventFilter(this);
            lbl->installEventFilter(this);
            
            connect(sw, SIGNAL(stateChanged(bool)), this, SLOT(onReviewSwitchToggled(bool)));
            
            row->addWidget(sw);
            row->addWidget(lbl, 1);
            
            laySysfs->addLayout(row);
            m_reviewSwitches.append(sw);
            
            ScriptLineMap m;
            m.sw = sw;
            m.lbl = lbl;
            m.line = &s;
            m_scriptLineMap.append(m);
        }
    }
    laySysfs->addStretch(1);
    m_runner->setSysfsScriptLines(scriptLines);
}

void MainWindow::saveUserCheckedParameters()
{
    // Update parameter values based on switches
    for (TQValueList<SwitchMap>::ConstIterator it = m_paramMap.begin(); it != m_paramMap.end(); ++it) {
        (*it).param->enabled = (*it).sw->isChecked();
    }
    for (TQValueList<ScriptLineMap>::ConstIterator it = m_scriptLineMap.begin(); it != m_scriptLineMap.end(); ++it) {
        (*it).line->enabled = (*it).sw->isChecked();
    }
    
    // Save to configuration files
    m_runner->saveConfigs();
}

void MainWindow::startTuningApply()
{
    m_btnBack->setEnabled(false);
    m_btnNext->setEnabled(false);
    m_logBuffer = "";
    m_txtLog->clear();
    m_lblApplyStatus->setText(TRANS("ui.apply_status_running", "Applying tuning optimizations, please wait..."));
    m_lblApplyStatus->setPaletteForegroundColor(TQColor("#00D2FF"));
    
    m_progressCircle->show();
    m_progressCircle->spin();
    
    m_runner->applyTuning(m_chkDisableCStates->isChecked());
}

void MainWindow::startRestore()
{
    if (TQMessageBox::question(this,
        TRANS("ui.msg_restore_confirm_title", "Restore Backups"), 
        TRANS("ui.msg_restore_confirm_text", "Are you sure you want to revert all system tuning modifications and restore the original configurations?"), 
        TQMessageBox::Yes, TQMessageBox::No) != TQMessageBox::Yes) {
        return;
    }
    
    m_currentPage = 5;
    m_wizardStack->slideInIdx(5, TQtSlidingStackedWidget::Left2Right);
    updateNavigationButtons();
    
    m_btnBack->setEnabled(false);
    m_btnNext->setEnabled(false);
    m_logBuffer = "";
    m_txtLog->clear();
    m_lblApplyStatus->setText(TRANS("ui.apply_status_restoring", "Restoring original configurations, please wait..."));
    m_lblApplyStatus->setPaletteForegroundColor(TQColor("#F59E0B"));
    
    m_progressCircle->show();
    m_progressCircle->spin();
    
    m_runner->runRestore();
}

void MainWindow::onApplyFinished(bool success)
{
    m_progressCircle->stopSpinning();
    m_progressCircle->hide();
    
    if (success) {
        m_lblApplyStatus->setText(TRANS("ui.apply_status_success", "Tuning applied successfully!"));
        m_lblApplyStatus->setPaletteForegroundColor(TQColor("#10B981"));
        TQMessageBox::information(this,
            TRANS("ui.msg_tuning_success_title", "Tuning Success"),
            TRANS("ui.msg_tuning_success_text", "All custom optimization parameters have been applied and persistent rules generated successfully!"));
    } else {
        m_lblApplyStatus->setText(TRANS("ui.apply_status_failed", "Tuning application failed."));
        m_lblApplyStatus->setPaletteForegroundColor(TQColor("#EF4444"));
        
        // Parse OK/FAILED counts from log buffer to differentiate partial failures
        int okCount = 0;
        int failedCount = 0;
        
        int idxOk = m_logBuffer.find(" OK</b>");
        if (idxOk != -1) {
            int idxStart = m_logBuffer.findRev("<b>", idxOk);
            if (idxStart != -1) {
                TQString okStr = m_logBuffer.mid(idxStart + 3, idxOk - (idxStart + 3));
                okCount = okStr.toInt();
            }
        }
        
        int idxFail = m_logBuffer.find(" FAILED</b>");
        if (idxFail != -1) {
            int idxStart = m_logBuffer.findRev("<b>", idxFail);
            if (idxStart != -1) {
                TQString failStr = m_logBuffer.mid(idxStart + 3, idxFail - (idxStart + 3));
                failedCount = failStr.toInt();
            }
        }
        
        if (okCount > 0 && failedCount > 0) {
            TQMessageBox::warning(this,
                TRANS("ui.msg_tuning_warning_title", "Tuning Warning"),
                TRANS("ui.msg_tuning_warning_text", "Some parameters failed to apply. Check execution log for details."));
        } else {
            TQMessageBox::critical(this,
                TRANS("ui.msg_tuning_error_title", "Tuning Error"),
                TRANS("ui.msg_tuning_error_text", "Failed to apply optimization parameters. Check execution log for details."));
        }
    }
    m_btnNext->setEnabled(true);
    updateNavigationButtons();
}

void MainWindow::onRestoreFinished(bool success)
{
    m_progressCircle->stopSpinning();
    m_progressCircle->hide();
    
    if (success) {
        m_lblApplyStatus->setText(TRANS("ui.apply_status_success_restore", "Restored successfully!"));
        m_lblApplyStatus->setPaletteForegroundColor(TQColor("#10B981"));
        TQMessageBox::information(this,
            TRANS("ui.msg_restore_success_title", "Restore Success"),
            TRANS("ui.msg_restore_success_text", "Original configurations restored, persistence service disabled, and udev rules removed successfully."));
    } else {
        m_lblApplyStatus->setText(TRANS("ui.apply_status_failed_restore", "Restore failed."));
        m_lblApplyStatus->setPaletteForegroundColor(TQColor("#EF4444"));
        TQMessageBox::critical(this,
            TRANS("ui.msg_restore_error_title", "Restore Error"),
            TRANS("ui.msg_restore_error_text", "Failed to restore backup configurations. Check log for details."));
    }
    m_btnNext->setEnabled(true);
    updateNavigationButtons();
}

void MainWindow::onLogMessage(const TQString& msg)
{
    TQString copy = msg;
    copy.replace("\n", "<br>");
    copy.replace("  ", "&nbsp;&nbsp;");
    m_logBuffer += copy;
    m_txtLog->setText("<font color=\"#39D353\">" + m_logBuffer + "</font>");
    m_txtLog->moveCursor(TQTextEdit::MoveEnd, false);
}

void MainWindow::onExperimentalStateChanged(bool checked)
{
    m_lblExperimental->setText(checked ? 
        "<b><font color='#00D2FF'>Enable Experimental Tweaks</font></b>" :
        "<b><font color='#A0A0AA'>Enable Experimental Tweaks</font></b>");
}

void MainWindow::onCStatesStateChanged(bool checked)
{
    m_lblDisableCStates->setText(checked ?
        TQString("<b><font color='#00D2FF'>Disable CPU C-States (Sleep states)</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getCStatesCurrentState()) :
        TQString("<b><font color='#A0A0AA'>Disable CPU C-States (Sleep states)</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getCStatesCurrentState()));
}

void MainWindow::onReviewSwitchToggled(bool checked)
{
    TQtToggleSwitch* sw = const_cast<TQtToggleSwitch*>(static_cast<const TQtToggleSwitch*>(sender()));
    if (!sw) return;
    
    for (TQValueList<SwitchMap>::Iterator it = m_paramMap.begin(); it != m_paramMap.end(); ++it) {
        if ((*it).sw == sw) {
            (*it).lbl->setPaletteForegroundColor(checked ? TQColor("#00D2FF") : TQColor("#A0A0AA"));
            return;
        }
    }
    for (TQValueList<ScriptLineMap>::Iterator it = m_scriptLineMap.begin(); it != m_scriptLineMap.end(); ++it) {
        if ((*it).sw == sw) {
            (*it).lbl->setPaletteForegroundColor(checked ? TQColor("#00D2FF") : TQColor("#A0A0AA"));
            return;
        }
    }
}

bool MainWindow::eventFilter(TQObject* watched, TQEvent* event)
{
    if (event->type() == TQEvent::Enter) {
        if (watched == m_btnLangue) {
            m_btnLangue->setIconSet(TQIconSet(m_pixLangueHover));
            m_btnLangue->setPaletteBackgroundColor(TQColor("#2A2A35"));
            return false;
        }
        if (watched == m_btnGithub) {
            m_btnGithub->setIconSet(TQIconSet(m_pixGithubHover));
            m_btnGithub->setPaletteBackgroundColor(TQColor("#2A2A35"));
            return false;
        }
        if (watched == m_btnAbout) {
            m_btnAbout->setIconSet(TQIconSet(m_pixAboutHover));
            m_btnAbout->setPaletteBackgroundColor(TQColor("#2A2A35"));
            return false;
        }
        // Handle button & combobox hover text contrast
        if (watched == m_btnNext || watched == m_btnBack || watched == m_btnExit || watched == m_btnRestore || watched == m_cbIPv6) {
            if (static_cast<TQWidget*>(watched)->isEnabled()) {
                static_cast<TQWidget*>(watched)->setPaletteForegroundColor(TQColor("#000000"));
            }
            return false;
        }

        // Advanced page toggles
        if (watched == m_chkExperimental || watched == m_lblExperimental) {
            TQString desc = TRANS("Enable Experimental Tweaks");
            if (!desc.isEmpty()) {
                m_lblPolicyHelp->setText(TQString("<b>%1</b>: %2").arg(TRANS("label.Enable Experimental Tweaks")).arg(desc));
            }
            return false;
        }
        if (watched == m_chkDisableCStates || watched == m_lblDisableCStates) {
            TQString desc = TRANS("Disable CPU C-States (Sleep states)");
            if (!desc.isEmpty()) {
                m_lblPolicyHelp->setText(TQString("<b>%1</b>: <font color='#EF4444'>%2</font>").arg(TRANS("label.Disable CPU C-States (Sleep states)")).arg(desc));
            }
            return false;
        }

        if (watched == m_chkDisableJournald || watched == m_lblDisableJournald) {
            TQString desc = TRANS("disable_journald");
            m_lblPolicyHelp->setText(TQString("<b>%1</b>: %2").arg(TRANS("label.disable_journald")).arg(desc));
            return false;
        }
        if (watched == m_chkDisableRsyslog || watched == m_lblDisableRsyslog) {
            TQString desc = TRANS("disable_rsyslog");
            m_lblPolicyHelp->setText(TQString("<b>%1</b>: %2").arg(TRANS("label.disable_rsyslog")).arg(desc));
            return false;
        }
        if (watched == m_chkDisableXorg || watched == m_lblDisableXorg) {
            TQString desc = TRANS("disable_xorg");
            m_lblPolicyHelp->setText(TQString("<b>%1</b>: %2").arg(TRANS("label.disable_xorg")).arg(desc));
            return false;
        }
        if (watched == m_chkDisableBoot || watched == m_lblDisableBoot) {
            TQString desc = TRANS("disable_boot");
            m_lblPolicyHelp->setText(TQString("<b>%1</b>: %2").arg(TRANS("label.disable_boot")).arg(desc));
            return false;
        }
        if (watched == m_chkDisablePam || watched == m_lblDisablePam) {
            TQString desc = TRANS("disable_pam");
            m_lblPolicyHelp->setText(TQString("<b>%1</b>: %2").arg(TRANS("label.disable_pam")).arg(desc));
            return false;
        }

        // Misc Toggles hovers
        TQString miscKeys[] = {
            "disable_coredumps", "disable_modemmanager", "disable_nm_wait",
            "disable_smbd", "disable_nmbd", "disable_serial_getty",
            "disable_colord", "disable_smartd", "disable_usb_legacy",
            "disable_pcspkr", "disable_bluetooth", "disable_print",
            "disable_apparmor", "disable_ufw", "disable_bluez_cups"
        };
        TQtToggleSwitch** miscChks[] = {
            &m_chkDisableCoredumps, &m_chkDisableModemManager, &m_chkDisableNmWait,
            &m_chkDisableSmbd, &m_chkDisableNmbd, &m_chkDisableSerialGetty,
            &m_chkDisableColord, &m_chkDisableSmartd, &m_chkDisableUsbLegacy,
            &m_chkDisablePcspkr, &m_chkDisableBluetooth, &m_chkDisablePrint,
            &m_chkDisableApparmor, &m_chkDisableUfw, &m_chkDisableBluezCups
        };
        TQLabel** miscLbls[] = {
            &m_lblDisableCoredumps, &m_lblDisableModemManager, &m_lblDisableNmWait,
            &m_lblDisableSmbd, &m_lblDisableNmbd, &m_lblDisableSerialGetty,
            &m_lblDisableColord, &m_lblDisableSmartd, &m_lblDisableUsbLegacy,
            &m_lblDisablePcspkr, &m_lblDisableBluetooth, &m_lblDisablePrint,
            &m_lblDisableApparmor, &m_lblDisableUfw, &m_lblDisableBluezCups
        };
        for (int i = 0; i < 15; ++i) {
            if (watched == *(miscChks[i]) || watched == *(miscLbls[i])) {
                TQString desc = TRANS(miscKeys[i]);
                m_lblPolicyHelp->setText(TQString("<b>%1</b>: %2").arg(TRANS(TQString("label.") + miscKeys[i])).arg(desc));
                return false;
            }
        }

        // Check m_paramMap
        for (TQValueList<SwitchMap>::ConstIterator it = m_paramMap.begin(); it != m_paramMap.end(); ++it) {
            if (watched == (*it).sw || watched == (*it).lbl) {
                TQString key = (*it).param->key;
                TQString explanation = TRANS(key);
                if (explanation == key || explanation.isEmpty()) {
                    explanation = (*it).param->comment;
                }
                if (explanation.isEmpty()) {
                    explanation = "No detailed description available.";
                }
                m_lblHelpDescription->setText(TQString("<b>%1</b>: %2").arg(key).arg(explanation));
                return false;
            }
        }

        // Check m_scriptLineMap
        for (TQValueList<ScriptLineMap>::ConstIterator it = m_scriptLineMap.begin(); it != m_scriptLineMap.end(); ++it) {
            if (watched == (*it).sw || watched == (*it).lbl) {
                TQString label = (*it).line->label;
                TQString explanation = TRANS(label);
                if (explanation == label || explanation.isEmpty()) {
                    explanation = (*it).line->text.stripWhiteSpace();
                }
                TQString transLabel = TRANS(TQString("label.") + label, label);
                m_lblHelpDescription->setText(TQString("<b>%1</b>: %2").arg(transLabel).arg(explanation));
                return false;
            }
        }
    }
    else if (event->type() == TQEvent::Leave) {
        if (watched == m_btnLangue) {
            m_btnLangue->setIconSet(TQIconSet(m_pixLangueNormal));
            m_btnLangue->setPaletteBackgroundColor(TQColor("#16161C"));
            return false;
        }
        if (watched == m_btnGithub) {
            m_btnGithub->setIconSet(TQIconSet(m_pixGithubNormal));
            m_btnGithub->setPaletteBackgroundColor(TQColor("#16161C"));
            return false;
        }
        if (watched == m_btnAbout) {
            m_btnAbout->setIconSet(TQIconSet(m_pixAboutNormal));
            m_btnAbout->setPaletteBackgroundColor(TQColor("#16161C"));
            return false;
        }
        if (watched == m_btnNext) {
            if (m_currentPage == 4 || m_currentPage == 5) {
                m_btnNext->setPaletteForegroundColor(TQColor("#FFFFFF"));
            } else {
                m_btnNext->setPaletteForegroundColor(TQColor("#E2E8F0"));
            }
            return false;
        }
        else if (watched == m_btnBack) {
            m_btnBack->setPaletteForegroundColor(TQColor("#E2E8F0"));
            return false;
        }
        else if (watched == m_btnExit) {
            m_btnExit->setPaletteForegroundColor(TQColor("#E2E8F0"));
            return false;
        }
        else if (watched == m_btnRestore) {
            m_btnRestore->setPaletteForegroundColor(TQColor("#FFFFFF"));
            return false;
        }
        else if (watched == m_cbIPv6) {
            m_cbIPv6->setPaletteForegroundColor(TQColor("#E2E8F0"));
            return false;
        }
        else if (watched && watched->name() && TQString(watched->name()) == "scroll_container") {
            if (m_lblHelpDescription) {
                m_lblHelpDescription->setText("Hover over any parameter or switch to read its detailed optimization purpose.");
            }
            if (m_lblPolicyHelp) {
                m_lblPolicyHelp->setText("Hover over any policy or log switch to read its detailed optimization purpose.");
            }
        }
    }
    else if (event->type() == TQEvent::MouseButtonRelease) {
        if (watched && watched->name() && TQString(watched->name()) == "about_github_link") {
            openUrl("https://github.com/seb3773/tuneperf");
            return true;
        }
    }
    
    return TQMainWindow::eventFilter(watched, event);
}

void MainWindow::onLogToggleChanged(bool)
{
    updateLogToggleLabels();
}

void MainWindow::updateLogToggleLabels()
{
    if (!m_chkDisableJournald) return;
    
    bool j = m_chkDisableJournald->isChecked();
    m_lblDisableJournald->setText(j ?
        TQString("<b><font color='#00D2FF'>Disable Systemd Journald Logs</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getLogCurrentState("/var/log/journal")) :
        TQString("<b><font color='#A0A0AA'>Disable Systemd Journald Logs</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getLogCurrentState("/var/log/journal")));
        
    bool r = m_chkDisableRsyslog->isChecked();
    m_lblDisableRsyslog->setText(r ?
        TQString("<b><font color='#00D2FF'>Disable Syslog & Kernel Logs (rsyslog)</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getLogCurrentState("/var/log/syslog")) :
        TQString("<b><font color='#A0A0AA'>Disable Syslog & Kernel Logs (rsyslog)</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getLogCurrentState("/var/log/syslog")));
        
    bool x = m_chkDisableXorg->isChecked();
    m_lblDisableXorg->setText(x ?
        TQString("<b><font color='#00D2FF'>Disable Xorg & TDM Display Logs</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getLogCurrentState("/var/log/Xorg.0.log")) :
        TQString("<b><font color='#A0A0AA'>Disable Xorg & TDM Display Logs</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getLogCurrentState("/var/log/Xorg.0.log")));
        
    bool b = m_chkDisableBoot->isChecked();
    m_lblDisableBoot->setText(b ?
        TQString("<b><font color='#00D2FF'>Disable Boot & Service Logs</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getLogCurrentState("/var/log/boot.log")) :
        TQString("<b><font color='#A0A0AA'>Disable Boot & Service Logs</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getLogCurrentState("/var/log/boot.log")));
        
    bool p = m_chkDisablePam->isChecked();
    m_lblDisablePam->setText(p ?
        TQString("<b><font color='#00D2FF'>Disable Login & Mail Notification Logs</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getLogCurrentState("/var/log/btmp")) :
        TQString("<b><font color='#A0A0AA'>Disable Login & Mail Notification Logs</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getLogCurrentState("/var/log/btmp")));

    // Misc Toggles
    bool cd = m_chkDisableCoredumps->isChecked();
    m_lblDisableCoredumps->setText(cd ?
        TQString("<b><font color='#00D2FF'>Disable Core Dumps</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getCoredumpCurrentState()) :
        TQString("<b><font color='#A0A0AA'>Disable Core Dumps</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getCoredumpCurrentState()));

    bool mm = m_chkDisableModemManager->isChecked();
    m_lblDisableModemManager->setText(mm ?
        TQString("<b><font color='#00D2FF'>Disable ModemManager service</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("ModemManager")) :
        TQString("<b><font color='#A0A0AA'>Disable ModemManager service</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("ModemManager")));

    bool nw = m_chkDisableNmWait->isChecked();
    m_lblDisableNmWait->setText(nw ?
        TQString("<b><font color='#00D2FF'>Disable NetworkManager-wait-online service</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("NetworkManager-wait-online")) :
        TQString("<b><font color='#A0A0AA'>Disable NetworkManager-wait-online service</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("NetworkManager-wait-online")));

    bool sm = m_chkDisableSmbd->isChecked();
    m_lblDisableSmbd->setText(sm ?
        TQString("<b><font color='#00D2FF'>Disable Samba smbd service</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("smbd")) :
        TQString("<b><font color='#A0A0AA'>Disable Samba smbd service</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("smbd")));

    bool nm = m_chkDisableNmbd->isChecked();
    m_lblDisableNmbd->setText(nm ?
        TQString("<b><font color='#00D2FF'>Disable Samba nmbd service</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("nmbd")) :
        TQString("<b><font color='#A0A0AA'>Disable Samba nmbd service</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("nmbd")));

    bool sg = m_chkDisableSerialGetty->isChecked();
    m_lblDisableSerialGetty->setText(sg ?
        TQString("<b><font color='#00D2FF'>Disable serial-getty service</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("serial-getty@ttyS0")) :
        TQString("<b><font color='#A0A0AA'>Disable serial-getty service</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("serial-getty@ttyS0")));

    bool co = m_chkDisableColord->isChecked();
    m_lblDisableColord->setText(co ?
        TQString("<b><font color='#00D2FF'>Disable colord service</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("colord")) :
        TQString("<b><font color='#A0A0AA'>Disable colord service</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("colord")));

    bool sd = m_chkDisableSmartd->isChecked();
    m_lblDisableSmartd->setText(sd ?
        TQString("<b><font color='#00D2FF'>Disable smartd service</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("smartd")) :
        TQString("<b><font color='#A0A0AA'>Disable smartd service</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("smartd")));

    bool ul = m_chkDisableUsbLegacy->isChecked();
    m_lblDisableUsbLegacy->setText(ul ?
        TQString("<b><font color='#00D2FF'>Disable legacy usbmouse/usbkbd modules (in favor of hid)</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getUsbLegacyCurrentState()) :
        TQString("<b><font color='#A0A0AA'>Disable legacy usbmouse/usbkbd modules (in favor of hid)</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getUsbLegacyCurrentState()));

    bool ps = m_chkDisablePcspkr->isChecked();
    m_lblDisablePcspkr->setText(ps ?
        TQString("<b><font color='#00D2FF'>Disable pc speaker module</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getPcspkrCurrentState()) :
        TQString("<b><font color='#A0A0AA'>Disable pc speaker module</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getPcspkrCurrentState()));

    bool bt = m_chkDisableBluetooth->isChecked();
    m_lblDisableBluetooth->setText(bt ?
        TQString("<b><font color='#00D2FF'>Disable Bluetooth service</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("bluetooth")) :
        TQString("<b><font color='#A0A0AA'>Disable Bluetooth service</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("bluetooth")));

    bool pr = m_chkDisablePrint->isChecked();
    m_lblDisablePrint->setText(pr ?
        TQString("<b><font color='#00D2FF'>Disable print services (CUPS/Avahi)</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("cups")) :
        TQString("<b><font color='#A0A0AA'>Disable print services (CUPS/Avahi)</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("cups")));

    bool aa = m_chkDisableApparmor->isChecked();
    m_lblDisableApparmor->setText(aa ?
        TQString("<b><font color='#00D2FF'>Disable AppArmor</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("apparmor")) :
        TQString("<b><font color='#A0A0AA'>Disable AppArmor</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getServiceCurrentState("apparmor")));

    bool uf = m_chkDisableUfw->isChecked();
    if (isUfwInstalled()) {
        m_lblDisableUfw->setText(uf ?
            TQString("<b><font color='#00D2FF'>Disable UFW Firewall</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getUfwCurrentState()) :
            TQString("<b><font color='#A0A0AA'>Disable UFW Firewall</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getUfwCurrentState()));
    } else {
        m_lblDisableUfw->setText(TQString("<b><font color='#71717A'>Disable UFW Firewall</font></b> <font color='#71717A'>(current: Not Installed)</font>"));
    }

    bool bc = m_chkDisableBluezCups->isChecked();
    if (isBluezCupsInstalled()) {
        m_lblDisableBluezCups->setText(bc ?
            TQString("<b><font color='#00D2FF'>Disable Bluez-cups printing backend</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getBluezCupsCurrentState()) :
            TQString("<b><font color='#A0A0AA'>Disable Bluez-cups printing backend</font></b> <font color='#F59E0B'>(current: %1)</font>").arg(getBluezCupsCurrentState()));
    } else {
        m_lblDisableBluezCups->setText(TQString("<b><font color='#71717A'>Disable Bluez-cups printing backend</font></b> <font color='#71717A'>(current: Not Installed)</font>"));
    }
}

void MainWindow::loadProfileSelection()
{
    TQString roleStr, usageStr, ipv6Str;
    bool expVal = false, cstatesVal = false;
    m_runner->readProfile(roleStr, usageStr, ipv6Str, expVal, cstatesVal);
    
    // Select role
    int roleIdx = roleStr.toInt();
    if (roleIdx >= 1 && roleIdx <= 6) {
        TQListViewItem* item = m_lvRoles->firstChild();
        while (item) {
            if (item->text(0).startsWith(roleStr)) {
                m_lvRoles->setSelected(item, true);
                m_lvRoles->setCurrentItem(item);
                break;
            }
            item = item->nextSibling();
        }
    }
    
    // Repopulate usages list based on the new role selection
    onRoleChanged(m_lvRoles->currentItem());
    
    // Select usage
    int usageIdx = usageStr.toInt();
    if (usageIdx >= 1) {
        TQListViewItem* item = m_lvUsages->firstChild();
        while (item) {
            if (item->text(0).startsWith(usageStr)) {
                m_lvUsages->setSelected(item, true);
                m_lvUsages->setCurrentItem(item);
                break;
            }
            item = item->nextSibling();
        }
    }
    
    // Select IPv6
    int ipv6Idx = ipv6Str.toInt() - 1;
    if (ipv6Idx >= 0 && ipv6Idx < m_cbIPv6->count()) {
        m_cbIPv6->setCurrentItem(ipv6Idx);
    }
    
    // Select Toggles
    m_chkExperimental->setState(expVal);
    m_chkDisableCStates->setState(cstatesVal);
    onExperimentalStateChanged(expVal);
    onCStatesStateChanged(cstatesVal);
}

void MainWindow::onTabChanged(TQWidget*)
{
    // Visual tab selection states are managed automatically by CustomTabWidget
}

void MainWindow::slotLanguageClicked()
{
    TQPopupMenu* menu = new TQPopupMenu(this);
    
#ifndef STATIC_BUILD
    TQPalette pal = menu->palette();
    
    // Active/Normal color group
    TQColorGroup activeGroup = pal.active();
    activeGroup.setColor(TQColorGroup::Foreground, TQColor("#FFFFFF"));
    activeGroup.setColor(TQColorGroup::ButtonText, TQColor("#FFFFFF"));
    activeGroup.setColor(TQColorGroup::Text, TQColor("#FFFFFF"));
    pal.setActive(activeGroup);
    
    // Inactive color group
    TQColorGroup inactiveGroup = pal.inactive();
    inactiveGroup.setColor(TQColorGroup::Foreground, TQColor("#FFFFFF"));
    inactiveGroup.setColor(TQColorGroup::ButtonText, TQColor("#FFFFFF"));
    inactiveGroup.setColor(TQColorGroup::Text, TQColor("#FFFFFF"));
    pal.setInactive(inactiveGroup);
    
    // Disabled color group
    TQColorGroup disabledGroup = pal.disabled();
    disabledGroup.setColor(TQColorGroup::Foreground, TQColor("#FFFFFF"));
    disabledGroup.setColor(TQColorGroup::ButtonText, TQColor("#FFFFFF"));
    disabledGroup.setColor(TQColorGroup::Text, TQColor("#FFFFFF"));
    pal.setDisabled(disabledGroup);
    
    menu->setPalette(pal);
#endif

    TQString lang = TranslationManager::instance().getLanguage();
    menu->insertItem("English", 1);
    menu->insertItem(TQString::fromUtf8("Français"), 2);
    menu->insertItem("Deutsch", 3);
    menu->insertItem(TQString::fromUtf8("Español"), 4);
    menu->insertItem("Italiano", 5);
    menu->insertItem("Polski", 6);
    
    int activeId = 1;
    if (lang == "fr") activeId = 2;
    else if (lang == "de") activeId = 3;
    else if (lang == "es") activeId = 4;
    else if (lang == "it") activeId = 5;
    else if (lang == "pl") activeId = 6;
    
    menu->setItemChecked(activeId, true);
    
    TQPoint pos = m_btnLangue->mapToGlobal(TQPoint(0, m_btnLangue->height()));
    int res = menu->exec(pos);
    if (res != -1) {
        TQString newLang = "en";
        if (res == 2) newLang = "fr";
        else if (res == 3) newLang = "de";
        else if (res == 4) newLang = "es";
        else if (res == 5) newLang = "it";
        else if (res == 6) newLang = "pl";
        
        if (newLang != lang) {
            TranslationManager::instance().setLanguage(newLang);
            updateTexts();
        }
    }
    delete menu;
}

void MainWindow::slotGithubClicked()
{
    openUrl("https://github.com/seb3773/tuneperf");
}

void MainWindow::slotAboutClicked()
{
    TQDialog* dlg = new TQDialog(this, "about_dialog", true);
    dlg->setCaption(TRANS("ui.about_title", "About TunePerf"));
    dlg->setPaletteBackgroundColor(TQColor("#000000"));
    dlg->resize(340, 380);
    
    TQVBoxLayout* lay = new TQVBoxLayout(dlg, 16, 12);
    
    // Header
    TQHBoxLayout* header = new TQHBoxLayout();
    header->setSpacing(8);
    header->setAlignment(TQt::AlignCenter);
    
    TQLabel* logoIcon = new TQLabel(dlg);
    TQPixmap logoPix = loadRawIcon(icons_tuneperfs_png);
    logoIcon->setPixmap(logoPix);
    header->addWidget(logoIcon);
    
    TQLabel* logoText = new TQLabel("<b><font color='#FFFFFF'>TUNE</font><font color='#00D2FF'>PERF</font></b>", dlg);
    logoText->setFont(TQFont("Outfit", 18, TQFont::Bold));
    header->addWidget(logoText);
    lay->addLayout(header);
    
    // Konqi Image
    TQLabel* konqiLbl = new TQLabel(dlg);
    TQPixmap konqiPix = loadRawIcon(icons_konqi_perfs_png);
    
    TQImage kImg = konqiPix.convertToImage();
    if (!kImg.isNull()) {
        TQPixmap scaled;
        scaled.convertFromImage(kImg.smoothScale(160, 160));
        konqiLbl->setPixmap(scaled);
    } else {
        konqiLbl->setPixmap(konqiPix);
    }
    konqiLbl->setAlignment(TQt::AlignCenter);
    lay->addWidget(konqiLbl);
    
    // Version
#ifdef STATIC_BUILD
    TQString versionText = TQString(TRANS("ui.about_version_static")).arg(TUNEPERF_VERSION);
#else
    TQString versionText = TQString(TRANS("ui.about_version_tde")).arg(TUNEPERF_VERSION);
#endif
    TQLabel* versionLbl = new TQLabel(versionText, dlg);
    versionLbl->setFont(TQFont("Inter", 11, TQFont::Bold));
    versionLbl->setPaletteForegroundColor(TQColor("#E2E8F0"));
    versionLbl->setAlignment(TQt::AlignCenter);
    lay->addWidget(versionLbl);
    
    // Author
    TQLabel* authorLbl = new TQLabel(TRANS("ui.about_by"), dlg);
    authorLbl->setFont(TQFont("Inter", 10, TQFont::Normal));
    authorLbl->setPaletteForegroundColor(TQColor("#A0A0AA"));
    authorLbl->setAlignment(TQt::AlignCenter);
    lay->addWidget(authorLbl);
    
    // Link
    TQLabel* linkLbl = new TQLabel("<u><font color='#00D2FF'>https://github.com/seb3773/tuneperf</font></u>", dlg, "about_github_link");
    linkLbl->setFont(TQFont("Inter", 10, TQFont::Normal));
    linkLbl->setAlignment(TQt::AlignCenter);
    linkLbl->setCursor(TQCursor(TQt::PointingHandCursor));
    linkLbl->installEventFilter(this);
    lay->addWidget(linkLbl);
    
    // Separator
    lay->addSpacing(8);
    
    // Button
    TQPushButton* btnOk = new TQPushButton(TRANS("ui.about_ok"), dlg);
    btnOk->setFixedWidth(100);
    btnOk->setPaletteBackgroundColor(TQColor("#252530"));
    btnOk->setPaletteForegroundColor(TQColor("#E2E8F0"));
    connect(btnOk, SIGNAL(clicked()), dlg, SLOT(accept()));
    
    TQHBoxLayout* btnRow = new TQHBoxLayout();
    btnRow->addStretch();
    btnRow->addWidget(btnOk);
    btnRow->addStretch();
    lay->addLayout(btnRow);
    
    dlg->exec();
    delete dlg;
}

void MainWindow::updateTexts()
{
    setCaption(TRANS("ui.window_title"));

    auto getTabPage = [](TQWidget* container) -> TQWidget* {
        if (!container) return (TQWidget*)0;
        TQWidget* p1 = static_cast<TQWidget*>(container->parent());
        if (!p1) return container;
        TQWidget* p2 = static_cast<TQWidget*>(p1->parent());
        return p2 ? p2 : container;
    };

    // Welcome page
    if (m_lblWelcomeTitle) m_lblWelcomeTitle->setText(TRANS("ui.welcome_title"));
    if (m_lblWelcomeDesc) m_lblWelcomeDesc->setText(TRANS("ui.welcome_desc"));
    if (m_gbHardwareBox) m_gbHardwareBox->setTitle(TRANS("ui.hardware_info"));
    populateHardwareInfo();

    // Role page
    if (m_lblRoleTitle) m_lblRoleTitle->setText(TRANS("ui.role_title"));
    if (m_lblRoleDesc) m_lblRoleDesc->setText(TRANS("ui.role_desc"));
    if (m_lvRoles) {
        m_lvRoles->setColumnText(0, TRANS("ui.role_hdr_name", "Role Profile"));
        m_lvRoles->setColumnText(1, TRANS("ui.role_hdr_desc", "Description"));
        populateRoles();
    }

    // Usage page
    if (m_lblUsageTitle) m_lblUsageTitle->setText(TRANS("ui.usage_title"));
    if (m_lblUsageDesc) m_lblUsageDesc->setText(TRANS("ui.usage_desc"));
    if (m_lvUsages) {
        m_lvUsages->setColumnText(0, TRANS("ui.usage_hdr_name", "Usage Profile"));
        m_lvUsages->setColumnText(1, TRANS("ui.usage_hdr_desc", "Optimization Target"));
        if (m_lvRoles) {
            onRoleChanged(m_lvRoles->currentItem());
        }
    }

    // Policies page
    if (m_lblPolicyTitle) m_lblPolicyTitle->setText(TRANS("ui.policies_title"));
    if (m_lblIPv6) m_lblIPv6->setText(TQString(TRANS("ui.ipv6_config", "IPv6 Protocol Configuration: <font color='#F59E0B'>(current: %1)</font>")).arg(TRANS(getIPv6CurrentState())));
    if (m_cbIPv6) {
        int ipv6Cur = m_cbIPv6->currentItem();
        m_cbIPv6->clear();
        m_cbIPv6->insertItem(TRANS("ui.ipv6_keep", "Keep Enabled (Default)"));
        m_cbIPv6->insertItem(TRANS("ui.ipv6_disable", "Disable Completely"));
        m_cbIPv6->insertItem(TRANS("ui.ipv6_local", "Local-Only (IPv6 Autoconf disabled)"));
        m_cbIPv6->setCurrentItem(ipv6Cur);
    }
    if (m_tabAdvanced) {
        if (m_tab1Container) m_tabAdvanced->setTabLabel(getTabPage(m_tab1Container), TRANS("ui.tab_system_policies", "System Policies"));
        if (m_tab2Container) m_tabAdvanced->setTabLabel(getTabPage(m_tab2Container), TRANS("ui.tab_logs", "Log Management"));
        if (m_tab3Container) m_tabAdvanced->setTabLabel(getTabPage(m_tab3Container), TRANS("ui.tab_misc", "Misc. Optimizations"));
    }
    if (m_lblExperimental) m_lblExperimental->setText(TQString("<b><font color='#A0A0AA'>%1</font></b>").arg(TRANS("Enable Experimental Tweaks")));
    if (m_lblDisableCStates) m_lblDisableCStates->setText(TQString("<b><font color='#A0A0AA'>%1</font></b> <font color='#F59E0B'>(current: %2)</font>").arg(TRANS("Disable CPU C-States (Sleep states)")).arg(TRANS(getCStatesCurrentState())));
    if (m_lblPolicyHelp) m_lblPolicyHelp->setText(TRANS("ui.help_desc_policy", "Hover over any policy or log switch to read its detailed optimization purpose."));
    updateLogToggleLabels();

    // Sidebar elements
    if (m_lblSidebarSubtitle) m_lblSidebarSubtitle->setText(TRANS("ui.sidebar_subtitle", "System Optimization"));
    if (m_lblRootText) m_lblRootText->setText(TRANS("ui.root_privileges", "Root Privileges"));
    for (int i = 0; i < (int)m_sidebarLabels.count(); ++i) {
        TQString key;
        if (i == 0) key = "ui.sidebar_welcome";
        else if (i == 1) key = "ui.sidebar_role";
        else if (i == 2) key = "ui.sidebar_profile";
        else if (i == 3) key = "ui.sidebar_policies";
        else if (i == 4) key = "ui.sidebar_editor";
        else if (i == 5) key = "ui.sidebar_optimization";
        m_sidebarLabels[i]->setText(TRANS(key));
    }

    // Review page
    if (m_lblReviewTitle) m_lblReviewTitle->setText(TRANS("ui.review_title"));
    if (m_lblReviewDesc) m_lblReviewDesc->setText(TRANS("ui.review_desc"));
    if (m_lblHelpDescription) m_lblHelpDescription->setText(TRANS("ui.help_desc"));
    // Re-populate the review parameters so they reflect updated labels/current-states
    populateParametersTree();

    // Apply page
    if (m_lblApplyTitle) m_lblApplyTitle->setText(TRANS("ui.apply_title"));
    if (m_lblApplyStatus) {
        TQString currentStatus = m_lblApplyStatus->text();
        if (currentStatus == "Starting execution...") m_lblApplyStatus->setText(TRANS("ui.apply_status_starting"));
        else if (currentStatus == "Applying tuning optimizations, please wait...") m_lblApplyStatus->setText(TRANS("ui.apply_status_running"));
        else if (currentStatus == "Restoring original configurations, please wait...") m_lblApplyStatus->setText(TRANS("ui.apply_status_restoring"));
        else if (currentStatus.contains("successfully") && currentStatus.contains("Restore")) m_lblApplyStatus->setText(TRANS("ui.apply_status_success_restore"));
        else if (currentStatus.contains("successfully")) m_lblApplyStatus->setText(TRANS("ui.apply_status_success"));
        else if (currentStatus.contains("Some parameters failed to apply")) m_lblApplyStatus->setText(TRANS("ui.apply_status_success_partial"));
        else if (currentStatus.contains("failed") && currentStatus.contains("Restore")) m_lblApplyStatus->setText(TRANS("ui.apply_status_failed_restore"));
        else if (currentStatus.contains("failed")) m_lblApplyStatus->setText(TRANS("ui.apply_status_failed"));
    }

    // Bottom buttons
    if (m_btnBack) m_btnBack->setText("< " + TRANS("ui.btn_back"));
    if (m_btnRestore) m_btnRestore->setText(TRANS("ui.btn_restore"));
    if (m_btnExit) m_btnExit->setText(TRANS("ui.btn_exit"));
    
    updateNavigationButtons();
}

void MainWindow::populateRoles()
{
    if (!m_lvRoles) return;
    
    // Save selection
    int selIdx = 0;
    TQListViewItem* cur = m_lvRoles->selectedItem();
    if (cur) {
        int idx = 0;
        for (TQListViewItem* it = m_lvRoles->firstChild(); it; it = it->nextSibling()) {
            if (it == cur) {
                selIdx = idx;
                break;
            }
            idx++;
        }
    }
    
    // Temporarily disconnect selection changed signal to avoid triggering slots during rebuild
    disconnect(m_lvRoles, SIGNAL(selectionChanged(TQListViewItem*)), this, SLOT(onRoleChanged(TQListViewItem*)));

    m_lvRoles->clear();
    
    TQListViewItem* it1 = new TQListViewItem(m_lvRoles, TRANS("role.desktop.name"), TRANS("role.desktop.desc"));
    TQListViewItem* it2 = new TQListViewItem(m_lvRoles, TRANS("role.workstation.name"), TRANS("role.workstation.desc"));
    TQListViewItem* it3 = new TQListViewItem(m_lvRoles, TRANS("role.gaming.name"), TRANS("role.gaming.desc"));
    TQListViewItem* it4 = new TQListViewItem(m_lvRoles, TRANS("role.server_light.name"), TRANS("role.server_light.desc"));
    TQListViewItem* it5 = new TQListViewItem(m_lvRoles, TRANS("role.server_heavy.name"), TRANS("role.server_heavy.desc"));
    TQListViewItem* it6 = new TQListViewItem(m_lvRoles, TRANS("role.vm_guest.name"), TRANS("role.vm_guest.desc"));

    // Restore selection
    TQListViewItem* selectIt = it1;
    if (selIdx == 1) selectIt = it2;
    else if (selIdx == 2) selectIt = it3;
    else if (selIdx == 3) selectIt = it4;
    else if (selIdx == 4) selectIt = it5;
    else if (selIdx == 5) selectIt = it6;
    
    m_lvRoles->setSelected(selectIt, true);
    m_lvRoles->setCurrentItem(selectIt);
    
    connect(m_lvRoles, SIGNAL(selectionChanged(TQListViewItem*)), this, SLOT(onRoleChanged(TQListViewItem*)));
}

#include "mainwindow.moc"
