// Tests for module 1 — file descriptors and I/O discipline.
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

#include "hx/mod1.hpp"
#include "tinytest.hpp"
#include "testutil.hpp"

// ------------------------------------------------------------------- 1.0 ----
HX_TEST("1.0", "raw_write reaches the kernel without the libc wrapper") {
    int p[2];
    CHECK_SYS(pipe(p));
    const char* msg = "raw";
    long n = hx::m1::raw_write(p[1], msg, 3);
    CHECK_EQ(n, 3L);
    char buf[8] = {};
    CHECK_EQ((long)read(p[0], buf, sizeof buf), 3L);
    CHECK_STR_EQ(std::string(buf, 3), "raw");
    close(p[0]);
    close(p[1]);
}

HX_TEST("1.0", "raw_write surfaces failure for a bad descriptor") {
    long n = hx::m1::raw_write(999, "x", 1);
    CHECK_MSG(n < 0, "expected a negative return for EBADF");
}

// ------------------------------------------------------------------- 1.1 ----
HX_TEST("1.1", "write_all writes a small buffer completely") {
    int p[2];
    CHECK_SYS(pipe(p));
    std::string data = tu::pattern(1000);
    CHECK_EQ((long)hx::m1::write_all(p[1], data.data(), data.size()), (long)data.size());
    std::string got;
    got.resize(data.size());
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = read(p[0], &got[off], data.size() - off);
        if (n <= 0) break;
        off += (size_t)n;
    }
    CHECK_EQ((long)off, (long)data.size());
    CHECK(got == data);
    close(p[0]);
    close(p[1]);
}

HX_TEST("1.1", "write_all survives short writes and EINTR (512 KiB, slow reader, signals)") {
    tu::arm_interrupting_sigusr1();
    int p[2];
    CHECK_SYS(pipe(p));
    std::string data = tu::pattern(512 * 1024, 11);

    // The child reads slowly and pokes us with SIGUSR1 after every chunk, so
    // our write() calls get interrupted mid-flight.
    tu::Drainer d = tu::Drainer::start(p[0], p[1], /*delay_us=*/50, /*signal_parent=*/true);

    ssize_t n = hx::m1::write_all(p[1], data.data(), data.size());
    close(p[1]);

    uint64_t bytes = 0, hash = 0;
    CHECK_MSG(d.finish(&bytes, &hash), "reader child did not report back");
    CHECK_EQ((long)n, (long)data.size());
    CHECK_EQ((long long)bytes, (long long)data.size());
    CHECK_MSG(hash == tu::fnv1a(data),
              "the bytes that arrived are not the bytes we sent — an offset is wrong");
}

HX_TEST("1.1", "write_all returns -1 on a real error instead of looping forever") {
    ssize_t n = hx::m1::write_all(999, "x", 1);
    CHECK_MSG(n < 0, "expected -1 for EBADF");
    CHECK_EQ(errno, EBADF);
}

// ------------------------------------------------------------------- 1.2 ----
HX_TEST("1.2", "read_full keeps reading until it has everything asked for") {
    int p[2];
    CHECK_SYS(pipe(p));
    std::string data = tu::pattern(40000, 3);

    pid_t pid = fork();
    CHECK_SYS(pid);
    if (pid == 0) {
        close(p[0]);
        // Dribble it out in small pieces with gaps, so a single read() cannot
        // possibly return it all.
        size_t off = 0;
        while (off < data.size()) {
            size_t chunk = data.size() - off < 700 ? data.size() - off : 700;
            ssize_t w = write(p[1], data.data() + off, chunk);
            if (w <= 0) break;
            off += (size_t)w;
            usleep(200);
        }
        close(p[1]);
        _exit(0);
    }
    close(p[1]);

    std::string got;
    got.resize(data.size());
    ssize_t n = hx::m1::read_full(p[0], got.data(), got.size());
    CHECK_EQ((long)n, (long)data.size());
    CHECK_MSG(got == data, "content mismatch");
    close(p[0]);
    int st = 0;
    waitpid(pid, &st, 0);
}

HX_TEST("1.2", "read_full stops at EOF and reports the short count") {
    int p[2];
    CHECK_SYS(pipe(p));
    ssize_t ignored = write(p[1], "abcdefghij", 10);
    (void)ignored;
    close(p[1]);  // EOF

    char buf[100] = {};
    ssize_t n = hx::m1::read_full(p[0], buf, sizeof buf);
    CHECK_EQ((long)n, 10L);
    CHECK_STR_EQ(std::string(buf, 10), "abcdefghij");
    close(p[0]);
}

// ------------------------------------------------------------------- 1.3 ----
HX_TEST("1.3", "read_file returns the whole file") {
    std::string content = tu::pattern(70000, 5);
    tu::TempFile f(content);
    bool ok = false;
    std::string got = hx::m1::read_file(f.path.c_str(), &ok);
    CHECK_MSG(ok, "ok was not set to true");
    CHECK_EQ((long)got.size(), (long)content.size());
    CHECK_MSG(got == content, "content mismatch");
}

