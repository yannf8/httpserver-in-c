# Module 6 — Measuring and shaving latency

**Goal:** measure honestly, then make it faster on evidence rather than on
folklore.

---

## Theory recap

### Measure first. Always.

The rule that separates engineers from cargo cultists: **no optimisation without
a measurement before and after.** Every technique below is a real win in some
situation and a pessimisation in others.

### Clocks

```c
struct timespec ts;
clock_gettime(CLOCK_MONOTONIC, &ts);        // never steps, never goes backwards
uint64_t ns = ts.tv_sec * 1000000000ull + ts.tv_nsec;
```

| clock | use it for |
| --- | --- |
| `CLOCK_MONOTONIC` | durations. NTP adjusts its *rate* but never steps it |
| `CLOCK_MONOTONIC_RAW` | durations with no NTP rate correction at all |
| `CLOCK_REALTIME` | wall-clock timestamps (log lines, the `Date` header) — can jump backwards |
| `rdtsc` | sub-20 ns measurements, once you have pinned the thread and confirmed `constant_tsc` |

Never time anything with `CLOCK_REALTIME`: an NTP step mid-measurement produces a
negative duration, and in unsigned arithmetic that becomes a suspiciously large
number in your p99.

`clock_gettime` is fast because it is a **vDSO** call — the kernel maps a page of
code and a shared timekeeping structure into your address space, so it usually
costs ~20 ns and no syscall at all. Confirm with `strace`: you will not see it.

### Percentiles, not averages

```
1000 requests, 999 take 1 µs, one takes 50 ms
mean = 51 µs        <- describes nothing that happened
p50  = 1 µs
p99  = 1 µs
p999 = 1 µs
max  = 50 ms        <- the only number that matters to the client who hit it
```

The mean is a fiction for latency distributions: they are long-tailed, and the
tail is where users, and exchanges, live. Report p50 / p99 / p99.9 / max.

Sample count and percentile precision are linked: you cannot measure a p99.9 with
1000 samples — the answer is literally your single worst sample. Rule of thumb:
you need ≥ 10/(1−p) samples, so 10 000 for a p99.9 and 100 000 for a p99.99.

**Nearest-rank** definition, which the tests use:

```
index = clamp(ceil(p * N - 1e-9), 1, N)     // 1-based, on the sorted samples
```

The `1e-9` is not decoration. `0.9 * 1000` is `900.0000000000000222` in IEEE 754,
so a bare `ceil` hands you the 901st sample. Floating-point percentile bugs are
common and quietly wrong.

### Coordinated omission — the trap in your own bench tool

Your `bench` sends a request, waits for the response, then sends the next. When
the server stalls for 50 ms, that loop *does not send* the ~50 000 requests it
should have sent during the stall. You measure one slow request instead of fifty
thousand, and your p99 looks fine while users are furious. Gil Tene named this
**coordinated omission**, and it invalidates most homegrown load tests.

Fixes, in order of effort:

1. Send on a fixed schedule (a target rate), and measure latency from the time the
   request *should* have been sent, not from when you managed to send it.
2. Use open-loop tools that do this properly (`wrk2`, `hdrhistogram`-based ones).
3. At minimum, report the max and note the limitation. Being able to explain the
   problem is worth more than a fancy harness.

### Nagle and delayed ACK

Nagle's algorithm (RFC 896) buffers a small write while an earlier small segment
is unacknowledged, to avoid flooding the network with tiny packets. Delayed ACK
holds an ACK for up to 40 ms hoping to piggyback it on data. Put them together and
you get the classic **40 ms stall**: your write waits for an ACK that is waiting
for your write.

```c
int one = 1;
setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
```

Every request/response server wants `TCP_NODELAY` on accepted sockets. The real
fix for many small writes is not to make many small writes — build the whole
response and send it once (`writev`, 1.4). Related: `TCP_CORK` does the opposite
on purpose (batch until you uncork), and `TCP_QUICKACK` disables delayed ACK for
one read.

### Fewer copies, fewer syscalls

A naive static-file response copies: disk → page cache → your buffer → socket
buffer → NIC. `sendfile(2)` removes the userspace round trip entirely:

```c
sendfile(out_socket_fd, in_file_fd, &offset, count);
```

Page cache straight to socket, inside the kernel. `splice(2)` generalises it to
pipes; `MSG_ZEROCOPY` can avoid the last copy for large sends (at the cost of
completion notifications, so it only pays above ~10 KB).

And the general principle: **one syscall that does more beats several that do
less.**

