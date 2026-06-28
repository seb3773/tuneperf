#ifndef TQTCONCURRENT_H
#define TQTCONCURRENT_H

#include <ntqobject.h>
#include <ntqevent.h>
#include <ntqvariant.h>

#include <ntqstring.h>

class TQtFuture;

class TQtConcurrentRunnable {
public:
    virtual ~TQtConcurrentRunnable() {}
    virtual TQVariant run() = 0;
};

typedef TQVariant (*TQtConcurrentFunction)(void* arg);

namespace TQtConcurrent {
    void setMaxThreadCount(int n);
    int maxThreadCount();

    void shutdown();

    void runDetached(TQtConcurrentRunnable* runnable);
    void runDetached(TQtConcurrentFunction fn, void* arg);

    TQtFuture* run(TQtConcurrentRunnable* runnable);
    TQtFuture* run(TQtConcurrentFunction fn, void* arg);
}

class TQtFuture : public TQObject {
    TQ_OBJECT
public:
    TQtFuture(TQObject* parent = 0);
    ~TQtFuture();

    bool isFinished() const;
    bool hasError() const;

    TQVariant result() const;
    TQString errorString() const;

signals:
    void finished(const TQVariant& result);
    void error(const TQString& message);

protected:
    void customEvent(TQCustomEvent* e);

private:
    struct Private;
    Private* d;

    friend class TQtConcurrentInternal;
};

#endif
