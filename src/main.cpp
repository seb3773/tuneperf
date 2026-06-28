/*
 * main.cpp - Linux Performance Tuner GUI entry point
 */

#ifdef STATIC_BUILD
# include <ntqapplication.h>
# define I18N_NOOP(x) (x)
#else
# include <tdeapplication.h>
# include <tdeaboutdata.h>
# include <tdecmdlineargs.h>
# include <tdelocale.h>
#endif

#include <ntqdir.h>
#include <ntqfileinfo.h>
#include <unistd.h>
#include <stdlib.h>
#include "mainwindow.h"
#include "tqtconcurrent.h"
#include "passworddialog.h"
#include <ntqmessagebox.h>
#include "zx0em_runtime.h"

static const char description[] =
    I18N_NOOP("System Optimizer and Performance Tuning GUI for Linux");

static const char version[] = TUNEPERF_VERSION;

int main(int argc, char **argv)
{
    zx0em_init();

    // Ensure system administration commands are in PATH
    char* path = getenv("PATH");
    TQString pathStr = path ? path : "";
    if (!pathStr.contains("/usr/sbin") && !pathStr.contains("/sbin")) {
        pathStr = "/usr/local/sbin:/usr/sbin:/sbin:" + pathStr;
        setenv("PATH", pathStr.latin1(), 1);
    }

#ifndef STATIC_BUILD
    TDEAboutData about(
        "tuneperfs-gui",                      /* appName */
        I18N_NOOP("TunePerf GUI"),            /* programName */
        version,                               /* version */
        description,                           /* shortDescription */
        TDEAboutData::License_MIT,            /* licenseType */
        "(C) 2026, TunePerf GUI Contributors", /* copyrightStatement */
        0,                                     /* text */
        "https://github.com/seb3773",         /* homePageAddress */
        "bugs@tuneperfs.org"                   /* bugsEmailAddress */
    );

    about.addAuthor("seb3773", 0, 0);

    TDECmdLineArgs::init(argc, argv, &about);

    TDEApplication app;
#else
    TQApplication app(argc, argv);
#endif

    // Check if running directly as root
    if (getuid() == 0) {
        TQMessageBox::critical(NULL, "TunePerf - Access Denied",
            "Running this GUI directly as root is not permitted for security reasons.\n"
            "Please launch it as a normal user (without sudo).");
        return 1;
    }

    // Privilege elevation dialog to get the password for backend actions
    PasswordDialog dlg;
    
    TQPixmap appIcon = MainWindow::loadRawIcon(ZX0EM_tuneperfs_png);
    dlg.setIcon(appIcon);
    
    if (dlg.exec() != TQDialog::Accepted) {
        return 0; // User cancelled or closed the dialog
    }
    
    TQString adminPassword = dlg.password();

    // Run the GUI under the normal user session
    MainWindow* win = new MainWindow(0, 0, adminPassword);
    app.setMainWidget(win);
    win->show();
    
    int ret = app.exec();
    TQtConcurrent::shutdown();
    return ret;
}
