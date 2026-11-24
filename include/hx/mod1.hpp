// Module 1 — File descriptors and I/O discipline.
// Theory: docs/01-syscalls.md
#pragma once

#include <sys/uio.h>  // struct iovec

#include "hx/common.hpp"

namespace hx::m1 {

// 1.0 — Call write(2) without using the libc wrapper: go through
// syscall(SYS_write, ...) yourself. Return exactly what the kernel returned
// (a negative return means the libc wrapper already put the error in errno).
long raw_write(int fd, const void* buf, size_t n);

// 1.1 — Write *all* n bytes. Loop over short writes. Retry on EINTR.
// Return n on success, or -1 with errno set on a real error.
ssize_t write_all(int fd, const void* buf, size_t n);

// 1.2 — Read up to n bytes into buf, looping until either n bytes have been
// read or EOF is reached. Retry on EINTR. Returns the number of bytes read
// (which may be < n at EOF), or -1 with errno set on error.
ssize_t read_full(int fd, void* buf, size_t n);

// 1.3 — Read an entire file into a string. open(2) / read(2) loop / close(2).
// On success set *ok = true and return the contents. On failure set *ok = false,
// leave errno as the failing syscall left it, and return "".
// Must not leak a file descriptor on any path, including the error paths.
std::string read_file(const char* path, bool* ok);

// 1.4 — Vectored write. Write every byte described by iov[0..iovcnt), handling
// partial writes by advancing into the iovec array (do NOT copy the buffers
// into one big buffer — the whole point is to avoid that copy).
// Returns the total bytes written, or -1 with errno set.
// You may modify the caller's iov array in place.
ssize_t writev_all(int fd, struct iovec* iov, int iovcnt);

// 1.5 — Diagnostic helper: how many file descriptors does this process have
// open right now? Read /proc/self/fd. Return -1 on failure.
// (The test uses this to prove 1.3 does not leak.)
int count_open_fds();

}  // namespace hx::m1
