// Module 5 — non-blocking I/O and epoll.  Theory: docs/05-concurrency.md
//
// Run:  make test MODULE=5

#include "hx/mod5.hpp"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace hx::m5 {

// 5.0 — O_NONBLOCK
int set_nonblocking(int fd) {
    // TODO(5.0): F_GETFL, then F_SETFL with the extra bit. Overwriting the
    // flags instead of OR-ing them is a classic bug — it can silently drop
    // O_APPEND and friends.
    (void)fd;
    return kUnimplemented;
}

bool is_nonblocking(int fd) {
    // TODO(5.0)
    (void)fd;
    return false;
}

// 5.1 — epoll
int make_epoll() {
    // TODO(5.1): epoll_create1 with EPOLL_CLOEXEC. Why CLOEXEC?
    return kUnimplemented;
}

int epoll_add(int ep, int fd, uint32_t events) {
    // TODO(5.1): struct epoll_event { events; data }. Put the fd in
    // data.fd for now; later you will want data.ptr pointing at your
    // connection object instead. Zero the struct first.
    (void)ep;
    (void)fd;
    (void)events;
    return kUnimplemented;
}

int epoll_mod(int ep, int fd, uint32_t events) {
    // TODO(5.1)
    (void)ep;
    (void)fd;
    (void)events;
    return kUnimplemented;
}

int epoll_del(int ep, int fd) {
    // TODO(5.1): the last argument was allowed to be NULL only from Linux
    // 2.6.9 onwards; passing a dummy event is still common.
    (void)ep;
    (void)fd;
    return kUnimplemented;
}

// 5.2 — partial writes on a non-blocking socket
int flush_out(int fd, OutBuf* b) {
    // TODO(5.2): write from b->data.data() + b->off, remaining() bytes.
    //   - n > 0: advance off, keep going while not done
    //   - EAGAIN/EWOULDBLOCK: return 0 (the socket buffer is full)
    //   - EINTR: retry
    //   - anything else: return -1
    // This function is the whole reason an event loop needs EPOLLOUT.
    (void)fd;
    (void)b;
    return kUnimplemented;
}

// 5.7 — SO_REUSEPORT
int set_reuseport(int fd) {
    // TODO(5.7)
    (void)fd;
    return kUnimplemented;
}

}  // namespace hx::m5
