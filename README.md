# httpserver-in-c — build an HTTP server from raw Linux syscalls

A guided, self-contained assignment. You start with `socket(2)` and finish with a
single-threaded `epoll` server that serves files, handles keep-alive and
pipelining, survives slowloris, and reports its own p99 latency — built out of
syscalls, with no libraries.

Nothing here is implemented. Every function is a stub with a contract and a set of
tests. You climb the ladder; the tests tell you where you are.

```
$ make status

Module 2 — Sockets
  ✓ 2.0   socket(2)                                         1/1
  ✓ 2.1   Byte order and sockaddr_in                        4/4
  ✓ 2.2   SO_REUSEADDR                                      1/1
  ✗ 2.3   bind(2)                                           1/3   <- you are here
  ✗ 2.4   listen(2) and getsockname(2)                      0/2
  ...

37/114 checks passing
Next: 2.3 bind(2)
      theory  : docs/02-sockets.md  (section 2.3)
      run     : make test SECTION=2.3
      hints   : docs/HINTS.md  (section 2.3)
```

---

## Quick start

```bash
make            # builds the test runner and three server binaries
make test       # 91 unit tests. 0.0 is free; a few "rejects bad input"
                #   checks pass by accident on the stubs. The rest are your work
make status     # the ladder
```

Requirements: Linux, `g++` (or `clang++`) with C++20, GNU make, Python 3.8+ for
the end-to-end tests. All present on any normal Linux box; nothing else to
install.

Not on Linux, or would rather not install anything? Use the Docker container
below. It works the same on macOS, Windows and Linux.

Then open `docs/00-orientation.md` and start at section 0.1.

---

## Running in Docker

Everything this course uses is in the container: the compiler, `gdb`, `strace`,
`perf`, `curl`, `nc`, `ss`, and the man pages (`man 2 write` works). Your copy of
the repo is mounted at `/work`, so you can edit with your usual editor on the host
and build in the container. Nothing is lost when the container stops.

You need Docker with Compose (Docker Desktop on macOS/Windows, or Docker Engine
on Linux).

```bash
git clone https://github.com/yannf8/httpserver-in-c.git
cd httpserver-in-c

docker compose up -d --build     # first time: builds the image (a few minutes)
docker compose exec dev bash     # a shell inside, in /work

# inside the container, everything from Quick start works as-is
make
make status
```

Open as many shells as you like with `docker compose exec dev bash`. You want two
in module 4: one runs the server and the other talks to it.

```bash
# shell 1                          # shell 2
make run                           curl -v http://127.0.0.1:8080/
```

**From a browser on your machine**, open <http://localhost:8080/>. Your server
binds `127.0.0.1` inside the container, which the host cannot reach directly, so
the container runs a small relay that forwards host port 8080 to it. If nothing is
listening on 8080 inside, the browser just gets an empty reply.

**Stop it** with `docker compose down`. Your files stay where they are; next time
`docker compose up -d` starts again in seconds.

| situation | what to do |
| --- | --- |
| Files in `build/` are owned by root or another user | your host UID is not 1000. Rebuild with `HOST_UID=$(id -u) HOST_GID=$(id -g) docker compose up -d --build` |
| Port 8080 on your machine is already taken | change the left side of `"127.0.0.1:8080:8000"` in `compose.yaml`, e.g. `9090`, and browse to that port |
| `perf` says it has no permission | `perf` needs the host to allow it: run `sudo sysctl kernel.perf_event_paranoid=1` on the host (Linux), or do module 6.6 on a Linux host |
| You use Podman | `podman build --build-arg USER_UID=$(id -u) --build-arg USER_GID=$(id -g) -t httpserver-in-c-dev .` then `podman run --rm -it --userns=keep-id -v "$PWD":/work:z --cap-add=SYS_PTRACE --security-opt seccomp=unconfined httpserver-in-c-dev` (no browser relay this way; use `curl` inside) |

Latency numbers measured inside a container (module 6) include Docker's
networking and, on macOS/Windows, a virtual machine. They are fine for comparing
your own before/after changes, but they are not your hardware's real floor.

---

## The workflow

```
make status                     # what is next?
$EDITOR docs/0N-....md          # read the theory for that section
$EDITOR src/modN.cpp            # implement the TODO
make test SECTION=N.M           # red -> green
                                # stuck? docs/HINTS.md, section N.M
```

Every command:

| command | what it does |
| --- | --- |
| `make` | build everything |
| `make test` | all unit tests |
| `make test MODULE=3` | one module |
| `make test SECTION=3.4` | one section |
| `make test FILTER=chunked` | tests whose name matches |
| `make integration` | end-to-end tests against your server binaries |
| `make integration MODULE=4` | one module of those |
| `make status` | the ladder (unit + end-to-end) |
| `make status FAST=1` | the ladder, unit tests only |
| `make list` | every test with its section id |
| `make run` / `make run-epoll` | your server on port 8080, docroot `./www` |
| `make gdb SECTION=2.3` | that section in-process under gdb |
| `make SAN=1 test` | rebuilt with AddressSanitizer + UBSan |
| `make clean` | remove `build/` |

Each test runs in its own forked process with a timeout, so a segfault or an
infinite loop in one section is reported as `CRASH`/`TIMEOUT` for that test alone
and the rest of the run continues.

---

## The ladder

**Module 0 — Setup and the syscall interface** · [docs](docs/00-orientation.md)
| 0.0 harness self-check · 0.1 your first `write(2)` · 0.2 `errno` · 0.3 `man`/`strace`/`/proc` *(by hand)*

