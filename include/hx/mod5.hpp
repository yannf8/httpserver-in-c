// Module 5 — Concurrency: non-blocking fds, epoll, and partial writes.
// Theory: docs/05-concurrency.md
#pragma once

#include <sys/epoll.h>

#include "hx/common.hpp"

namespace hx::m5 {

// 5.0 — Add O_NONBLOCK to an fd's flags without clobbering the other flags
// (read them with F_GETFL first). Returns 0 or -1.
int set_nonblocking(int fd);
// Diagnostic used by the tests: is O_NONBLOCK set?
bool is_nonblocking(int fd);

// 5.1 — epoll wrappers. Create with EPOLL_CLOEXEC. `events` is a mask such as
// EPOLLIN, EPOLLOUT, EPOLLET. Each returns 0 / the epoll fd, or -1.
int make_epoll();
int epoll_add(int ep, int fd, uint32_t events);
int epoll_mod(int ep, int fd, uint32_t events);
int epoll_del(int ep, int fd);

// 5.2 — An output buffer that survives partial writes. `off` is how much of
// `data` has already reached the kernel.
struct OutBuf {
    std::string data;
    size_t off = 0;

    bool done() const { return off >= data.size(); }
    size_t remaining() const { return done() ? 0 : data.size() - off; }
    void reset(std::string s) {
        data = std::move(s);
        off = 0;
    }
};

// 5.2 — Write as much of *b as the kernel will take, right now, on a
// non-blocking fd. Advance b->off by what was written. Retry on EINTR.
// Returns  1 = fully flushed (b->done() is true)
//          0 = would block, some or none written — arm EPOLLOUT and come back
//         -1 = real error, errno set
int flush_out(int fd, OutBuf* b);

// 5.7 — SO_REUSEPORT: let several sockets bind the same port so the kernel
// load-balances accepts across them. Returns 0 or -1.
int set_reuseport(int fd);

}  // namespace hx::m5
