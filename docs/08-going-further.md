# Module 8 — Going further

You have built an HTTP/1.1 server out of raw syscalls: an incremental parser,
a blocking server, an `epoll` event loop, honest latency measurement, and the
hardening a network-facing program needs. This page is for checking that the
ideas stuck, and for deciding what to build next.

The skills transfer well beyond HTTP. Any program that reads a byte stream off
a socket has the same problems: frames split arbitrarily across reads, strict
validation of untrusted input, backpressure when the peer is slow, and tail
latency you can only fix once you can measure it.

---

## Questions you should now be able to answer

**Syscalls and I/O**
1. What happens mechanically when you call `write()`?
2. Why can `write()` return less than you asked for? What do you do about it?
3. What is `EINTR` and when do you see it? Why must you not retry `close()`?
4. `read()` returns 0 — what does that mean, and what does it *not* mean?
5. Why is `writev` better than three `write`s? How much better, roughly?
6. What does `sendfile` avoid, exactly?

**Sockets and TCP**
7. Walk through `socket`/`bind`/`listen`/`accept`. What does `accept` return and why?
8. What does the `backlog` argument bound? What happens when it overflows?
9. What is `TIME_WAIT` for, which side has it, and why does `SO_REUSEADDR` exist?
10. `SO_REUSEADDR` vs `SO_REUSEPORT`?
11. What is Nagle's algorithm, what is delayed ACK, and what happens when they meet?
12. What is a half-close and when would you use one?
13. Why does writing to a closed socket kill your process by default?

**Concurrency**
14. Why `epoll` rather than `select` or `poll`? Give the complexity argument.
15. Level-triggered vs edge-triggered — what breaks if you use `EPOLLET` naively?
16. Where does an event loop keep the state that a thread would keep on its stack?
17. How do you handle a response that does not fit in the socket buffer?
18. Thread-per-connection vs event loop vs one loop per core — when is each right?
19. What is the use-after-free that everyone hits in an epoll loop?

**Latency**
20. Which clock do you time with, and why not the other ones?
21. Why report percentiles instead of a mean?
22. How many samples do you need for a trustworthy p99.9?
23. What is coordinated omission?
24. Your p99 is 40× your p50 on an idle machine. Name five possible causes.
25. What is false sharing and how do you find it?
26. How would you reduce the syscall count of this server further? (`io_uring`,
    batching, kernel bypass.)

**Protocol and safety**
27. Why parse incrementally rather than "read the whole request"?
28. What is request smuggling and how does a parser invite it?
29. How do you make sure a path from the network cannot escape the docroot?
30. Which limits does a network-facing parser need, and what does each prevent?

If a question here makes you uncomfortable, that is the next thing to read — the
module docs each end with the same material from the implementation side.

## Where to go after this

In rough order of value:

1. **`io_uring`**: rewrite the event loop on submission/completion rings. This is
   the current state of the art in the kernel.
2. **UDP multicast + `recvmmsg`**: build a tiny multicast receiver. The same
   `socket`/`bind` calls as module 2, plus `IP_ADD_MEMBERSHIP` and no connection.
3. **A lock-free SPSC ring buffer** between two pinned threads, measured with
   `Hist`. Then read about the cache-line reasoning that makes it fast.
4. **`perf` seriously**: flame graphs, `perf stat` counters, and
   `perf sched` for scheduler jitter.
5. **A binary protocol codec** (fixed-width, length-prefixed) with the same test
   discipline you used in module 3 — it is a genuinely small project once the
   framing loop is second nature.
6. **Kernel bypass**, conceptually: what Onload/DPDK do and why they help.
7. Read the source of something real: `redis`'s `ae.c` event loop and `picohttpparser`
   are both small enough to read in an evening and are written by people who cared
   about exactly what you just cared about.

## Books and references worth the money

* Stevens, *UNIX Network Programming, Vol 1* — still the reference for everything
  in modules 2 and 5.
* Kerrisk, *The Linux Programming Interface* — the best single book on the syscall
  interface. Chapters 4–5, 44, 56–61, 63.
* Drepper, *What Every Programmer Should Know About Memory* — for module 6's
  cache reasoning.
* RFC 9110 / 9112 — HTTP semantics and HTTP/1.1 syntax. Short, readable, and the
  authority for every rule in module 3.
* Gil Tene's *How NOT to Measure Latency* talk — one hour, and it will change how
  you report numbers.
