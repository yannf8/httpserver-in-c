# Module 5 — Concurrency: non-blocking I/O and epoll

**Goal:** one thread, thousands of connections, no client able to stall another.

---

## Theory recap

### Four ways to serve more than one client

| model | cost per connection | when it is right |
| --- | --- | --- |
| process per connection (`fork`) | ~1 MB + page tables | isolation matters more than speed (CGI, `sshd`) |
| thread per connection | 8 MB virtual / ~8–64 KB real stack, plus a scheduler entry | modest concurrency, blocking code you cannot rewrite |
| **event loop** (`epoll`) | a few hundred bytes | many mostly-idle connections; this is what nginx, redis, node do |
| thread pool of event loops | one loop per core | all of the above, on many cores (5.7) |

The historical name for the problem is **C10K**: at 10 000 connections, a thread
each means 10 000 stacks and a scheduler queue that thrashes. An event loop keeps
10 000 connections in a few megabytes because an idle connection is just a struct.

### Non-blocking is not optional

An event loop must never block. Anywhere it blocks, every other connection waits.
So every fd is `O_NONBLOCK`:

```c
int fl = fcntl(fd, F_GETFL);          // read the flags
fcntl(fd, F_SETFL, fl | O_NONBLOCK);  // OR in the bit — do not overwrite
```

and every operation gains a new possible answer:

```
read()  ->  n>0 data | 0 EOF | -1 EAGAIN "nothing right now" | -1 real error
write() ->  n>0 partial or full | -1 EAGAIN "send buffer full" | -1 real error
```

`EAGAIN` is not an error. It means "come back when the kernel tells you". That is
the whole contract of an event loop.

### `epoll`

```c
int ep = epoll_create1(EPOLL_CLOEXEC);

struct epoll_event ev = { .events = EPOLLIN, .data.fd = fd };
epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev);      // also MOD and DEL

struct epoll_event out[256];
int n = epoll_wait(ep, out, 256, timeout_ms);   // -1 = forever, 0 = poll
```

Why not `select`/`poll`? Both take the entire fd set on **every** call, so the
kernel rescans everything: O(n) per wait, with `select` additionally capped at
`FD_SETSIZE` (1024). `epoll` registers interest once and hands back only what is
ready: O(ready). At 10 000 connections with 10 active, that is a thousandfold
difference in kernel work per iteration.

`data` is a union — `fd`, or `ptr`. Put your `Conn*` in `data.ptr` and you get
the connection state back with no lookup. (`data.fd` and a hash map is easier to
debug; both are fine, and the tests only check `data.fd` in the 5.1 unit test.)

`EPOLL_CLOEXEC` closes the epoll fd automatically across `exec()`, so a child
process cannot inherit it. Make it a habit: `SOCK_CLOEXEC`, `O_CLOEXEC`,
`accept4(..., SOCK_CLOEXEC)`.

### Level-triggered vs edge-triggered

| | level-triggered (default) | edge-triggered (`EPOLLET`) |
| --- | --- | --- |
| reports | "this fd **is** ready" | "this fd **became** ready" |
| if you read only half the data | you are told again next wait | you are **not** told again — you hang |
| requirement | none | you must drain until `EAGAIN` |
| syscalls | slightly more | fewer |

Start level-triggered. It is forgiving and correct. Switch to edge-triggered once
your read and write loops genuinely drain to `EAGAIN`, and understand that with
`EPOLLET` the listening socket must be `accept()`ed in a loop too — otherwise a
burst of connections arrives, you accept one, and the rest sit unnoticed.

### Partial writes are now your problem

In module 4, `write_all` looped until finished. It could, because it was allowed
to block. In an event loop it is not:

```
flush_out(fd, &conn->out):
    1  -> fully written; disarm EPOLLOUT (or close, if we were closing)
    0  -> would block; the kernel's send buffer is full
          leave the rest in conn->out, arm EPOLLIN|EPOLLOUT, and move on
   -1  -> tear the connection down
```

The response therefore has to outlive the function that produced it — it lives in
the connection struct with an offset. That offset is the entire reason an event
loop needs per-connection state. When `EPOLLOUT` fires, you flush from where you
stopped.

This is also the answer to "why is my 2 MiB response truncated": the first
`write` took 200 KB, returned, and nobody ever sent the rest.

