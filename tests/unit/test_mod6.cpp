// Tests for module 6 — measurement and latency.
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sched.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "hx/mod6.hpp"
#include "tinytest.hpp"
#include "testutil.hpp"

// ------------------------------------------------------------------- 6.0 ----
HX_TEST("6.0", "now_ns is nanoseconds and never goes backwards") {
    uint64_t a = hx::m6::now_ns();
    CHECK_MSG(a > 0, "now_ns returned 0");

    // 1e9 ns of uptime is 1 second; a machine that has been up for a while will
    // be far above this. Catches returning seconds or microseconds by mistake.
    CHECK_MSG(a > 1000000ull, "the magnitude looks wrong — are you returning nanoseconds?");

    uint64_t prev = a;
    for (int i = 0; i < 1000; i++) {
        uint64_t now = hx::m6::now_ns();
        CHECK_MSG(now >= prev, "time went backwards");
        prev = now;
    }
}

HX_TEST("6.0", "now_ns measures a 20 ms sleep as roughly 20 ms") {
    uint64_t t0 = hx::m6::now_ns();
    struct timespec ts{0, 20 * 1000 * 1000};
    nanosleep(&ts, nullptr);
    uint64_t elapsed = hx::m6::now_ns() - t0;
    CHECK_MSG(elapsed > 15ull * 1000 * 1000,
              "measured " + std::to_string(elapsed) + " ns for a 20 ms sleep (too small)");
    CHECK_MSG(elapsed < 500ull * 1000 * 1000,
              "measured " + std::to_string(elapsed) + " ns for a 20 ms sleep (too large)");
}

// ------------------------------------------------------------------- 6.1 ----
HX_TEST("6.1", "an empty Hist answers without crashing") {
    hx::m6::Hist h;
    CHECK_EQ((long)h.count(), 0L);
    CHECK_EQ((long long)h.percentile(0.5), 0LL);
    CHECK_EQ((long long)h.percentile(0.99), 0LL);
    CHECK_MSG(h.mean() == 0.0, "mean of nothing is 0 by our contract, not a division by zero");
}

HX_TEST("6.1", "Hist percentiles use nearest-rank on the sorted samples") {
    hx::m6::Hist h;
    // Insert 1..1000 in a scrambled but deterministic order: a correct
    // implementation must sort, not assume insertion order.
    for (int i = 0; i < 1000; i++) h.add((uint64_t)(((i * 617) % 1000) + 1));

    CHECK_EQ((long)h.count(), 1000L);
    CHECK_EQ((long long)h.min(), 1LL);
    CHECK_EQ((long long)h.max(), 1000LL);
    CHECK_EQ((long long)h.percentile(0.0), 1LL);
    CHECK_EQ((long long)h.percentile(0.5), 500LL);
    CHECK_EQ((long long)h.percentile(0.9), 900LL);
    CHECK_EQ((long long)h.percentile(0.99), 990LL);
    CHECK_EQ((long long)h.percentile(0.999), 999LL);
    CHECK_EQ((long long)h.percentile(1.0), 1000LL);
    CHECK_MSG(h.mean() > 500.0 && h.mean() < 501.0,
              "mean should be 500.5, got " + std::to_string(h.mean()));
}

HX_TEST("6.1", "Hist handles a single sample and reset") {
    hx::m6::Hist h;
    h.add(42);
    CHECK_EQ((long)h.count(), 1L);
    CHECK_EQ((long long)h.percentile(0.0), 42LL);
    CHECK_EQ((long long)h.percentile(0.5), 42LL);
    CHECK_EQ((long long)h.percentile(1.0), 42LL);
    CHECK_EQ((long long)h.min(), 42LL);
    CHECK_EQ((long long)h.max(), 42LL);
    h.reset();
    CHECK_EQ((long)h.count(), 0L);
}

HX_TEST("6.1", "Hist copes with the wide spread real latency has") {
    hx::m6::Hist h;
    for (int i = 0; i < 999; i++) h.add(1000);   // 1 us, 99.9% of the time
    h.add(50ull * 1000 * 1000);                  // one 50 ms stall
    CHECK_EQ((long long)h.percentile(0.5), 1000LL);
    CHECK_EQ((long long)h.percentile(0.99), 1000LL);
    CHECK_EQ((long long)h.percentile(1.0), 50000000LL);
    CHECK_MSG(h.mean() > 1000.0,
              "this is the point of the module: the mean hides the stall, the max does not");
}

