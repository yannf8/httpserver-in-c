// Tests for module 5 — non-blocking I/O and epoll.
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "hx/mod5.hpp"
#include "tinytest.hpp"
#include "testutil.hpp"

// ------------------------------------------------------------------- 5.0 ----
HX_TEST("5.0", "set_nonblocking sets O_NONBLOCK") {
    int p[2];
    CHECK_SYS(pipe(p));
    CHECK_MSG(!hx::m5::is_nonblocking(p[0]), "a fresh pipe is blocking");
    CHECK_EQ(hx::m5::set_nonblocking(p[0]), 0);
    CHECK_MSG(hx::m5::is_nonblocking(p[0]), "O_NONBLOCK is not set");

    // With no writer and no data, a non-blocking read returns EAGAIN instead
    // of parking the thread. This is the whole basis of an event loop.
    char c;
    errno = 0;
    ssize_t n = read(p[0], &c, 1);
    CHECK_MSG(n < 0 && hx::is_would_block(errno), "expected EAGAIN from an empty non-blocking pipe");
    close(p[0]);
    close(p[1]);
}

HX_TEST("5.0", "set_nonblocking preserves the flags that were already there") {
    tu::TempFile f("x");
    int fd = open(f.path.c_str(), O_WRONLY | O_APPEND);
    CHECK_SYS(fd);
    CHECK_EQ(hx::m5::set_nonblocking(fd), 0);
    int flags = fcntl(fd, F_GETFL);
    CHECK_SYS(flags);
    CHECK_MSG(flags & O_NONBLOCK, "O_NONBLOCK missing");
    CHECK_MSG(flags & O_APPEND,
              "O_APPEND was clobbered — you overwrote the flags instead of OR-ing them");
    close(fd);
}

// ------------------------------------------------------------------- 5.1 ----
HX_TEST("5.1", "make_epoll returns a close-on-exec epoll descriptor") {
    int ep = hx::m5::make_epoll();
    CHECK_SYS(ep);
    int fl = fcntl(ep, F_GETFD);
    CHECK_SYS(fl);
    CHECK_MSG(fl & FD_CLOEXEC, "EPOLL_CLOEXEC was not used");
    close(ep);
}

HX_TEST("5.1", "epoll_add reports readiness for a readable pipe") {
    int p[2];
    CHECK_SYS(pipe(p));
    int ep = hx::m5::make_epoll();
    CHECK_SYS(ep);
    CHECK_EQ(hx::m5::epoll_add(ep, p[0], EPOLLIN), 0);

    struct epoll_event evs[4];
    // Nothing written yet: the wait must time out, not report readiness.
    CHECK_EQ(epoll_wait(ep, evs, 4, 50), 0);

    ssize_t ignored = write(p[1], "hi", 2);
    (void)ignored;
    int n = epoll_wait(ep, evs, 4, 1000);
    CHECK_EQ(n, 1);
    CHECK_MSG(evs[0].events & EPOLLIN, "expected EPOLLIN");
    CHECK_MSG(evs[0].data.fd == p[0],
              "data.fd should identify which connection is ready — that is the only thing "
              "epoll_wait hands back to you");

    close(ep);
    close(p[0]);
    close(p[1]);
}

HX_TEST("5.1", "epoll_mod and epoll_del change and remove interest") {
    int p[2];
    CHECK_SYS(pipe(p));
    int ep = hx::m5::make_epoll();
    CHECK_SYS(ep);

    // Watch the write end for writability.
    CHECK_EQ(hx::m5::epoll_add(ep, p[1], EPOLLOUT), 0);
    struct epoll_event evs[4];
    CHECK_EQ(epoll_wait(ep, evs, 4, 100), 1);
    CHECK_MSG(evs[0].events & EPOLLOUT, "an empty pipe is writable");

    // Stop caring about writability: an empty pipe is never readable, so the
    // wait should now time out.
    CHECK_EQ(hx::m5::epoll_mod(ep, p[1], EPOLLIN), 0);
    CHECK_EQ(epoll_wait(ep, evs, 4, 50), 0);

    CHECK_EQ(hx::m5::epoll_mod(ep, p[1], EPOLLOUT), 0);
    CHECK_EQ(epoll_wait(ep, evs, 4, 100), 1);

    CHECK_EQ(hx::m5::epoll_del(ep, p[1]), 0);
    CHECK_EQ(epoll_wait(ep, evs, 4, 50), 0);

    // Deleting twice must fail with ENOENT, not silently succeed.
    errno = 0;
    CHECK_MSG(hx::m5::epoll_del(ep, p[1]) < 0, "removing an fd that is not registered must fail");
    CHECK_EQ(errno, ENOENT);

    close(ep);
    close(p[0]);
    close(p[1]);
}