HX_TEST("1.3", "read_file handles an empty file") {
    tu::TempFile f("");
    bool ok = false;
    std::string got = hx::m1::read_file(f.path.c_str(), &ok);
    CHECK_MSG(ok, "an empty file is not an error");
    CHECK_EQ((long)got.size(), 0L);
}

HX_TEST("1.3", "read_file reads a /proc file that reports size 0") {
    // This is the test that fails if you fstat() and trust st_size.
    bool ok = false;
    std::string got = hx::m1::read_file("/proc/version", &ok);
    CHECK_MSG(ok, "could not read /proc/version");
    CHECK_MSG(got.size() > 5, "got fewer bytes than expected — did you trust st_size?");
    CHECK_STR_EQ(got.substr(0, 5), "Linux");
}

HX_TEST("1.3", "read_file reports failure for a missing file") {
    bool ok = true;
    std::string got = hx::m1::read_file("/definitely/not/here", &ok);
    CHECK_MSG(!ok, "ok should be false");
    CHECK_EQ((long)got.size(), 0L);
    CHECK_EQ(errno, ENOENT);
}

// ------------------------------------------------------------------- 1.4 ----
HX_TEST("1.4", "writev_all writes every iovec in order") {
    int p[2];
    CHECK_SYS(pipe(p));
    std::string a = "HTTP/1.1 200 OK\r\n";
    std::string b = "Content-Length: 5\r\n\r\n";
    std::string c = "hello";
    struct iovec iov[3];
    iov[0].iov_base = a.data();
    iov[0].iov_len = a.size();
    iov[1].iov_base = b.data();
    iov[1].iov_len = b.size();
    iov[2].iov_base = c.data();
    iov[2].iov_len = c.size();

    ssize_t n = hx::m1::writev_all(p[1], iov, 3);
    close(p[1]);
    CHECK_EQ((long)n, (long)(a.size() + b.size() + c.size()));

    std::string got;
    char buf[256];
    ssize_t r;
    while ((r = read(p[0], buf, sizeof buf)) > 0) got.append(buf, (size_t)r);
    close(p[0]);
    CHECK_STR_EQ(got, a + b + c);
}

HX_TEST("1.4", "writev_all handles partial vectored writes (1 MiB across 40 iovecs)") {
    tu::arm_interrupting_sigusr1();
    int p[2];
    CHECK_SYS(pipe(p));

    const int kVecs = 40;
    const size_t kEach = 26 * 1024;  // 40 * 26 KiB > the 64 KiB pipe buffer
    std::string data = tu::pattern(kVecs * kEach, 23);
    struct iovec iov[kVecs];
    for (int i = 0; i < kVecs; i++) {
        iov[i].iov_base = data.data() + (size_t)i * kEach;
        iov[i].iov_len = kEach;
    }

    tu::Drainer d = tu::Drainer::start(p[0], p[1], /*delay_us=*/20, /*signal_parent=*/true);
    ssize_t n = hx::m1::writev_all(p[1], iov, kVecs);
    close(p[1]);

    uint64_t bytes = 0, hash = 0;
    CHECK_MSG(d.finish(&bytes, &hash), "reader child did not report back");
    CHECK_EQ((long)n, (long)data.size());
    CHECK_EQ((long long)bytes, (long long)data.size());
    CHECK_MSG(hash == tu::fnv1a(data),
              "bytes arrived out of order or duplicated — check how you advance iov_base");
}

// ------------------------------------------------------------------- 1.5 ----
HX_TEST("1.5", "count_open_fds sees descriptors appear and disappear") {
    int before = hx::m1::count_open_fds();
    CHECK_MSG(before > 0, "expected at least stdin/stdout/stderr");

    int fds[10];
    for (int i = 0; i < 10; i++) {
        fds[i] = open("/dev/null", O_RDONLY);
        CHECK_SYS(fds[i]);
    }
    CHECK_EQ(hx::m1::count_open_fds(), before + 10);
    for (int i = 0; i < 10; i++) close(fds[i]);
    CHECK_EQ(hx::m1::count_open_fds(), before);
}

HX_TEST("1.5", "read_file does not leak descriptors, even on failures") {
    tu::TempFile f("some bytes");
    bool ok = false;
    // Warm up once: the first call may create lazily-initialised state.
    hx::m1::read_file(f.path.c_str(), &ok);
    int before = hx::m1::count_open_fds();
    CHECK_MSG(before > 0, "count_open_fds is not working yet (finish 1.5 first)");

    for (int i = 0; i < 200; i++) {
        hx::m1::read_file(f.path.c_str(), &ok);
        hx::m1::read_file("/definitely/not/here", &ok);
    }
    CHECK_EQ(hx::m1::count_open_fds(), before);
}
