// common.hpp — shared includes and tiny helpers used across modules.
//
// `hx` = "hand-rolled HTTP". Everything you implement lives in namespace hx::mN
// where N is the module number, so the tests can talk about your work precisely.
#pragma once

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <string>
#include <string_view>
#include <vector>

namespace hx {

// Returned by stubs you have not implemented yet. Tests treat it as a failure.
inline constexpr int kUnimplemented = -1;

// Handy in your own code: "did this syscall fail only because a signal arrived?"
inline bool is_retryable(int e) { return e == EINTR; }
// "would this non-blocking call have blocked?" (EAGAIN and EWOULDBLOCK are the
// same value on Linux, but portable code checks both.)
inline bool is_would_block(int e) { return e == EAGAIN || e == EWOULDBLOCK; }

}  // namespace hx