// ------------------------------------------------------------------- 5.2 ----
HX_TEST("5.2", "flush_out returns 1 when everything fits") {
    int sp[2];
    CHECK_SYS(socketpair(AF_UNIX, SOCK_STREAM, 0, sp));
    CHECK_EQ(hx::m5::set_nonblocking(sp[0]), 0);

    hx::m5::OutBuf b;
    b.reset("HTTP/1.1 200 OK\r\n\r\n");
    CHECK_EQ(hx::m5::flush_out(sp[0], &b), 1);
    CHECK_MSG(b.done(), "done() should be true after a full flush");
    CHECK_EQ((long)b.off, (long)b.data.size());

    char buf[64] = {};
    ssize_t n = read(sp[1], buf, sizeof buf);
    CHECK_EQ((long)n, (long)b.data.size());
    close(sp[0]);
    close(sp[1]);
}

HX_TEST("5.2", "flush_out returns 0 on a full socket and resumes exactly where it stopped") {
    int sp[2];
    CHECK_SYS(socketpair(AF_UNIX, SOCK_STREAM, 0, sp));
    CHECK_EQ(hx::m5::set_nonblocking(sp[0]), 0);
    CHECK_EQ(hx::m5::set_nonblocking(sp[1]), 0);

    std::string payload = tu::pattern(8 * 1024 * 1024, 41);  // far bigger than any socket buffer
    hx::m5::OutBuf b;
    b.reset(payload);

    int rc = hx::m5::flush_out(sp[0], &b);
    CHECK_MSG(rc == 0, "the socket buffer cannot hold 8 MiB — expected 0 (would block), got " +
                           std::to_string(rc));
    CHECK_MSG(b.off > 0, "some bytes should have been written before EAGAIN");
    CHECK_MSG(!b.done(), "done() must be false");

    // Now play the role of the event loop: drain the reader, flush again,
    // repeat until the buffer is empty.
    std::string received;
    int rounds = 0;
    while (rc == 0) {
        char buf[65536];
        for (;;) {
            ssize_t n = read(sp[1], buf, sizeof buf);
            if (n > 0) {
                received.append(buf, (size_t)n);
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            break;  // EAGAIN: reader drained
        }
        rc = hx::m5::flush_out(sp[0], &b);
        CHECK_MSG(rc >= 0, "flush_out reported an error mid-stream");
        if (++rounds > 5000) {
            CHECK_MSG(false, "flush_out is not making progress");
        }
    }
    CHECK_EQ(rc, 1);
    CHECK_MSG(b.done(), "done() must be true at the end");

    for (;;) {
        char buf[65536];
        ssize_t n = read(sp[1], buf, sizeof buf);
        if (n > 0) {
            received.append(buf, (size_t)n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
    CHECK_EQ((long long)received.size(), (long long)payload.size());
    CHECK_MSG(received == payload,
              "the bytes are wrong — a partial write resumed from the wrong offset");
    close(sp[0]);
    close(sp[1]);
}

HX_TEST("5.2", "flush_out reports a real error as -1") {
    int sp[2];
    CHECK_SYS(socketpair(AF_UNIX, SOCK_STREAM, 0, sp));
    CHECK_EQ(hx::m5::set_nonblocking(sp[0]), 0);
    signal(SIGPIPE, SIG_IGN);
    close(sp[1]);

    hx::m5::OutBuf b;
    b.reset("data");
    CHECK_EQ(hx::m5::flush_out(sp[0], &b), -1);
    CHECK_EQ(errno, EPIPE);
    close(sp[0]);
}

// ------------------------------------------------------------------- 5.7 ----
HX_TEST("5.7", "set_reuseport turns SO_REUSEPORT on") {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK_SYS(fd);
    CHECK_EQ(hx::m5::set_reuseport(fd), 0);
    int v = 0;
    socklen_t len = sizeof v;
    CHECK_SYS(getsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &v, &len));
    CHECK_MSG(v != 0, "SO_REUSEPORT is still off");
    close(fd);
}
