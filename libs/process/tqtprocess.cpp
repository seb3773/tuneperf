#include "tqtprocess.h"

#include "tqtconcurrent.h"

#include <ntqapplication.h>
#include <ntqmutex.h>
#include <ntqwaitcondition.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <pthread.h>

class TQtProcessFinishedEvent : public TQCustomEvent {
public:
    enum { TypeId = TQEvent::User + 125 };

    TQtProcessFinishedEvent() : TQCustomEvent((TQEvent::Type)TypeId) {
        exitCode = -1;
        ok = 0;
    }

    int exitCode;
    int ok;
    TQString error;
    TQByteArray out;
    TQByteArray err;
};

class TQtProcessOutputEvent : public TQCustomEvent {
public:
    enum { TypeId = TQEvent::User + 126 };

    TQtProcessOutputEvent(const TQByteArray& data, bool isStderr) 
        : TQCustomEvent((TQEvent::Type)TypeId), m_data(data), m_isStderr(isStderr) {}

    TQByteArray m_data;
    bool m_isStderr;
};

struct TQtProcessShared {
    pthread_mutex_t lock;
    int ref;

    TQtProcess* proc;

    pid_t pid;
    int fdIn;
    int fdOut;
    int fdErr;
    int started;
    int finished;
    int abort;

    int exitCode;
    TQString error;

    TQString program;
    TQStringList arguments;
    TQString workingDir;
    TQStringList environment;

    TQByteArray out;
    TQByteArray err;

    TQtProcessShared()
        : ref(1), proc(0), pid(-1), fdIn(-1), fdOut(-1), fdErr(-1), started(0), finished(0), abort(0), exitCode(-1), error(), program(), arguments(), workingDir(), environment(), out(), err() {
        pthread_mutex_init(&lock, 0);
    }

    ~TQtProcessShared() {
        if (fdIn >= 0) close(fdIn);
        if (fdOut >= 0) close(fdOut);
        if (fdErr >= 0) close(fdErr);
        pthread_mutex_destroy(&lock);
    }
};

static inline void shared_ref(TQtProcessShared* s) {
    __sync_add_and_fetch(&s->ref, 1);
}

static inline void shared_unref(TQtProcessShared* s) {
    if (__sync_sub_and_fetch(&s->ref, 1) == 0) delete s;
}

static void post_output_event(TQtProcessShared* s, TQtProcessOutputEvent* ev) {
    pthread_mutex_lock(&s->lock);
    TQtProcess* p = s->proc;
    pthread_mutex_unlock(&s->lock);

    if (!p) {
        delete ev;
        return;
    }

    TQApplication::postEvent(p, ev);
}

static void post_finished_event(TQtProcessShared* s, TQtProcessFinishedEvent* ev) {
    pthread_mutex_lock(&s->lock);
    TQtProcess* p = s->proc;
    pthread_mutex_unlock(&s->lock);

    if (!p) {
        delete ev;
        return;
    }

    TQApplication::postEvent(p, ev);
}

static void ba_append(TQByteArray* b, const char* p, int n) {
    if (!b || !p || n <= 0) return;
    const int old = b->size();
    b->resize(old + n);
    memcpy(b->data() + old, p, (size_t)n);
}

static int set_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static TQString errno_str(int e) {
    char buf[128];
    buf[0] = 0;
    strerror_r(e, buf, sizeof(buf));
    return TQString::fromLocal8Bit(buf);
}

