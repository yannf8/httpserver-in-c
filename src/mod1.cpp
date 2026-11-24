// Module 1 — file descriptors and I/O discipline.  Theory: docs/01-syscalls.md
//
// Run:  make test MODULE=1

#include "hx/mod1.hpp"

#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace hx::m1 {

// 1.0 — the syscall behind the wrapper
long raw_write(int fd, const void* buf, size_t n) {
    // TODO(1.0): syscall(SYS_write, fd, buf, n). man 2 syscall, man 2 syscalls.
    (void)fd;
    (void)buf;
    (void)n;
    return kUnimplemented;
}

// 1.1 — short writes are not an error
ssize_t write_all(int fd, const void* buf, size_t n) {
    // TODO(1.1): loop until all n bytes are gone.
    //   - write() may return less than you asked for. That is normal.
    //   - errno == EINTR means "a signal interrupted me, nothing was lost" -> retry.
    //   - a return of 0 for n > 0 should not happen on a regular fd; treat it
    //     as an error rather than spinning forever.
    (void)fd;
    (void)buf;
    (void)n;
    return kUnimplemented;
}

// 1.2 — short reads are not EOF
ssize_t read_full(int fd, void* buf, size_t n) {
    // TODO(1.2): loop until n bytes are read or read() returns 0 (real EOF).
    (void)fd;
    (void)buf;
    (void)n;
    return kUnimplemented;
}

// 1.3 — open / read / close without leaking
std::string read_file(const char* path, bool* ok) {
    // TODO(1.3): read the whole file into a string using a read() loop.
    //   Do not fstat() and trust st_size — files can change size, and /proc
    //   files report size 0. Grow the string as you go.
    //   Every early return must still close the fd.
    (void)path;
    if (ok) *ok = false;
    return {};
}

// 1.4 — vectored I/O
ssize_t writev_all(int fd, struct iovec* iov, int iovcnt) {
    // TODO(1.4): writev() can also be short. Advance past fully written
    // iovecs, and adjust iov_base/iov_len of the partially written one.
    // This is exactly how you send "status line + headers + body" in one
    // syscall instead of three.
    (void)fd;
    (void)iov;
    (void)iovcnt;
    return kUnimplemented;
}

// 1.5 — proof of no leaks
int count_open_fds() {
    // TODO(1.5): count the entries in /proc/self/fd, excluding "." and "..".
    // Note: the directory handle you open to read it is itself an open fd.
    // Decide what you count and be consistent — the test only compares two
    // counts taken the same way.
    return kUnimplemented;
}

}  // namespace hx::m1
