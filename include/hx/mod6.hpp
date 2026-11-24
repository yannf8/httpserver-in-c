// Module 6 — Measuring and shaving latency.
// Theory: docs/06-low-latency.md
#pragma once

#include "hx/common.hpp"

namespace hx::m6 {

// 6.0 — Monotonic nanosecond timestamp from clock_gettime(CLOCK_MONOTONIC).
// Never goes backwards, unaffected by NTP steps or the wall clock.
uint64_t now_ns();

// 6.1 — A latency histogram. You decide the storage; the contract is:
//  * percentile(p), p in [0,1], uses *nearest-rank* on the sorted samples:
//      index = clamp(ceil(p * N - 1e-9), 1, N)   (1-based)
//    so percentile(0) is the minimum and percentile(1) is the maximum.
//    The 1e-9 is not decoration: 0.9 * 1000 is 900.0000000000000222 in IEEE
//    754, and a bare ceil() would hand you the 901st sample. Getting bitten by
//    this in a latency report is a rite of passage; getting bitten by it in
//    production is a bug report.
//  * percentile() on an empty histogram returns 0.
class Hist {
public:
    void add(uint64_t ns);
    size_t count() const;
    uint64_t percentile(double p) const;
    uint64_t max() const;
    uint64_t min() const;
    double mean() const;  // 0 when empty
    void reset();

private:
    std::vector<uint64_t> samples_;  // fine to start here; see docs/06 for why
                                     // a real hot path would not do this
};

// 6.2 — Disable Nagle's algorithm so a small write goes out immediately
// instead of waiting for an ACK or more data. Returns 0 or -1.
int set_tcp_nodelay(int fd);

// 6.3 — Pin the calling thread to one CPU with sched_setaffinity(2).
// Returns 0 or -1.
int pin_to_cpu(int cpu);

// 6.5 — Copy `count` bytes from `in_fd` at `offset` to the socket `out_fd`
// using sendfile(2), looping until done. No userspace buffer is involved.
// Returns bytes sent, or -1.
ssize_t send_file_all(int out_fd, int in_fd, off_t offset, size_t count);

}  // namespace hx::m6
