# Module 0 — Setup and the syscall interface

**Goal:** get the build/test loop working, and understand what a syscall actually
is before you make a hundred of them.

---

## Theory recap

### What a syscall is

Your process runs in *user mode*. It cannot touch the network card, the disk, or
another process's memory. The kernel runs in *kernel mode* and can. A syscall is
the controlled doorway between the two.

On x86-64 Linux, calling `write(2)` ends up as:

```
mov  $1,  %rax      ; syscall number: 1 = write
mov  $1,  %rdi      ; arg1: fd = 1 (stdout)
mov  buf, %rsi      ; arg2: pointer to the bytes
mov  $13, %rdx      ; arg3: how many bytes
syscall             ; trap into the kernel
                    ; return value comes back in %rax
```

The `syscall` instruction switches privilege level, saves your registers, and
jumps to the kernel's handler. On return you are back in user mode with a value
in `%rax`. That transition is not free — it is roughly 50–200 ns on a modern CPU
once you count the pipeline effects, and much more if a mitigation like
KPTI/Spectre-v2 is active. Module 6 makes you measure it. **The reason
high-performance servers look the way they do is almost entirely "make fewer
syscalls, and do more work per syscall".**

### The libc wrapper vs the raw syscall

`write()` in your C program is not the syscall. It is a small glibc function
that:

1. moves your arguments into the right registers,
2. executes `syscall`,
3. if the return value is in `[-4095, -1]`, negates it, stores it in the
   thread-local `errno`, and returns `-1`,
4. otherwise returns the value unchanged.

That is the whole story. `errno` is a userspace invention. The kernel just
returns `-ENOENT`. You will do this by hand in section 1.0.

### Return values and `errno`, the rules

* Check *every* return value. Every one.
* `errno` is only meaningful after a call that actually failed. It is never
  cleared on success, so reading it after a successful call gives you garbage
  from some earlier failure.
* Anything you do between the failing call and reading `errno` may clobber it —
  `close()`, `printf()`, even a destructor. Save it in a local if you need to
  clean up first:

  ```c
  int saved = errno;
  close(fd);
  errno = saved;
  ```
* `errno` is thread-local, so it is safe with threads but useless across
  processes.

### Descriptors

A file descriptor is a small non-negative integer indexing a per-process table.
Entry 0 is stdin, 1 is stdout, 2 is stderr. Everything is an fd: files, pipes,
sockets, epoll instances, timers, signals, even other processes (`pidfd`). This
uniformity is why `read()` and `write()` work on sockets, and why the same
`epoll` loop can watch a socket, a timer, and a signal at once.

`open()` always returns the **lowest** free descriptor number. That fact is what
section 0.2's leak test exploits: if the number you get back drifts upwards, you
leaked.

---

## Your tools

```bash
man 2 write            # section 2 = syscalls, section 3 = library functions
man 2 syscalls         # the full list
strace -f ./build/server_blocking --port 8080     # every syscall, live
strace -c -f ./build/unit_tests --module 1        # a syscall count summary
ltrace ./prog          # library calls instead
ls -l /proc/self/fd    # this process's open descriptors
ss -ltnp               # who is listening on which TCP port
cat /proc/sys/net/ipv4/ip_local_port_range        # the ephemeral port range
```

`strace` is the single most useful tool in this whole project. When a test fails
and you cannot see why, run the thing under `strace` and read what the kernel was
actually asked to do. It is usually not what you thought.

---

## Exercises

### 0.0 — Toolchain and harness self-check *(free)*

```bash
make            # builds build/unit_tests and the three server binaries
make test MODULE=0
```

You should see `0.0` green and everything else red. Red is the starting state;
your job is to walk the ladder upwards.

Useful commands, all documented in the `Makefile` header:

| command | what it does |
| --- | --- |
| `make test` | every unit test |
| `make test MODULE=2` | one module |
| `make test SECTION=2.3` | one section |
| `make test FILTER=writev` | tests whose name matches |
| `make integration` | end-to-end tests against your server binaries |
| `make status` | the ladder: where you are, what is next |
| `make gdb SECTION=2.3` | that section, in-process, under gdb |
| `make SAN=1 test` | rebuilt with AddressSanitizer + UBSan |

### 0.1 — Your first syscall

Implement `hx::m0::write_str` in `src/mod0.cpp` with a single `write(2)` call,
returning exactly what `write` returned.

```bash
make test SECTION=0.1
```

Then look at it from the outside:

```bash
strace -e trace=write ./build/unit_tests --section 0.1
```

### 0.2 — `errno`

Implement `hx::m0::errno_for_open`. Three tests cover `ENOENT`, `ENOTDIR` and
success; a fourth proves you did not leak the descriptor you opened.

`ENOTDIR` for `/dev/null/nope` is worth a moment's thought: the kernel walked
the path, found `/dev/null` is not a directory, and could not continue. Path
resolution errors are a category you will meet again in section 3.7.

### 0.3 — Read the manual *(checked by hand)*

No test for this one. Do it anyway; the rest of the project assumes it.

- [ ] `man 2 write` — read RETURN VALUE and ERRORS in full. Note that a
      successful `write` may transfer **fewer** bytes than requested.
- [ ] `man 2 read` — find the difference between "returned 0" and "returned -1".
- [ ] `man 7 signal` — skim "Interruption of system calls", and note what
      `SA_RESTART` does.
- [ ] `strace -c ./build/unit_tests --module 0` — which syscall is called most?
- [ ] `ls -l /proc/self/fd` in a shell — what are fds 0, 1, 2 pointing at?

---

## Common mistakes

| symptom | cause |
| --- | --- |
| `errno` is `0` in a failure message | you read `errno` after a call that succeeded, or after `close()` clobbered it |
| The leak test fails but your code "obviously" closes the fd | an early `return` on the error path skips the `close()` |
| A test reports `CRASH: killed by signal 11` | you dereferenced something null — run `make gdb SECTION=x.y` |

**Next:** [`01-syscalls.md`](01-syscalls.md)
