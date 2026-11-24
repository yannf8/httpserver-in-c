# Hints

Staged on purpose. Read hint 1, go back to the code, and only come back for hint
2 if you are still stuck. There is no solution file: the tests are the
specification, and the struggle before the hint is where the learning is.

Before any hint, try these in order:

1. Read the failure message. They name the file and line and say what was
   expected — e.g. `got 0, want 13330` for a byte-swap.
2. Read the contract comment above the function in `include/hx/modN.hpp`.
   The tests check it literally.
3. `man 2 <the_syscall>`, specifically RETURN VALUE and ERRORS.
4. `strace -f ./build/unit_tests --section X.Y` — see what the kernel was
   actually asked.
5. `make gdb SECTION=X.Y` for a crash.

---

## Module 0

**0.1** One `write` call. `s.data()` and `s.size()` from the `string_view`. Return
its return value unchanged — the test wants `-1` for `fd = 999`.

**0.2** `open(path, O_RDONLY)`. On `-1`, return `errno` **immediately** — calling
anything else first can overwrite it. On success `close` and return 0.

---

## Module 1

**1.0** `syscall(SYS_write, fd, buf, n)` from `<sys/syscall.h>` and `<unistd.h>`.

**1.1** Shape:

```
off = 0
while off < n:
    w = write(fd, (char*)buf + off, n - off)
    if w < 0 and errno == EINTR: continue
    if w < 0: return -1
    if w == 0: return -1
    off += w
return n
```
*Hint 2:* if the hash test fails, you advanced by `n - off` instead of by `w`.

**1.2** Same shape with `read`, except `r == 0` means EOF: `break`, and return
`off` (not `-1`).

**1.3** `open`, then a `read` loop appending into `std::string`, then `close`.
*Hint 2:* the `/proc/version` test fails only if you sized the buffer from
`fstat`. Do not `fstat`.
*Hint 3:* for the leak test, the error path inside the loop must `close(fd)`
before returning — save `errno` around the `close`.

**1.4** Shape:

```
i = 0, total = 0
while i < iovcnt:
    skip over iov[i] with iov_len == 0
    w = writev(fd, iov + i, iovcnt - i)
    handle EINTR / -1
    total += w
    left = w
    while i < iovcnt and left > 0:
        if left >= iov[i].iov_len: left -= iov[i].iov_len; iov[i].iov_len = 0; i++
        else: iov[i].iov_base = (char*)iov[i].iov_base + left;
              iov[i].iov_len -= left; left = 0
return total
```

**1.5** `opendir("/proc/self/fd")`, count entries skipping `.` and `..`,
`closedir`. Subtract 1 for the directory handle itself if you want the count to
match "fds open before I called this".

---

## Module 2

**2.0** `socket(AF_INET, SOCK_STREAM, 0)`. The third argument is the protocol;
`0` means "the default for this type", which is TCP.

**2.1** `(v << 8) | (v >> 8)` for 16 bits — but watch the type: shifting a
`uint16_t` promotes to `int`, so cast the result back. For 32 bits, four bytes:
`((v & 0xff) << 24) | ((v & 0xff00) << 8) | ((v >> 8) & 0xff00) | (v >> 24)`.

For `fill_sockaddr`: `memset` the whole struct, then `sin_family = AF_INET`,
`sin_port = htons(port)`, and `inet_pton(AF_INET, ip, &out->sin_addr) == 1`.
*Hint 2:* `inet_pton` returns `1` on success, `0` for a malformed address. `!= 0`
is not the right test.

**2.2** `int one = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);`

**2.3** Build the `sockaddr_in` with 2.1, then
`bind(fd, (sockaddr*)&a, sizeof a)`.

**2.4** `listen(fd, backlog)`. For `local_port`, `getsockname` needs
`socklen_t len = sizeof a;` **initialised** — it is an in/out parameter. Then
`ntohs(a.sin_port)`.

**2.5** `accept(listen_fd, (sockaddr*)peer, &len)` with `len = sizeof(*peer)`,
wrapped in a retry loop for `EINTR`. If `peer` is null, pass null and null.

**2.6** `socket`, then `connect`. On failure: `int saved = errno; close(fd);
errno = saved; return -1;` — the `ECONNREFUSED` test checks exactly this.

