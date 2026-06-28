#ifndef TQTPROCESS_H
#define TQTPROCESS_H

#include <ntqobject.h>
#include <ntqstring.h>
#include <ntqstringlist.h>

#include <ntqcstring.h>

class TQtProcess : public TQObject {
    TQ_OBJECT
public:
    enum ProcessError {
        FailedToStart,
        Crashed,
        Timedout,
        ReadError,
        WriteError,
        UnknownError
    };

    TQtProcess(TQObject* parent = 0);
    ~TQtProcess();

    void setWorkingDirectory(const TQString& dir);
    void setEnvironment(const TQStringList& env);

    void start(const TQString& program, const TQStringList& arguments = TQStringList());

    bool waitForFinished(int timeoutMs = -1);

    bool isRunning() const;
    bool isFinished() const;

    int exitCode() const;

    TQByteArray readAllStandardOutput();
    TQByteArray readAllStandardError();

    int write(const TQByteArray& data);
    void closeWriteChannel();

    void terminate();
    void kill();

    TQString errorString() const;

signals:
    void finished(int exitCode);
    void error(const TQString& message);
    void readyReadStandardOutput(const TQByteArray& data);
    void readyReadStandardError(const TQByteArray& data);

protected:
    void customEvent(TQCustomEvent* e);

private:
    struct Private;
    Private* d;
};

#endif
