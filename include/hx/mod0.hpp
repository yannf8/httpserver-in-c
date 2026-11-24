// Module 0 — Setup and the syscall interface.
// Theory: docs/00-orientation.md
#pragma once

#include "hx/common.hpp"

namespace hx::m0 {

// 0.0 — nothing to implement. `harness_ok()` already returns true; it proves the
// build + test loop works on your machine.
bool harness_ok();

// 0.1 — Your first syscall. Write the whole of `s` to file descriptor `fd`
// using write(2) exactly once (assume the write is not short — you will fix
// that assumption in 1.1). Return the value write(2) gave you.
ssize_t write_str(int fd, std::string_view s);

// 0.2 — errno. Attempt to open `path` for reading. If it fails, return the
// errno value; if it succeeds, close it and return 0.
int errno_for_open(const char* path);

}  // namespace hx::m0
