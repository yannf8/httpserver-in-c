// Module 6 — measurement and latency.  Theory: docs/06-low-latency.md
//
// Run:  make test MODULE=6

#include "hx/mod6.hpp"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sched.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <time.h>

#include <algorithm>

namespace hx::m6 {

// 6.0 — a clock you can trust
uint64_t now_ns() {
    // TODO(6.0): clock_gettime(CLOCK_MONOTONIC, &ts) then ts.tv_sec * 1e9 +
    // ts.tv_nsec. Use integer arithmetic, and mind the 64-bit multiply.
    return 0;
}

// 6.1 — percentiles, not averages
void Hist::add(uint64_t ns) {
    // TODO(6.1)
    (void)ns;
}

size_t Hist::count() const {
    // TODO(6.1)
    return 0;
}

uint64_t Hist::percentile(double p) const {
    // TODO(6.1): nearest-rank, see the contract in include/hx/mod6.hpp.
    // Sorting a copy is fine for now; think about what a hot path would do.
    (void)p;
    return 0;
}

uint64_t Hist::max() const {
    // TODO(6.1)
    return 0;
}

uint64_t Hist::min() const {
    // TODO(6.1)
    return 0;
}

double Hist::mean() const {
    // TODO(6.1)
    return 0;
}

void Hist::reset() {
    // TODO(6.1)
}

// 6.2 — Nagle
int set_tcp_nodelay(int fd) {
    // TODO(6.2): setsockopt(IPPROTO_TCP, TCP_NODELAY).
    (void)fd;
    return kUnimplemented;
}

// 6.3 — affinity
int pin_to_cpu(int cpu) {
    // TODO(6.3): CPU_ZERO / CPU_SET into a cpu_set_t, then
    // sched_setaffinity(0, sizeof(set), &set).
    (void)cpu;
    return kUnimplemented;
}

// 6.5 — sendfile
ssize_t send_file_all(int out_fd, int in_fd, off_t offset, size_t count) {
    // TODO(6.5): loop sendfile() until `count` bytes are sent. It can return
    // short, and on a non-blocking socket it can return EAGAIN.
    (void)out_fd;
    (void)in_fd;
    (void)offset;
    (void)count;
    return kUnimplemented;
}

}  // namespace hx::m6