### The loop, in full

```
for (;;) {
    n = epoll_wait(ep, evs, MAX, next_timeout_ms);
    for each ready event:
        listening fd:  accept() in a loop until EAGAIN
                       set O_NONBLOCK (or accept4), TCP_NODELAY
                       create Conn, epoll_add(EPOLLIN)
        EPOLLIN:       read until EAGAIN into conn->in
                       parse as many complete requests as are buffered
                       append responses to conn->out
        then:          flush_out; if it returns 0, arm EPOLLOUT
        EPOLLOUT:      flush_out; when done, disarm (or close if closing)
        EPOLLHUP|ERR:  tear down
    sweep connections idle longer than the timeout
}
```

### The bug you are going to hit

Deleting a connection while still iterating the `epoll_wait` result array. The
same fd can appear more than once in `evs[]` (e.g. `EPOLLIN|EPOLLHUP`), and after
you `close()` and `delete`, the second entry dereferences freed memory. Worse, the
kernel may hand the *number* to a new connection immediately.

Pick a rule before you hit this:

* look the fd up in your map on every event and skip if absent (simple, safe), or
* mark-and-sweep: set `conn->dead = true`, delete after the loop.

`make SAN=1 all` plus AddressSanitizer will catch it if you get it wrong. Run the
integration tests that way at least once.

---

## Exercises

`src/mod5.cpp` for the primitives, `src/server_epoll.cpp` for the loop. Same CLI
contract as module 4:

```
./build/server_epoll --port N [--docroot DIR] [--timeout-ms MS]
```

| section | what the tests check |
| --- | --- |
| 5.0 | `set_nonblocking` sets the bit **and preserves `O_APPEND`**; an empty non-blocking pipe gives `EAGAIN` |
| 5.1 | `epoll_create1` with `CLOEXEC`; `ADD` reports `EPOLLIN` with the right `data.fd`; `MOD`/`DEL` change interest; a second `DEL` fails with `ENOENT` |
| 5.2 | `flush_out` returns 1 / 0 / -1 correctly and resumes an 8 MiB transfer at the exact byte it stopped on |
| 5.3 | the loop serves `GET /` |
| 5.4 | 64 simultaneous connections all get their response |
| 5.5 | a silent client does not delay a healthy one (< 2 s); a 2 MiB response to a deliberately slow reader arrives complete |
| 5.6 | three pipelined requests in one `write` get three responses in order |
| 5.7 | `set_reuseport` |

Reuse `handle_request` from module 4 unchanged — that is the payoff for keeping
module 3 free of I/O.

### 5.7 — one loop per core *(the interesting extension)*

The classic mistake is one thread that `accept()`s and hands fds to workers: the
acceptor becomes the bottleneck and the handoff costs a cache-line bounce. Better:

* N threads, each with **its own** listening socket on the same port via
  `SO_REUSEPORT`, its own `epoll`, its own connections. The kernel hashes the
  4-tuple to a socket, so no thundering herd and no shared state at all.
* Pin each thread to a core (`hx::m6::pin_to_cpu`).

Only the unit test for `set_reuseport` is automated; building the multi-loop
server is an exercise whose payoff you measure with your own `bench` (6.4). Do
that: measure single-loop first, then N loops, and see whether it actually helps
on loopback. (It often does not — loopback is not the bottleneck. Knowing *that*
is worth more than the change.)

---

## Common mistakes

| symptom | cause |
| --- | --- |
| Large responses truncate | ignoring `flush_out`'s 0 return, or not arming `EPOLLOUT` |
| Server spins at 100% CPU | armed `EPOLLOUT` with nothing to write, so it fires forever — disarm it when done |
| First client works, later ones hang | you did not `accept()` in a loop, or set `EPOLLET` without draining |
| Random crashes under 5.4 | freeing a connection mid-iteration |
| Everything works but 5.5 fails | something in your handler still blocks — a blocking `read_file` on a slow disk, or a `write_all` loop left over from module 4 |
| `EPOLLHUP` storms | you closed the fd but never `EPOLL_CTL_DEL`'d it (closing does remove it, but not if you dup'd it) |

**Next:** [`06-low-latency.md`](06-low-latency.md)