| instead of | use |
| --- | --- |
| 3 × `write` (status, headers, body) | one `writev` |
| `read` + `write` for a file | `sendfile` |
| `accept` + `fcntl(O_NONBLOCK)` | `accept4(SOCK_NONBLOCK)` |
| N × `recvmsg` | `recvmmsg` (batched, big win for UDP) |
| `epoll_wait` + N × `read`/`write` | `io_uring` (batch submit, batch complete) |

`io_uring` is the current frontier: submission and completion ring buffers shared
with the kernel, so you can queue dozens of operations and reap them all with one
(or zero) syscalls. It is where a rewrite of this server would go next.

### Jitter: where tail latency actually comes from

Below ~50 µs, your enemy is not throughput but **variance**:

| source | mitigation |
| --- | --- |
| scheduler moving your thread | `sched_setaffinity` (pin), `isolcpus`, `nohz_full` |
| another thread on the sibling hyperthread | leave the sibling idle, or disable SMT |
| NUMA: memory on the far socket | allocate and pin on the same node (`numactl`) |
| page faults / swap | pre-touch memory, `mlockall`, huge pages |
| `malloc` in the hot path | preallocate, arenas, object pools |
| cache misses on shared data | pad to 64 bytes, avoid false sharing |
| CPU frequency scaling / C-states | performance governor, `idle=poll` |
| interrupts on your core | IRQ affinity away from it |
| GC or JIT (not here, but) | why this tier is C++ |

The single biggest lever is usually **pinning plus not sharing anything mutable
between cores.**

```c
cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu, &set);
sched_setaffinity(0, sizeof set, &set);      // 0 = this thread
```

(Do not pass a cpu number ≥ `CPU_SETSIZE` (1024) to `CPU_SET` — it writes past the
end of the set. Ask for a real core.)

### False sharing, concretely

Two counters in the same 64-byte cache line, written by two cores, cost ~100 ns
each because the line ping-pongs between L1s. Padded to separate lines, ~1 ns.
This is why per-connection and per-thread state is grouped per thread, not in one
shared array.

---

## Exercises

| section | what you implement / check |
| --- | --- |
| 6.0 | `now_ns` — monotonic, nanoseconds, sane over a 20 ms sleep |
| 6.1 | `Hist` — nearest-rank percentiles, incl. the `1e-9`, empty-histogram safety, and a distribution where the mean lies |
| 6.2 | `set_tcp_nodelay` (read back with `getsockopt`) |
| 6.3 | `pin_to_cpu` — affinity mask has exactly one CPU, and `sched_getcpu()` agrees |
| 6.4 | `src/bench.cpp` — your own load generator; must print `count`, `p50`, `p99`, `p999`, `max` in nanoseconds |
| 6.5 | `send_file_all` — `sendfile` loop, honouring the offset; plus an end-to-end 1 MiB file check |
| 6.6 | *by hand* — the measurement lab below |

### 6.6 — the measurement lab *(no automated test; the point is the numbers)*

Keep a file `notes/measurements.md` with a before/after table. Suggested runs:

```bash
# baseline
./build/server_epoll --port 8080 --docroot www &
./build/bench --port 8080 --requests 20000 --connections 1 --path /notes.txt

# 1. does TCP_NODELAY on accepted sockets change p99?
# 2. one writev vs three writes for the response?
# 3. sendfile vs read_file for a 1 MiB file?
# 4. pinning the server to core 2 and bench to core 4?
# 5. 1 loop vs 4 loops with SO_REUSEPORT?
```

Tools:

```bash
strace -c -f ./build/server_epoll --port 8080     # syscalls per request — the cheapest insight there is
perf stat -e task-clock,cycles,instructions,cache-misses,context-switches ./build/bench --port 8080 --requests 20000
perf record -g ./build/server_epoll --port 8080   # then: perf report
perf top                                          # live hot symbols
/usr/bin/time -v ./build/bench ...                # max RSS, page faults, context switches
```

For the allocation audit: run the server under `strace -e trace=brk,mmap` and
watch whether serving a request allocates. Then remove the allocations from the
hot path (reuse the connection's buffers instead of building fresh `std::string`s
per response) and measure again. Write down what changed. If nothing changed, that
is also a result — and knowing which optimisations *did not* matter on loopback is
exactly the judgement the job needs.

---

## Common mistakes

| symptom | cause |
| --- | --- |
| p99 of 0 ns | timing the wrong thing, or `now_ns` returning something other than nanoseconds |
| percentiles off by one | missing the `1e-9`, or 0-based indexing |
| Huge p99 that disappears when you look | the first iterations include connection setup and cold caches — warm up, then measure |
| "sendfile made no difference" | on loopback with a page-cached file it often does not; say so, do not pretend |
| Numbers change 30% between runs | not pinned, or the machine is busy; pin, repeat, report the median of medians |

**Next:** [`07-hardening.md`](07-hardening.md)