static int spawn_child(const TQString& program,
                       const TQStringList& args,
                       const TQString& workingDir,
                       const TQStringList& environment,
                       pid_t* outPid,
                       int* outStdinFd,
                       int* outStdoutFd,
                       int* outStderrFd,
                       TQString* outErr) {
    if (!outPid || !outStdinFd || !outStdoutFd || !outStderrFd) return -1;

    int in_pipe[2];
    int out_pipe[2];
    int err_pipe[2];

    if (pipe(in_pipe) != 0) {
        if (outErr) *outErr = errno_str(errno);
        return -1;
    }
    if (pipe(out_pipe) != 0) {
        if (outErr) *outErr = errno_str(errno);
        close(in_pipe[0]);
        close(in_pipe[1]);
        return -1;
    }
    if (pipe(err_pipe) != 0) {
        if (outErr) *outErr = errno_str(errno);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return -1;
    }

    set_cloexec(in_pipe[0]);
    set_cloexec(in_pipe[1]);
    set_cloexec(out_pipe[0]);
    set_cloexec(out_pipe[1]);
    set_cloexec(err_pipe[0]);
    set_cloexec(err_pipe[1]);

    pid_t pid = fork();
    if (pid < 0) {
        if (outErr) *outErr = errno_str(errno);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        dup2(in_pipe[0], 0);
        dup2(out_pipe[1], 1);
        dup2(err_pipe[1], 2);

        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);

        if (!workingDir.isEmpty()) {
            const TQCString wd = workingDir.utf8();
            if (chdir(wd.data()) != 0) _exit(127);
        }

        if (!environment.isEmpty()) {
            clearenv();
            for (TQStringList::ConstIterator it = environment.begin(); it != environment.end(); ++it) {
                const TQCString e = (*it).utf8();
                char* dup = (char*)malloc((size_t)e.length() + 1);
                if (!dup) _exit(127);
                memcpy(dup, e.data(), (size_t)e.length() + 1);
                putenv(dup);
            }
        }

        const TQCString prog = program.utf8();

        int argc = 1 + (int)args.count();
        char** argv = (char**)malloc((size_t)(argc + 1) * sizeof(char*));
        if (!argv) _exit(127);

        argv[0] = (char*)prog.data();

        int i = 1;
        for (TQStringList::ConstIterator it = args.begin(); it != args.end(); ++it, ++i) {
            const TQCString a = (*it).utf8();
            const size_t an = strlen(a.data());
            char* dup = (char*)malloc(an + 1);
            if (!dup) _exit(127);
            memcpy(dup, a.data(), an + 1);
            argv[i] = dup;
        }
        argv[argc] = 0;

        execvp(argv[0], argv);
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);

    set_nonblock(in_pipe[1]);
    set_nonblock(out_pipe[0]);
    set_nonblock(err_pipe[0]);

    *outPid = pid;
    *outStdinFd = in_pipe[1];
    *outStdoutFd = out_pipe[0];
    *outStderrFd = err_pipe[0];

    return 0;
}