// ------------------------------------------------------------------- 6.2 ----
HX_TEST("6.2", "set_tcp_nodelay disables Nagle") {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK_SYS(fd);
    int v = -1;
    socklen_t len = sizeof v;
    CHECK_SYS(getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &v, &len));
    CHECK_EQ(v, 0);
    CHECK_EQ(hx::m6::set_tcp_nodelay(fd), 0);
    CHECK_SYS(getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &v, &len));
    CHECK_MSG(v != 0, "TCP_NODELAY is still off");
    close(fd);
}

// ------------------------------------------------------------------- 6.3 ----
HX_TEST("6.3", "pin_to_cpu restricts the thread to one core") {
    CHECK_EQ(hx::m6::pin_to_cpu(0), 0);

    cpu_set_t set;
    CPU_ZERO(&set);
    CHECK_SYS(sched_getaffinity(0, sizeof set, &set));
    CHECK_MSG(CPU_ISSET(0, &set), "cpu 0 should be in the mask");
    CHECK_EQ(CPU_COUNT(&set), 1);

    sched_yield();
    int cpu = sched_getcpu();
    CHECK_MSG(cpu == 0, "we asked for cpu 0 but the scheduler put us on " + std::to_string(cpu));
}

HX_TEST("6.3", "pin_to_cpu fails for a cpu that does not exist") {
    // 1000 is inside CPU_SETSIZE (1024) but no machine you will meet has that
    // many cores, so the kernel refuses the mask. Do not pass a number >=
    // CPU_SETSIZE: CPU_SET would write past the end of the cpu_set_t.
    errno = 0;
    CHECK_MSG(hx::m6::pin_to_cpu(1000) < 0, "pinning to cpu 1000 should fail");
}

// ------------------------------------------------------------------- 6.5 ----
HX_TEST("6.5", "send_file_all copies a file to a socket without a userspace buffer") {
    std::string content = tu::pattern(300 * 1024, 61);
    tu::TempFile f(content);
    int in_fd = open(f.path.c_str(), O_RDONLY);
    CHECK_SYS(in_fd);

    int sp[2];
    CHECK_SYS(socketpair(AF_UNIX, SOCK_STREAM, 0, sp));
    tu::Drainer d = tu::Drainer::start(sp[1], sp[0]);  // 300 KiB will not fit in the socket buffer

    ssize_t n = hx::m6::send_file_all(sp[0], in_fd, 0, content.size());
    close(sp[0]);

    uint64_t bytes = 0, hash = 0;
    CHECK_MSG(d.finish(&bytes, &hash), "reader child did not report back");
    CHECK_EQ((long long)n, (long long)content.size());
    CHECK_EQ((long long)bytes, (long long)content.size());
    CHECK_MSG(hash == tu::fnv1a(content), "wrong bytes arrived");
    close(in_fd);
}

HX_TEST("6.5", "send_file_all honours the offset") {
    std::string content = tu::pattern(100 * 1024, 63);
    tu::TempFile f(content);
    int in_fd = open(f.path.c_str(), O_RDONLY);
    CHECK_SYS(in_fd);

    int sp[2];
    CHECK_SYS(socketpair(AF_UNIX, SOCK_STREAM, 0, sp));
    tu::Drainer d = tu::Drainer::start(sp[1], sp[0]);

    const off_t off = 1000;
    ssize_t n = hx::m6::send_file_all(sp[0], in_fd, off, content.size() - off);
    close(sp[0]);

    uint64_t bytes = 0, hash = 0;
    CHECK_MSG(d.finish(&bytes, &hash), "reader child did not report back");
    CHECK_EQ((long long)n, (long long)(content.size() - off));
    CHECK_EQ((long long)bytes, (long long)(content.size() - off));
    CHECK_MSG(hash == tu::fnv1a(content.substr(off)), "the offset was not applied correctly");
    close(in_fd);
}
