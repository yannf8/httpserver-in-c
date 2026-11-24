# Module 7 — Hardening

**Goal:** make the server survive input designed to break it, and shut down
cleanly. Anything reachable from a network is attacked; a parser is the most
exposed code you own.

---

## Theory recap

### Fuzzing

A fuzzer feeds semi-random input and watches for crashes, hangs, and broken
invariants. Your parser is the ideal target: pure, fast, no I/O.

The 7.0 test is a small mutation fuzzer with a **fixed seed** — deterministic on
purpose, because a crash you cannot reproduce is not a finding. It starts from a
valid request and applies byte flips, deletions, insertions, truncations and stray
CRLFs, then asserts the invariants that must hold for *every* input:

* it never crashes (the runner reports `CRASH: killed by signal 11` if it does),
* it never hangs (the runner's `alarm()` reports `TIMEOUT`),
* `Complete` implies `0 < consumed <= input.size()` — a `consumed` past the end is
  a read past the end,
* `Error` always carries a usable status code.

To go further, use a real coverage-guided fuzzer — the parser's shape is already
right for it:

```bash
# libFuzzer (clang), ~15 lines of harness
clang++ -std=c++20 -g -O1 -fsanitize=fuzzer,address,undefined \
        -Iinclude src/mod3.cpp fuzz/parse_fuzz.cpp -o fuzz_parse
./fuzz_parse -max_total_time=60 corpus/
```

with

```cpp
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n) {
    hx::m3::Request r;
    hx::m3::parse_request(std::string_view((const char*)d, n), &r);
    return 0;
}
```

Coverage guidance finds in seconds what random mutation needs hours for. This is
optional here but it is genuinely how protocol code is validated in industry.

### Limits and resource exhaustion

Every unbounded thing is a denial of service:

| unbounded thing | attack | bound it with |
| --- | --- | --- |
| request line | 20 KB URI | 8 KiB → 414 |
| header block | 2000 headers | 16 KiB → 431 |
| body | `Content-Length: 4294967296` | 1 MiB → 413, refused **before** allocating |
| head that never ends | dribble bytes forever | limit checked even when `\r\n\r\n` is absent → 431 |
| connections | one per socket the attacker owns | max-connections cap, idle timeouts |
| time per connection | slowloris | read/idle timeout |
| output queue | slow reader, huge responses | cap `conn->out`, apply backpressure |

Note the order in the body row: you must reject a 4 GiB `Content-Length` from the
*header*, not after trying to buffer it.

### Slowloris

Open hundreds of connections, send `GET / HTTP/1.1\r\nX-a: ` on each, and never
finish. Each connection is nearly free for the attacker and consumes a slot on
your side. A thread-per-connection server dies at a few hundred; an event loop
survives far longer but still needs:

* an idle timeout per connection (you have one from 4.6/5.5),
* a cap on total connections, refusing (or immediately closing) beyond it,
* a cap on header bytes (3.3).

The 7.2 test holds 50 unfinished requests open and requires a real request to
still be served in under 2 s.

### Graceful shutdown

`SIGTERM` is how the world asks a process to stop (systemd, Docker, Kubernetes all
send it, then `SIGKILL` after a grace period). Doing it right:

1. handler sets `volatile sig_atomic_t g_stop = 1` — nothing else,
2. the loop notices (either because `epoll_wait`/`accept` returned `EINTR`, or on
   the next timeout tick),
3. stop accepting, finish in-flight responses, close, `return 0`.

Only async-signal-safe functions are allowed in a handler (`man 7
signal-safety`): setting a flag, or `write()`. No `printf`, no `malloc`, no
locks — the handler can interrupt those halfway.

The tidy modern alternative is `signalfd(2)` or `pidfd`: turn signals into a
readable fd and handle them as just another event in your loop, with no handler
and no async-safety constraints. Worth doing once you have 7.3 passing.

### The security checklist for this server

- [ ] `resolve_path` is the *only* source of filesystem paths (3.7)
- [ ] percent-decode before checking for `..`
- [ ] no `printf(user_string)` — format strings are code
- [ ] no `strcpy`/`sprintf`/`alloca` on network data
- [ ] all lengths are `size_t`, and every subtraction on them is checked for
      wraparound (`len - 4` when `len` is 2 is a very large number)
- [ ] read into a buffer with an explicit capacity, never a fixed `char[256]` from
      a length you were told
- [ ] `SIGPIPE` ignored (2.9)
- [ ] descriptors closed on every path (1.5)
- [ ] the server does not run as root; if it must bind port 80, use
      `CAP_NET_BIND_SERVICE` or a reverse proxy
- [ ] `make SAN=1 test && make SAN=1 integration` is clean

---

## Exercises

| section | what the tests check |
| --- | --- |
| 7.0 | 20 000 deterministic mutations: no crash, no hang, invariants hold; plus every truncation of a valid request is never `Complete` |
| 7.1 | 20 KB URI → 414; 2000 headers → 431; 4 GiB `Content-Length` → 413; bare-LF heads never `Complete`; embedded NUL → 400 |
| 7.2 | 50 slowloris connections, real traffic still served in < 2 s |
| 7.3 | `SIGTERM` → exit code 0 within 3 s |

Note that 7.0 first checks your parser handles a *valid* request — a parser that
rejects everything would otherwise "pass" the fuzz test trivially.

### Going further (unmarked, genuinely worth doing)

1. Write the libFuzzer harness above and let it run for a few minutes.
2. Add a max-connections cap and make the server close the newest connection when
   it is hit. Measure the point at which your event loop degrades.
3. Replace the signal handler with `signalfd` and delete the global flag.
4. Add access logging that cannot block the loop (ring buffer + a writer thread).
5. Run the whole integration suite under `make SAN=1` and fix everything it finds.

---

## Common mistakes

| symptom | cause |
| --- | --- |
| 7.0 `CRASH` | usually `substr`/`compare` past the end after a length subtraction wrapped around |
| 7.0 `TIMEOUT` | a `while` loop whose index does not advance on malformed input |
| 7.1 413 arrives after a long pause | you buffered the body before checking the length |
| 7.2 fails | no idle timeout, or the sweep only runs when an event arrives (give `epoll_wait` a finite timeout) |
| 7.3 hangs | the handler tried to do real work, or nothing checks the flag |
| 7.3 exits 143 | you did not install a handler at all — 143 is 128+15, killed by SIGTERM |

**Next:** [`08-going-further.md`](08-going-further.md) — review questions and where
to go from here.