**2.7** `socket` → `set_reuseaddr` → `bind_tcp` → `listen_tcp`. `SO_REUSEADDR`
must be set **before** `bind`.

**2.8** `recv_until_eof`: `read` into a stack buffer, `append`, stop when `read`
returns 0. `half_close_write`: `shutdown(fd, SHUT_WR)` — `SHUT_RDWR` breaks the
test because the peer must still be able to reply.

**2.9** `signal(SIGPIPE, SIG_IGN)`, and return `-1` if it gives `SIG_ERR`.

---

## Module 3

**3.0** Write your own `lower()`: `c >= 'A' && c <= 'Z' ? c + 32 : c`. Do not use
`::tolower` without casting to `unsigned char` first (negative `char` is UB).
`trim_ows` returns a `substr` of the input — no allocation.

**3.1** Find the first space, then the next; reject a third. Reject empty tokens
and any byte `< 0x20` or `== 0x7f`. Do **not** validate the method name.

**3.2** Split on `\r\n`; for each line: reject if it starts with space/tab
(obs-fold), find `:`, reject if it is at position 0, reject if the character
before it is space/tab, `trim_ows` the value, keep the name's case.

**3.3** *Hint 1:* the head ends at `\r\n\r\n`. If it is not there, the only thing
you may do is check the total-size limit and return `Incomplete`.
*Hint 2:* if `head_end` is the index of `\r\n\r\n`, then the request line is
`[0, first \r\n)`, the header block is everything from after that up to and
**including** `head_end + 2` (so each header line still ends with its CRLF), and
the body starts at `head_end + 4`.
*Hint 3:* order the checks: size limits → request line → version → headers →
Content-Length → body. Each has its own status code.

**3.4** `parse_content_length`: digits only, and check overflow before
multiplying (`if (n > (SIZE_MAX - d) / 10) return nullopt;`). In `parse_request`,
scan **all** the headers for `Content-Length`: two with different values is 400.

**3.5** `build_response` is pure string concatenation — no header added by you.
For `http_date`, `gmtime_r` then format by hand with two `static const char*`
arrays for day and month names, so no locale can touch it.

**3.6** Default from the version, then walk the comma-separated `Connection`
tokens: `close` wins outright, `keep-alive` turns it on for HTTP/1.0. Compare
whole trimmed tokens with `iequals`.

**3.7** Order: cut at `?` → require leading `/` → percent-decode (reject `%zz`,
a truncated `%2`, and a decoded NUL) → split on `/` into segments → drop `.` →
`..` pops, and **if there is nothing to pop, reject** → rejoin under the docroot →
append `index.html` if the path ended at a directory.
*Hint 2:* track "did this end at a directory" as you go: it is true if the last
segment was `.`, `..`, or empty (trailing slash), and if no segments survived.

**3.8** Loop: find `\r\n` (else `Incomplete`), take everything before `;` as hex
(else `Error`), if the size is 0 break out; else check the buffer holds
`size + 2` more bytes, verify those two are `\r\n`, append, advance. After the
0-sized chunk, consume trailer lines until you hit an empty one.

---

## Module 4

**4.0** *Hint 1:* get a hard-coded response out first — `listen_on`, `accept`,
read once, ignore what you read, `write_all` a fixed
`"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nhi\n"`, `close`. Watch it in
`curl -v`. Then make it real.
*Hint 2:* the per-connection loop is in `docs/04-blocking-server.md`. Parse
*before* you read, or pipelining hangs.

**4.1** `resolve_path` → `read_file` → 200 with `Content-Length`,
`Content-Type` (`mime_type` on the resolved path) and `Date`. Failure to read is
a 404, not a 500 — you cannot distinguish "missing" from "no permission" without
`stat`, and 404 is the safer answer.

**4.2** Give errors a body so the failure is visible in a browser, and always a
`Content-Length`.

**4.4** After `write_all`, `buf.erase(0, r.consumed)` and loop. Only `read` when
the parser says `Incomplete`. Send `Connection: close` in the response whenever
you are about to close.

**4.5** Compute the response exactly as for `GET`, then send the head only. The
`Content-Length` still describes the body a `GET` would have returned.

**4.6** `SO_RCVTIMEO` is the two-line version:
```c
struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
```
Then a `read` that times out returns `-1` with `EAGAIN`. If you have a partial
request buffered, reply 408; otherwise just close.

