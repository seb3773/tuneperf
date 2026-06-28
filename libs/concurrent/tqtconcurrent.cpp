#include "tqtconcurrent.h"

#include <ntqapplication.h>
#include <ntqmutex.h>
#include <ntqvaluelist.h>
#include <ntqwaitcondition.h>

#include <pthread.h>

#include <string.h>

class TQtFutureResultEvent : public TQCustomEvent {
public:
    enum { TypeId = TQEvent::User + 126 };

    TQtFutureResultEvent() : TQCustomEvent((TQEvent::Type)TypeId), ok(0) {}

    int ok;
    TQVariant result;
    TQString error;
};

struct TQtFutureShared {
    pthread_mutex_t lock;
    int ref;

    TQtFuture* future;

    TQtConcurrentRunnable* runnable;
    TQtConcurrentFunction fn;
    void* arg;

    int has_fn;
    int abort;
    int detached;

    TQtFutureShared() : ref(1), future(0), runnable(0), fn(0), arg(0), has_fn(0), abort(0), detached(0) {
        pthread_mutex_init(&lock, 0);
    }

    ~TQtFutureShared() {
        pthread_mutex_destroy(&lock);
        if (runnable) delete runnable;
        runnable = 0;
    }
};

struct TQtFuture::Private {
    TQMutex lock;

    int finished;
    int ok;
    TQVariant result;
    TQString error;

    TQtFutureShared* shared;
};

static int g_pool_threads = 0;
static int g_pool_stop = 0;
static int g_pool_max_threads = 4;

static pthread_t* g_workers = 0;
static int g_workers_count = 0;

static TQMutex g_queue_lock;
static TQWaitCondition g_queue_cv;

static TQValueList<TQtFutureShared*> g_queue;

static inline void shared_ref(TQtFutureShared* s) {
    __sync_add_and_fetch(&s->ref, 1);
}

static inline void shared_unref(TQtFutureShared* s) {
    if (__sync_sub_and_fetch(&s->ref, 1) == 0) delete s;
}

static void post_future_event(TQtFutureShared* s, TQtFutureResultEvent* ev) {
    pthread_mutex_lock(&s->lock);
    TQtFuture* f = s->future;
    pthread_mutex_unlock(&s->lock);

    if (!f) {
        delete ev;
        return;
    }

    TQApplication::postEvent(f, ev);
}

static void* worker_main(void*) {
    for (;;) {
        g_queue_lock.lock();
        while (g_queue.isEmpty() && !g_pool_stop) {
            g_queue_cv.wait(&g_queue_lock);
        }
        if (g_pool_stop) {
            g_queue_lock.unlock();
            break;
        }

        TQtFutureShared* s = g_queue.front();
        g_queue.pop_front();
        g_queue_lock.unlock();

        if (!s) continue;

        if (__sync_add_and_fetch(&s->abort, 0)) {
            if (!s->detached) {
                TQtFutureResultEvent* ev = new TQtFutureResultEvent();
                ev->ok = 0;
                ev->error = "aborted";
                post_future_event(s, ev);
            }
            shared_unref(s);
            continue;
        }

        TQVariant r;
        int ok = 1;
        TQString err;

        if (s->has_fn) {
            if (!s->fn) {
                ok = 0;
                err = "null function";
            } else {
                r = s->fn(s->arg);
            }
        } else {
            if (!s->runnable) {
                ok = 0;
                err = "null runnable";
            } else {
                r = s->runnable->run();
            }
        }

        if (!s->detached) {
            TQtFutureResultEvent* ev = new TQtFutureResultEvent();
            ev->ok = ok;
            ev->result = r;
            ev->error = err;
            post_future_event(s, ev);
        }
        shared_unref(s);
    }

    return 0;
}

static void ensure_pool_started() {
    if (__sync_add_and_fetch(&g_pool_threads, 0) != 0) return;

    g_workers_count = g_pool_max_threads;
    g_workers = new pthread_t[g_workers_count];

    g_pool_stop = 0;
    __sync_lock_test_and_set(&g_pool_threads, 1);

    for (int i = 0; i < g_workers_count; ++i) {
        pthread_create(&g_workers[i], 0, worker_main, 0);
    }
}

