// Tests for module 0 — setup and the syscall interface.
#include <fcntl.h>
#include <unistd.h>

#include "hx/mod0.hpp"
#include "tinytest.hpp"
#include "testutil.hpp"

HX_TEST("0.0", "the harness builds and runs (free pass)") {
    CHECK(hx::m0::harness_ok());
}

HX_TEST("0.1", "write_str puts the bytes on the fd and returns the count") {
    int p[2];
    CHECK_SYS(pipe(p));
    const std::string msg = "hello, kernel\n";

    ssize_t n = hx::m0::write_str(p[1], msg);
    CHECK_EQ((long)n, (long)msg.size());

    char buf[64] = {};
    ssize_t got = read(p[0], buf, sizeof buf);
    CHECK_EQ((long)got, (long)msg.size());
    CHECK_STR_EQ(std::string(buf, (size_t)got), msg);
    close(p[0]);
    close(p[1]);
}

HX_TEST("0.1", "write_str reports the error the kernel gave, without crashing") {
    // fd 999 is not open: write(2) must fail with EBADF.
    ssize_t n = hx::m0::write_str(999, "x");
    CHECK_MSG(n < 0, "writing to a closed fd should return -1");
    CHECK_EQ(errno, EBADF);
}

HX_TEST("0.2", "errno_for_open returns ENOENT for a missing file") {
    CHECK_EQ(hx::m0::errno_for_open("/definitely/not/here/at/all"), ENOENT);
}

HX_TEST("0.2", "errno_for_open returns ENOTDIR when a path component is a file") {
    CHECK_EQ(hx::m0::errno_for_open("/dev/null/nope"), ENOTDIR);
}

HX_TEST("0.2", "errno_for_open returns 0 when the file opens") {
    CHECK_EQ(hx::m0::errno_for_open("/dev/null"), 0);
}

HX_TEST("0.2", "errno_for_open does not leak the descriptor it opened") {
    // Open a marker fd, note its number, close it, then run errno_for_open
    // many times. If it leaks, the number we get back will have drifted.
    int marker = open("/dev/null", O_RDONLY);
    CHECK_SYS(marker);
    close(marker);
    for (int i = 0; i < 64; i++) CHECK_EQ(hx::m0::errno_for_open("/dev/null"), 0);
    int again = open("/dev/null", O_RDONLY);
    CHECK_SYS(again);
    CHECK_MSG(again == marker, "file descriptors are leaking: the lowest free fd moved");
    close(again);
}