static void read_pipes_until_exit(TQtProcessShared* s, int fdOut, int fdErr, pid_t pid, int* outExitCode, TQString* outErr) {
    int outOpen = (fdOut >= 0);
    int errOpen = (fdErr >= 0);

    int statusKnown = 0;
    int status = 0;

    for (;;) {
        if (__sync_add_and_fetch(&s->abort, 0)) {
            if (pid > 0) kill(pid, SIGKILL);
        }

        if (!statusKnown && pid > 0) {
            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid) statusKnown = 1;
        }

        if (!outOpen && !errOpen && statusKnown) break;

        fd_set rfds;
        FD_ZERO(&rfds);

        int maxfd = -1;
        if (outOpen) {
            FD_SET(fdOut, &rfds);
            if (fdOut > maxfd) maxfd = fdOut;
        }
        if (errOpen) {
            FD_SET(fdErr, &rfds);
            if (fdErr > maxfd) maxfd = fdErr;
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        int sel = select(maxfd + 1, &rfds, 0, 0, &tv);
        if (sel < 0) {
            if (errno == EINTR) continue;
            if (outErr) *outErr = errno_str(errno);
            break;
        }

        if (sel == 0) continue;

        char buf[8192];

        if (outOpen && FD_ISSET(fdOut, &rfds)) {
            for (;;) {
                int rd = (int)read(fdOut, buf, sizeof(buf));
                if (rd > 0) {
                    ba_append(&s->out, buf, rd);
                    TQByteArray chunk;
                    ba_append(&chunk, buf, rd);
                    post_output_event(s, new TQtProcessOutputEvent(chunk, false));
                    continue;
                }
                if (rd == 0) {
                    close(fdOut);
                    pthread_mutex_lock(&s->lock);
                    if (s->fdOut == fdOut) s->fdOut = -1;
                    pthread_mutex_unlock(&s->lock);
                    fdOut = -1;
                    outOpen = 0;
                    break;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == EINTR) continue;
                close(fdOut);
                pthread_mutex_lock(&s->lock);
                if (s->fdOut == fdOut) s->fdOut = -1;
                pthread_mutex_unlock(&s->lock);
                fdOut = -1;
                outOpen = 0;
                break;
            }
        }

        if (errOpen && FD_ISSET(fdErr, &rfds)) {
            for (;;) {
                int rd = (int)read(fdErr, buf, sizeof(buf));
                if (rd > 0) {
                    ba_append(&s->err, buf, rd);
                    TQByteArray chunk;
                    ba_append(&chunk, buf, rd);
                    post_output_event(s, new TQtProcessOutputEvent(chunk, true));
                    continue;
                }
                if (rd == 0) {
                    close(fdErr);
                    pthread_mutex_lock(&s->lock);
                    if (s->fdErr == fdErr) s->fdErr = -1;
                    pthread_mutex_unlock(&s->lock);
                    fdErr = -1;
                    errOpen = 0;
                    break;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == EINTR) continue;
                close(fdErr);
                pthread_mutex_lock(&s->lock);
                if (s->fdErr == fdErr) s->fdErr = -1;
                pthread_mutex_unlock(&s->lock);
                fdErr = -1;
                errOpen = 0;
                break;
            }
        }
    }

    if (!statusKnown && pid > 0) {
        waitpid(pid, &status, 0);
        statusKnown = 1;
    }

    int code = -1;
    if (statusKnown) {
        if (WIFEXITED(status)) code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) code = 128 + WTERMSIG(status);
    }

    if (outExitCode) *outExitCode = code;
}

static TQVariant process_worker(void* arg) {
    TQtProcessShared* s = (TQtProcessShared*)arg;
    if (!s) return TQVariant();

    pthread_mutex_lock(&s->lock);
    pid_t pid = s->pid;
    int fdOut = s->fdOut;
    int fdErr = s->fdErr;
    pthread_mutex_unlock(&s->lock);

    TQString ioerr;
    int exitCode = -1;

    read_pipes_until_exit(s, fdOut, fdErr, pid, &exitCode, &ioerr);

    TQtProcessFinishedEvent* ev = new TQtProcessFinishedEvent();
    ev->exitCode = exitCode;
    ev->out = s->out;
    ev->err = s->err;

    if (!ioerr.isEmpty()) {
        ev->ok = 0;
        ev->error = ioerr;
    } else {
        ev->ok = 1;
    }

    post_finished_event(s, ev);

    shared_unref(s);
    return TQVariant();
}

struct TQtProcess::Private {
    mutable TQMutex lock;
    TQWaitCondition finishedCv;

    TQtProcessShared* shared;

    TQString workingDir;
    TQStringList environment;

    int running;
    int finished;

    int exitCode;
    TQString error;

    TQByteArray out;
    TQByteArray err;

    Private() : shared(0), workingDir(), environment(), running(0), finished(0), exitCode(-1), error(), out(), err() {}
};

TQtProcess::TQtProcess(TQObject* parent) : TQObject(parent) {
    d = new Private;
}