class TQtConcurrentInternal {
public:
    static TQtFuture* submit(TQtFutureShared* s) {
        ensure_pool_started();

        TQtFuture* f = new TQtFuture(0);
        f->d->shared = s;

        pthread_mutex_lock(&s->lock);
        s->future = f;
        pthread_mutex_unlock(&s->lock);

        shared_ref(s);

        g_queue_lock.lock();
        g_queue.append(s);
        g_queue_cv.wakeOne();
        g_queue_lock.unlock();

        return f;
    }

    static void submit_detached(TQtFutureShared* s) {
        ensure_pool_started();

        s->detached = 1;

        g_queue_lock.lock();
        g_queue.append(s);
        g_queue_cv.wakeOne();
        g_queue_lock.unlock();
    }
};

TQtFuture::TQtFuture(TQObject* parent) : TQObject(parent) {
    d = new Private;
    d->finished = 0;
    d->ok = 0;
    d->shared = 0;
}

TQtFuture::~TQtFuture() {
    if (d->shared) {
        __sync_lock_test_and_set(&d->shared->abort, 1);
        pthread_mutex_lock(&d->shared->lock);
        d->shared->future = 0;
        pthread_mutex_unlock(&d->shared->lock);
        shared_unref(d->shared);
        d->shared = 0;
    }

    delete d;
    d = 0;
}

bool TQtFuture::isFinished() const { return d->finished != 0; }

bool TQtFuture::hasError() const { return d->finished && !d->ok; }

TQVariant TQtFuture::result() const { return d->result; }

TQString TQtFuture::errorString() const { return d->error; }

void TQtFuture::customEvent(TQCustomEvent* e) {
    if (!e || e->type() != (TQEvent::Type)TQtFutureResultEvent::TypeId) return;

    TQtFutureResultEvent* ev = (TQtFutureResultEvent*)e;

    d->result = ev->result;
    d->error = ev->error;
    d->ok = ev->ok;
    d->finished = 1;

    if (!d->ok) emit error(d->error);
    emit finished(d->result);
}

namespace TQtConcurrent {

void setMaxThreadCount(int n) {
    if (n < 1) n = 1;
    g_pool_max_threads = n;
}

int maxThreadCount() { return g_pool_max_threads; }

void shutdown() {
    if (__sync_add_and_fetch(&g_pool_threads, 0) == 0) return;

    g_queue_lock.lock();
    g_pool_stop = 1;
    g_queue_cv.wakeAll();
    g_queue_lock.unlock();

    for (int i = 0; i < g_workers_count; ++i) {
        pthread_join(g_workers[i], 0);
    }

    g_queue_lock.lock();
    while (!g_queue.isEmpty()) {
        TQtFutureShared* s = g_queue.front();
        g_queue.pop_front();
        if (s) {
            __sync_lock_test_and_set(&s->abort, 1);

            if (!s->detached) {
                TQtFutureResultEvent* ev = new TQtFutureResultEvent();
                ev->ok = 0;
                ev->error = "aborted";
                post_future_event(s, ev);
            }

            shared_unref(s);
        }
    }
    g_queue_lock.unlock();

    delete[] g_workers;
    g_workers = 0;
    g_workers_count = 0;
    g_pool_stop = 0;
    __sync_lock_test_and_set(&g_pool_threads, 0);
}

void runDetached(TQtConcurrentRunnable* runnable) {
    if (!runnable) return;
    TQtFutureShared* s = new TQtFutureShared();
    s->runnable = runnable;
    s->has_fn = 0;
    TQtConcurrentInternal::submit_detached(s);
}

void runDetached(TQtConcurrentFunction fn, void* arg) {
    TQtFutureShared* s = new TQtFutureShared();
    s->fn = fn;
    s->arg = arg;
    s->has_fn = 1;
    TQtConcurrentInternal::submit_detached(s);
}

TQtFuture* run(TQtConcurrentRunnable* runnable) {
    if (!runnable) return new TQtFuture(0);

    TQtFutureShared* s = new TQtFutureShared();
    s->runnable = runnable;
    s->has_fn = 0;
    return TQtConcurrentInternal::submit(s);
}

TQtFuture* run(TQtConcurrentFunction fn, void* arg) {
    TQtFutureShared* s = new TQtFutureShared();
    s->fn = fn;
    s->arg = arg;
    s->has_fn = 1;
    return TQtConcurrentInternal::submit(s);
}

}

#include "tqtconcurrent.moc"