**Module 1 — File descriptors and I/O discipline** · [docs](docs/01-syscalls.md)
| 1.0 `syscall()` directly · 1.1 `write_all` (short writes, `EINTR`) · 1.2 `read_full` · 1.3 `read_file` · 1.4 `writev` · 1.5 no descriptor leaks

**Module 2 — Sockets** · [docs](docs/02-sockets.md)
| 2.0 `socket` · 2.1 byte order and `sockaddr_in` · 2.2 `SO_REUSEADDR` · 2.3 `bind` · 2.4 `listen` + `getsockname` · 2.5 `accept` · 2.6 `connect` · 2.7 the four-step server socket · 2.8 EOF and half-close · 2.9 `SIGPIPE`

**Module 3 — Speaking HTTP/1.1** · [docs](docs/03-http.md)
| 3.0 ASCII helpers · 3.1 the request line · 3.2 the header block · 3.3 an incremental parser · 3.4 `Content-Length` and bodies · 3.5 responses and `Date` · 3.6 keep-alive · 3.7 paths, decoding, traversal · 3.8 chunked *(advanced)*

**Module 4 — A blocking server, end to end** · [docs](docs/04-blocking-server.md)
| 4.0 the accept loop, first `200 OK` · 4.1 static files · 4.2 `404`/`400`/`405` · 4.3 the docroot as a boundary · 4.4 keep-alive and pipelining · 4.5 `HEAD` · 4.6 read timeouts

**Module 5 — Concurrency: non-blocking I/O and `epoll`** · [docs](docs/05-concurrency.md)
| 5.0 `O_NONBLOCK`/`EAGAIN` · 5.1 `epoll_create1`/`ctl`/`wait` · 5.2 partial writes and `EPOLLOUT` · 5.3 the event loop serves a request · 5.4 many connections at once · 5.5 no head-of-line blocking · 5.6 pipelining · 5.7 `SO_REUSEPORT`, one loop per core

**Module 6 — Measuring and shaving latency** · [docs](docs/06-low-latency.md)
| 6.0 `clock_gettime` · 6.1 percentiles, not averages · 6.2 `TCP_NODELAY` · 6.3 CPU affinity · 6.4 your own load generator · 6.5 `sendfile` · 6.6 `perf`, flame graphs, allocation audit *(by hand)*

**Module 7 — Hardening** · [docs](docs/07-hardening.md)
| 7.0 fuzz the parser · 7.1 limits and hostile input · 7.2 slowloris · 7.3 graceful shutdown

**Module 8 — Going further** · [docs](docs/08-going-further.md)
| 30 review questions, follow-on projects, books and references

Modules 0–4 are the beginner path and end in a working server. 5 is the
intermediate core. 6 and 7 are where the low-latency and production-safety
judgement lives.

Rough time, if you are comfortable with C++ but new to syscalls: 0–1 an evening,
2 an evening, 3 two evenings, 4 an evening, 5 two or three, 6–7 open-ended.

---

## Layout

```
include/hx/          contracts: read these, the tests check them literally
  mod0..mod6.hpp
src/                 your work
  mod0..mod6.cpp     the stubs, one per module
  server_blocking.cpp    module 4 milestone
  server_epoll.cpp       module 5 milestone
  bench.cpp              module 6 load generator
tests/
  framework/         the runner (fork-isolated, timeouts). Not your problem.
  unit/              test_mod0..7.cpp — the specification
  integration/run.py end-to-end tests: a Python HTTP client vs your binaries
docs/                theory per module, plus HINTS.md and further reading
tools/               status.py (the ladder), syllabus.tsv
www/                 default docroot for playing by hand
Dockerfile           the dev container's toolchain
compose.yaml         how it runs: repo mounted at /work, port 8080 relayed to the host
```

Rules of the road:

* **You only edit `src/` (and add files if you want).** The tests and the
  framework are the fixed part of the deal. If you think a test is wrong, say so
  — but read the contract in the header first, because the tests follow it
  exactly.
* Restructure `src/` freely. Add an `Fd` RAII class, a buffer type, whatever you
  like — as long as the declared functions keep their contracts and the server
  binaries keep their command line.

## The two server contracts

Both binaries must accept exactly:

```
./build/server_blocking --port N [--docroot DIR] [--timeout-ms MS]
./build/server_epoll    --port N [--docroot DIR] [--timeout-ms MS]
```

bind `127.0.0.1`, default docroot `./www`, default timeout 5000 ms, and exit `0`
on `SIGTERM`. The end-to-end tests pick a free port, start the binary, wait for it
to accept, and then speak HTTP at it.

`bench` must print `count`, `p50`, `p99`, `p999` and `max` lines in nanoseconds
(see `src/bench.cpp`).

## Getting unstuck

1. The failure message — it names the file, line, and the values.
2. The contract comment in `include/hx/modN.hpp`.
3. `docs/0N-*.md`, the section with the same number, including its
   "common mistakes" table.
4. `docs/HINTS.md`, same section — staged from nudge to pseudocode.
5. `man 2 <syscall>`, and `strace -f ./build/unit_tests --section N.M`.
6. Then ask, with the section id, the failure output, your code, and what you
   already tried. Ask for the misunderstanding to be explained, not for the
   function to be written — see the end of `docs/HINTS.md`.

Optional but recommended: `git init && git add -A && git commit -m "start"`, then
commit after each green section. Watching the ladder fill up in your own history
is worth something.