TQtProcess::~TQtProcess() {
    if (d->shared) {
        __sync_lock_test_and_set(&d->shared->abort, 1);
        pthread_mutex_lock(&d->shared->lock);
        pid_t pid = d->shared->pid;
        d->shared->proc = 0;
        pthread_mutex_unlock(&d->shared->lock);
        if (pid > 0) ::kill(pid, SIGKILL);
        shared_unref(d->shared);
        d->shared = 0;
    }

    delete d;
    d = 0;
}

void TQtProcess::setWorkingDirectory(const TQString& dir) {
    if (d->running) return;
    d->workingDir = dir;
}

void TQtProcess::setEnvironment(const TQStringList& env) {
    if (d->running) return;
    d->environment = env;
}

void TQtProcess::start(const TQString& program, const TQStringList& arguments) {
    if (d->running) return;

    if (d->shared) {
        shared_unref(d->shared);
        d->shared = 0;
    }

    d->out.resize(0);
    d->err.resize(0);
    d->error = TQString();
    d->exitCode = -1;
    d->finished = 0;

    TQtProcessShared* s = new TQtProcessShared();
    s->proc = this;

    s->program = program;
    s->arguments = arguments;
    s->workingDir = d->workingDir;
    s->environment = d->environment;

    pid_t pid = -1;
    int fdIn = -1;
    int fdOut = -1;
    int fdErr = -1;
    TQString err;

    if (spawn_child(program, arguments, d->workingDir, d->environment, &pid, &fdIn, &fdOut, &fdErr, &err) != 0) {
        d->error = err.isEmpty() ? TQString("failed to start") : err;
        shared_unref(s);
        emit error(d->error);
        return;
    }

    pthread_mutex_lock(&s->lock);
    s->pid = pid;
    s->fdIn = fdIn;
    s->fdOut = fdOut;
    s->fdErr = fdErr;
    pthread_mutex_unlock(&s->lock);

    d->shared = s;

    d->running = 1;

    shared_ref(s);
    TQtConcurrent::runDetached(process_worker, s);
}

bool TQtProcess::waitForFinished(int timeoutMs) {
    if (d->finished) return true;
    if (!d->running) return false;

    struct timespec ts0;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    const long long startNs = (long long)ts0.tv_sec * 1000000000LL + (long long)ts0.tv_nsec;
    const long long deadlineNs = (timeoutMs < 0) ? -1 : (startNs + (long long)timeoutMs * 1000000LL);

    for (;;) {
        if (d->finished) return true;

        if (deadlineNs >= 0) {
            struct timespec tsn;
            clock_gettime(CLOCK_MONOTONIC, &tsn);
            const long long nowNs = (long long)tsn.tv_sec * 1000000000LL + (long long)tsn.tv_nsec;
            if (nowNs >= deadlineNs) break;
        }

        long waitMs = 50;
        if (deadlineNs >= 0) {
            struct timespec tsn;
            clock_gettime(CLOCK_MONOTONIC, &tsn);
            const long long nowNs = (long long)tsn.tv_sec * 1000000000LL + (long long)tsn.tv_nsec;
            const long long remNs = deadlineNs - nowNs;
            if (remNs < 0) break;
            const long long remMs = remNs / 1000000LL;
            if (remMs < waitMs) waitMs = (long)remMs;
            if (waitMs < 1) waitMs = 1;
        }

        if (tqApp) tqApp->processEvents();

        d->lock.lock();
        if (!d->finished) d->finishedCv.wait(&d->lock, (unsigned long)waitMs);
        d->lock.unlock();
    }

    if (timeoutMs >= 0 && !d->finished) {
        d->error = TQString("timed out");

        if (d->shared) {
            pthread_mutex_lock(&d->shared->lock);
            pid_t pid = d->shared->pid;
            pthread_mutex_unlock(&d->shared->lock);

            if (pid > 0) ::kill(pid, SIGTERM);
            usleep(200000);

            if (!d->finished) {
                __sync_lock_test_and_set(&d->shared->abort, 1);
                if (pid > 0) ::kill(pid, SIGKILL);
            }
        }

        for (int spin = 0; spin < 20; ++spin) {
            if (d->finished) break;
            if (tqApp) tqApp->processEvents();
            d->lock.lock();
            if (!d->finished) d->finishedCv.wait(&d->lock, 25);
            d->lock.unlock();
        }

        return d->finished != 0;
    }

    return d->finished != 0;
}

