# Module 1 — File descriptors and I/O discipline

**Goal:** never lose a byte and never leak a descriptor. Every function here is
one you will call from the server later, so bugs you leave now will resurface as
"the 1 MiB file is truncated" in module 4.

---

## Theory recap

### `read` and `write` are allowed to do less than you asked

This is the single most misunderstood part of the POSIX I/O interface.

```c
ssize_t n = write(fd, buf, 1000000);
```

Possible outcomes:

| return | meaning |
| --- | --- |
| `1000000` | everything went into the kernel's buffer |
| `1..999999` | a **short write**. The rest is still yours to send. Not an error. |
| `0` | nothing was written (only meaningful if you asked for 0 bytes) |
| `-1`, `errno == EINTR` | a signal arrived before anything was written. Retry. |
| `-1`, `errno == EAGAIN` | the fd is non-blocking and the buffer is full (module 5) |
| `-1`, other | a real error |

`read()` mirrors this:

| return | meaning |
| --- | --- |
| `n > 0` | you got `n` bytes, possibly fewer than you asked for |
| `0` | **end of file** — the writer closed, or the peer sent FIN |
| `-1`, `EINTR` | retry |
| `-1`, `EAGAIN` | non-blocking, nothing available right now |

A short read is *not* EOF. Confusing the two is how HTTP parsers end up
truncating request bodies. Only `read() == 0` means "no more data, ever".

Why do short transfers happen at all? Because the kernel copies into a
fixed-size buffer: a pipe holds 64 KiB by default, a socket's send buffer is
whatever `/proc/sys/net/ipv4/tcp_wmem` says. When that fills, a blocking fd
either sleeps or — if some bytes already moved — returns what it managed.

### `EINTR`

If a signal is delivered while you are blocked in a syscall, and the handler was
installed without `SA_RESTART`, the syscall returns `-1` with `errno == EINTR`.
Nothing was lost; you just have to ask again. Every blocking call in a serious
program is wrapped in a retry loop. That is why your `write_all` is tested with
a child process poking it with `SIGUSR1` mid-transfer.

Note that `close()` is the awkward exception: on Linux, if `close()` returns
`EINTR`, the descriptor **has already been closed**, so retrying is a bug (you
would be closing someone else's fd). Do not loop on `close`.

### Vectored I/O

```c
struct iovec iov[3] = {
    { status_line, 17 },
    { headers,     42 },
    { body,      1024 },
};
writev(fd, iov, 3);
```

One syscall, three buffers, no concatenation into a temporary. An HTTP response
is naturally three pieces, so this saves both a copy and two syscalls per
response. `writev` can be short exactly like `write`, and then you must work out
which iovec you stopped in the middle of — that is the interesting part of 1.4,
and the reason the test uses 40 iovecs against a 64 KiB pipe.

### Do not trust `st_size`

```c
struct stat st;
fstat(fd, &st);
buf = malloc(st.st_size);     // tempting, and wrong in two ways
```

1. Files under `/proc` and `/sys` report size 0 and still produce data.
2. The file can change size between the `fstat` and the `read` — a classic TOCTOU
   bug. Under load, "the file grew" means you truncate; "the file shrank" means
   you serve uninitialised memory.

Read in a loop until EOF and let the string grow. (In module 6 you will bypass
userspace buffers altogether with `sendfile`, which is both faster and immune to
this.)

### Descriptor leaks

An fd leak is a slow-motion outage: nothing is wrong until `accept()` starts
returning `EMFILE` and your server stops taking connections. Limits:

```bash
ulimit -n              # soft limit for this shell
cat /proc/sys/fs/file-max
ls -l /proc/self/fd    # what is open right now
```

The discipline that actually works in C++: put the descriptor in a type whose
destructor closes it (an `Fd` RAII class is ~15 lines). You are writing this
module in a C style on purpose, so you feel why RAII exists — but nothing stops
you writing that class and using it from module 4 onwards. That is a good idea.

---

## Exercises

### 1.0 — `syscall()`, under the wrapper

```c
long n = syscall(SYS_write, fd, buf, n);
```

`man 2 syscall`. Then diff your understanding: the glibc `write()` does the
`errno` translation, `syscall()` does it too (it is also a libc function), but
now *you* wrote the argument marshalling.

### 1.1 — `write_all`

Loop until every byte is gone; retry `EINTR`; return `-1` on a real error. The
hard test drives 512 KiB through a 64 KiB pipe with a slow reader and a signal
storm, and hashes what arrived. If your offset arithmetic is wrong, the hash
tells you, not the length.

### 1.2 — `read_full`

Loop until `n` bytes have arrived or `read` returns 0. Return the count. Note
the asymmetry with `write_all`: a short result here is a legitimate outcome
(EOF), not an error.

### 1.3 — `read_file`

`open` / loop / `close`, growing a `std::string`. Four tests: a 70 KB file, an
empty file, `/proc/version` (size 0, non-empty), and a missing file. The last one
also checks `errno == ENOENT` survived your cleanup.

### 1.4 — `writev_all`

Handle a short `writev` by advancing through the iovec array: skip fully written
entries, and adjust `iov_base` / `iov_len` of the one you stopped inside.

```
before:  iov[0]=[.....](5)  iov[1]=[.......](7)  iov[2]=[...](3)
wrote 9 bytes
after:   iov[0]=[](0)        iov[1]=[...](3)      iov[2]=[...](3)
                              ^ base advanced by 4, len reduced by 4
```

### 1.5 — Prove there is no leak

Implement `count_open_fds()` by counting entries in `/proc/self/fd`. Mind that
the directory handle you open to read the directory is itself an open fd; the
test only compares two counts taken the same way, so just be consistent.

Then the payoff test calls `read_file` 400 times (half of them failures) and
checks the count did not move.

---

## Common mistakes

| symptom | cause |
| --- | --- |
| Hash mismatch in 1.1 / 1.4 | you advanced the pointer by the requested length instead of by the returned length |
| 1.1 times out | you looped on `EAGAIN`, or treated a `0` return as "keep trying" |
| 1.3 fails only on `/proc/version` | you used `st_size` |
| 1.5 fails after 1.3 passes | an error path returns before `close()` |
| Everything passes but `make SAN=1 test` fails | you wrote past the end of a buffer; ASan is right and you are wrong |

**Next:** [`02-sockets.md`](02-sockets.md)
