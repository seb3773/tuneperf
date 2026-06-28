#include "passworddialog.h"
#include <ntqlineedit.h>
#include <ntqlabel.h>
#include <ntqpushbutton.h>
#include <ntqlayout.h>
#include <ntqfont.h>
#include <ntqcolor.h>
#include <ntqpixmap.h>
#include <ntqapplication.h>
#include <stdio.h>
#include <sys/wait.h>

#include <ntqstylefactory.h>
#include "mainwindow.h"
#include "embedded_icons.h"

PasswordDialog::PasswordDialog(TQWidget* parent) 
    : TQDialog(parent, "password_dialog", true) 
{
    setCaption("TunePerf - Privilege Elevation");
    resize(460, 320); // Height expanded to 320px to prevent button clipping
    setPaletteBackgroundColor(TQColor("#121214"));
    
    TQVBoxLayout* lay = new TQVBoxLayout(this, 20, 12);
    
    // Header with Logo
    TQHBoxLayout* logoLay = new TQHBoxLayout();
    logoLay->setSpacing(8);
    logoLay->setAlignment(TQt::AlignCenter);
    
    TQLabel* logoIcon = new TQLabel(this);
    TQPixmap logoPix = MainWindow::loadRawIcon(icons_tuneperfs_png);
    logoIcon->setPixmap(logoPix);
    logoLay->addWidget(logoIcon);
    
    TQLabel* logoText = new TQLabel("<b><font color='#FFFFFF'>TUNE</font><font color='#00D2FF'>PERF</font></b>", this);
    logoText->setFont(TQFont("Outfit", 16, TQFont::Bold));
    logoLay->addWidget(logoText);
    
    lay->addLayout(logoLay);
    lay->addSpacing(4);
    
    // Description
    TQLabel* desc = new TQLabel(
        "TunePerf requires root privileges to optimize kernel parameters and sysfs configuration.\n\n"
        "Please enter your root password:", this);
    desc->setFont(TQFont("Inter", 10, TQFont::Normal));
    desc->setPaletteForegroundColor(TQColor("#A0A0AA"));
    desc->setAlignment(TQt::AlignCenter | TQt::WordBreak);
    lay->addWidget(desc);
    
    // Password Input
    m_txtPassword = new TQLineEdit(this);
    m_txtPassword->setEchoMode(TQLineEdit::Password);
    m_txtPassword->setPaletteBackgroundColor(TQColor("#1E1E26"));
    m_txtPassword->setPaletteForegroundColor(TQColor("#E2E8F0"));
    m_txtPassword->setFont(TQFont("Inter", 11, TQFont::Normal));
    m_txtPassword->setStyle(TQStyleFactory::create("Windows"));
    lay->addWidget(m_txtPassword);
    m_txtPassword->installEventFilter(this);
    
    // Error Label
    m_lblError = new TQLabel("", this);
    m_lblError->setFont(TQFont("Inter", 10, TQFont::Normal));
    m_lblError->setPaletteForegroundColor(TQColor("#EF4444")); // Red
    m_lblError->setAlignment(TQt::AlignCenter);
    lay->addWidget(m_lblError);
    
    // Button Box
    TQHBoxLayout* btnLay = new TQHBoxLayout();
    btnLay->setSpacing(16);
    
    m_btnCancel = new TQPushButton("Cancel", this);
    m_btnCancel->setPaletteBackgroundColor(TQColor("#252530"));
    m_btnCancel->setPaletteForegroundColor(TQColor("#E2E8F0"));
    connect(m_btnCancel, SIGNAL(clicked()), this, SLOT(reject()));
    btnLay->addWidget(m_btnCancel);
    
    m_btnUnlock = new TQPushButton("OK", this); // Renamed to "OK"
    m_btnUnlock->setPaletteBackgroundColor(TQColor("#10B981")); // Green
    m_btnUnlock->setPaletteForegroundColor(TQColor("#FFFFFF"));
    m_btnUnlock->setDefault(true);
    connect(m_btnUnlock, SIGNAL(clicked()), this, SLOT(onUnlock()));
    btnLay->addWidget(m_btnUnlock);
    
    lay->addLayout(btnLay);

    connect(m_txtPassword, SIGNAL(returnPressed()), this, SLOT(onUnlock()));
}

void PasswordDialog::onUnlock() {
    TQString pass = m_txtPassword->text();
    if (pass.isEmpty()) {
        m_lblError->setPaletteForegroundColor(TQColor("#EF4444"));
        m_lblError->setText("Password cannot be empty.");
        return;
    }
    m_lblError->setPaletteForegroundColor(TQColor("#F59E0B")); // Amber
    m_lblError->setText("Verifying password...");
    tqApp->processEvents();
    
    // Use low-level popen/pclose for password verification to avoid event loop conflicts
    FILE* fp = popen("sudo -S -k -p '' -v 2>/dev/null", "w");
    bool success = false;
    if (fp) {
        fwrite(pass.local8Bit().data(), 1, pass.length(), fp);
        fwrite("\n", 1, 1, fp);
        int status = pclose(fp);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            success = true;
        }
    }
    
    if (success) {
        m_password = pass;
        accept();
        return;
    }
    
    m_lblError->setPaletteForegroundColor(TQColor("#EF4444")); // Red
    m_lblError->setText("Incorrect password. Please try again.");
}

bool PasswordDialog::eventFilter(TQObject* watched, TQEvent* event)
{
    if (watched == m_txtPassword) {
        if (event->type() == TQEvent::Enter || event->type() == TQEvent::Leave) {
            return true; // Discard hover events to prevent style hover effect
        }
    }
    return TQDialog::eventFilter(watched, event);
}

#include "passworddialog.moc"