bool TQtProcess::isRunning() const { return d->running != 0 && d->finished == 0; }

bool TQtProcess::isFinished() const { return d->finished != 0; }

int TQtProcess::exitCode() const { return d->exitCode; }

TQByteArray TQtProcess::readAllStandardOutput() {
    TQByteArray b = d->out;
    d->out = TQByteArray();
    return b;
}

TQByteArray TQtProcess::readAllStandardError() {
    TQByteArray b = d->err;
    d->err = TQByteArray();
    return b;
}

int TQtProcess::write(const TQByteArray& data) {
    if (!d->shared) return -1;
    if (data.isEmpty()) return 0;

    pthread_mutex_lock(&d->shared->lock);
    const int fd = d->shared->fdIn;
    pthread_mutex_unlock(&d->shared->lock);
    if (fd < 0) return -1;

    const char* p = data.data();
    int left = data.size();
    while (left > 0) {
        const int wr = (int)::write(fd, p, (size_t)left);
        if (wr > 0) {
            p += wr;
            left -= wr;
            continue;
        }
        if (wr < 0 && errno == EINTR) continue;
        if (wr < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        d->error = errno_str(errno);
        emit error(d->error);
        return -1;
    }

    return data.size() - left;
}

void TQtProcess::closeWriteChannel() {
    if (!d->shared) return;
    pthread_mutex_lock(&d->shared->lock);
    const int fd = d->shared->fdIn;
    d->shared->fdIn = -1;
    pthread_mutex_unlock(&d->shared->lock);
    if (fd >= 0) close(fd);
}

void TQtProcess::terminate() {
    if (!d->shared) return;
    pthread_mutex_lock(&d->shared->lock);
    pid_t pid = d->shared->pid;
    pthread_mutex_unlock(&d->shared->lock);
    if (pid > 0) ::kill(pid, SIGTERM);
}

void TQtProcess::kill() {
    if (!d->shared) return;
    __sync_lock_test_and_set(&d->shared->abort, 1);
    pthread_mutex_lock(&d->shared->lock);
    pid_t pid = d->shared->pid;
    pthread_mutex_unlock(&d->shared->lock);
    if (pid > 0) ::kill(pid, SIGKILL);
}

TQString TQtProcess::errorString() const { return d->error; }

void TQtProcess::customEvent(TQCustomEvent* e) {
    if (!e) return;

    if (e->type() == (TQEvent::Type)TQtProcessOutputEvent::TypeId) {
        TQtProcessOutputEvent* ev = (TQtProcessOutputEvent*)e;
        if (ev->m_isStderr) {
            emit readyReadStandardError(ev->m_data);
        } else {
            emit readyReadStandardOutput(ev->m_data);
        }
        return;
    }

    if (e->type() != (TQEvent::Type)TQtProcessFinishedEvent::TypeId) return;

    TQtProcessFinishedEvent* ev = (TQtProcessFinishedEvent*)e;

    d->exitCode = ev->exitCode;
    d->out = ev->out;
    d->err = ev->err;
    if (d->error.isEmpty()) d->error = ev->ok ? TQString() : ev->error;

    d->finished = 1;
    d->running = 0;

    closeWriteChannel();

    if (d->shared) {
        shared_unref(d->shared);
        d->shared = 0;
    }

    d->lock.lock();
    d->finishedCv.wakeAll();
    d->lock.unlock();

    if (!d->error.isEmpty()) emit error(d->error);
    emit finished(d->exitCode);
}

#include "tqtprocess.moc"