---

## Module 5

**5.0** `F_GETFL`, then `F_SETFL` with `flags | O_NONBLOCK`. Assigning instead of
OR-ing is the bug the `O_APPEND` test catches.

**5.1** `epoll_create1(EPOLL_CLOEXEC)`. `memset` the `epoll_event` before use.
`epoll_ctl(ep, EPOLL_CTL_ADD|MOD|DEL, fd, &ev)`.

**5.2** Loop while `!b->done()`: `write(fd, b->data.data() + b->off,
b->remaining())`. `n > 0` → `b->off += n`. `EINTR` → retry. `EAGAIN`/`EWOULDBLOCK`
→ `return 0` (there is `hx::is_would_block` for this). Anything else → `-1`.
Return `1` when the loop finishes.

**5.3** *Hint 1:* start from the loop skeleton in `docs/05-concurrency.md`.
*Hint 2:* keep a `std::unordered_map<int, Conn*>` and look the fd up on **every**
event — that alone avoids the use-after-free, because a dropped connection is
simply absent from the map.
*Hint 3:* the exit condition for the accept loop is `accept` returning `-1`. Do
not distinguish `EAGAIN` from real errors on the first pass; just stop.

**5.5** Give `epoll_wait` a finite timeout (e.g. 100 ms) so the idle sweep runs
even when nothing happens. Track `last_active` per connection and drop the ones
past the deadline.

**5.6** After a `Complete` parse, `erase(0, consumed)` and immediately try to
parse again — in a loop, appending each response to `conn->out`. Only then flush.

---

## Module 6

**6.0** `clock_gettime(CLOCK_MONOTONIC, &ts)`, then
`ts.tv_sec * 1000000000ull + ts.tv_nsec`. The `ull` matters: `tv_sec * 1000000000`
in `int` arithmetic overflows.

**6.1** Copy the samples, `std::sort`, then
`idx = ceil(p * n - 1e-9)` clamped to `[1, n]`, and return `sorted[idx - 1]`.
Empty → 0 everywhere. *Hint 2:* if `percentile(0.9)` on 1..1000 gives you 901,
you left out the `1e-9` — see the note in `include/hx/mod6.hpp`.

**6.2** `setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one)` — needs
`<netinet/tcp.h>`.

**6.3** `cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu, &set);
sched_setaffinity(0, sizeof set, &set);` — `0` means the calling thread.

**6.4** Connect your sockets, then per request: `t0 = now_ns()`, write the
request, read until you have a complete response (reuse your own parser, or just
look for `\r\n\r\n` plus `Content-Length` bytes), `h.add(now_ns() - t0)`. Print
the five contract lines. Keep the read buffer **per connection** across requests
or you will lose pipelined bytes.

**6.5** `sendfile(out_fd, in_fd, &offset, count - sent)` in a loop. Passing
`&offset` means the kernel advances it for you, so do not also add to it
yourself.

---

## Module 7

**7.0** If it crashes, the culprit is almost always an unchecked subtraction on a
`size_t` (`len - 4` when `len == 2` is about 18 quintillion) or a `substr` with a
position past the end. If it hangs, a loop index is not advancing for some
malformed input.

**7.1** These are the same code paths as 3.3/3.4 — if 3.3 and 3.4 are green and
7.1 is not, the missing case is the *ordering*: reject on the header value before
you wait for or allocate the body.

**7.2** You need the idle sweep from 5.5, and `epoll_wait` must have a finite
timeout so the sweep runs while the attacker is silent.

**7.3** `sigaction` with `sa_handler` setting a `volatile sig_atomic_t` and
`sa_flags = 0` (no `SA_RESTART`, so the blocked call returns `EINTR`). Check the
flag in the loop condition and `return 0`.

---

## Asking for help well

If you ask someone for help, a good question includes:

1. the section id (`2.3`),
2. the exact failure output,
3. the code you wrote,
4. what you already tried and what you expected.

Try: *"Section 3.3, this test fails: `<paste>`. Here is my `parse_request`:
`<paste>`. I think `consumed` is right because ... — explain what I have
misunderstood about the header block boundary, but do not write the function for
me."*

Asking for an explanation rather than a patch is the difference between finishing
this project and having finished this project.
