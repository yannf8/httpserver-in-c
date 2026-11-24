// Module 0 — your first syscalls.  Theory: docs/00-orientation.md
//
// Fill in the TODOs. Run:  make test MODULE=0

#include "hx/mod0.hpp"

#include <fcntl.h>
#include <unistd.h>

namespace hx::m0 {

// 0.0 — already done. This is your proof that the harness works.
bool harness_ok() { return true; }

// 0.1 — write(2)
ssize_t write_str(int fd, std::string_view s) {
    // TODO(0.1): one call to write(2) with s.data() and s.size(). Return what
    // it returns. `man 2 write` tells you everything you need.
    (void)fd;
    (void)s;
    return kUnimplemented;
}

// 0.2 — errno
int errno_for_open(const char* path) {
    // TODO(0.2): open(path, O_RDONLY). On failure return errno; on success
    // close the fd and return 0. Remember: errno is only meaningful *after* a
    // call that actually failed.
    (void)path;
    return kUnimplemented;
}

}  // namespace hx::m0
