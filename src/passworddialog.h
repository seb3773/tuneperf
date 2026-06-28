#ifndef PASSWORDDIALOG_H
#define PASSWORDDIALOG_H

#include <ntqdialog.h>
#include <ntqstring.h>

class TQLineEdit;
class TQLabel;
class TQPushButton;

class PasswordDialog : public TQDialog {
    TQ_OBJECT
public:
    PasswordDialog(TQWidget* parent = 0);
    TQString password() const { return m_password; }

private slots:
    void onUnlock();

protected:
    bool eventFilter(TQObject* watched, TQEvent* event) override;

private:
    TQLineEdit* m_txtPassword;
    TQLabel* m_lblError;
    TQPushButton* m_btnCancel;
    TQPushButton* m_btnUnlock;
    TQString m_password;
};

#endif
