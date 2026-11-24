# Module 2 — Sockets

**Goal:** get two processes exchanging bytes over TCP using nothing but syscalls,
and understand each of the four steps that turn a socket into a server.

---

## Theory recap

### The two lifecycles

```
   SERVER                                 CLIENT
   socket()      create an endpoint       socket()
   bind()        claim an address            |
   listen()      start queueing SYNs         |
   accept()  <---------- 3-way handshake --- connect()
      |                                      |
   read()/write() on the ACCEPTED fd      read()/write()
      |                                      |
   close()                                close()
```

Four calls on the server, two on the client. The asymmetry matters:

* `socket()` gives you an unbound, unconnected endpoint. It has no address yet.
* `bind()` claims a local address (IP + port). Skip it on a client and the kernel
  picks an ephemeral port at `connect()` time.
* `listen()` flips the socket into passive mode and creates two queues (below).
  A listening socket can never transfer data.
* `accept()` removes one completed connection from the queue and returns a
  **brand new descriptor** for it. The listening fd stays open and keeps
  accepting. Every beginner conflates these two; the tests check that you do not.

### `sockaddr_in`, and why it is so awkward

```c
struct sockaddr_in {
    sa_family_t    sin_family;   // AF_INET
    in_port_t      sin_port;     // port, NETWORK byte order
    struct in_addr sin_addr;     // IPv4 address, NETWORK byte order
    char           sin_zero[8];  // padding, must be zero
};
```

`bind`, `connect`, `accept` and friends take a `struct sockaddr*` — a generic
base "type" from before C had any way of expressing that. You build the concrete
`sockaddr_in` and cast. Always `memset` it to zero first: `sin_zero` must be
zero, and on some paths the kernel compares the whole struct.

### Byte order

Network protocols are big-endian ("network byte order"). x86 is little-endian.
So port 8080 = `0x1F90` is stored as the bytes `1F 90` on the wire, but as
`90 1F` in your `uint16_t`.

```c
uint16_t htons(uint16_t)   // host -> network, short  (16-bit)
uint32_t htonl(uint32_t)   // host -> network, long   (32-bit)
uint16_t ntohs(uint16_t)   // and back
uint32_t ntohl(uint32_t)
```

In section 2.1 you implement the swap by hand once, so the abstraction stops
being magic — then use `htons`/`ntohs` everywhere afterwards, because they
compile to a single `bswap`/`rol` instruction and are correct on big-endian
machines where your hand-rolled version would wrongly swap.

Addresses are text on the outside and 32 bits on the inside:

```c
struct in_addr a;
inet_pton(AF_INET, "127.0.0.1", &a);   // returns 1 on success, 0 if malformed, -1 on bad family
```

`inet_pton` returning `0` for "not a valid address" — rather than `-1` — trips
people up constantly. It also does *not* do DNS: `"localhost"` is a hostname and
must fail. (Resolution is `getaddrinfo(3)`, which does DNS, IPv6 and service
names, and is what you would actually use in production.)

### `SO_REUSEADDR`

Restart a server that just had connections and `bind()` fails with
`EADDRINUSE` — because closed connections sit in `TIME_WAIT` for up to 2×MSL
(60 s on Linux) holding the address pair. `SO_REUSEADDR` says "let me bind even
though `TIME_WAIT` entries exist". It must be set **before** `bind()`; setting it
afterwards does nothing.

It does *not* let two live sockets listen on the same port — that is
`SO_REUSEPORT`, a different option with different semantics, which you meet in
5.7.

### The two accept queues, and `backlog`

```
SYN arrives -> [ SYN queue: half-open, handshake in progress ]
                             |  handshake completes
                             v
               [ accept queue: ready, waiting for accept() ]  <- backlog bounds this
                             |  accept()
                             v
                    your new connected fd
```

`listen(fd, backlog)` bounds the accept queue (`/proc/sys/net/core/somaxconn`
caps it; the SYN queue is `tcp_max_syn_backlog`). If the accept queue overflows,
Linux drops the SYN or sends a RST depending on `tcp_abort_on_overflow`, and the
client sees a timeout or connection reset. A backlog of 128–1024 is normal for a
server; the tests use small values because they accept immediately.

An important consequence, used throughout the tests: **`connect()` completes as
soon as the kernel finishes the handshake, whether or not you ever call
`accept()`.** That is why a single-process test can connect to itself and then
accept afterwards, with no threads.

### Connection teardown, EOF, and the half-close

TCP connections are two independent byte streams. `close()` kills both. To say
"I am done sending but keep listening", use:

```c
shutdown(fd, SHUT_WR);   // send FIN; peer's read() now returns 0
shutdown(fd, SHUT_RD);   // stop receiving
shutdown(fd, SHUT_RDWR); // both, but the fd still exists until close()
```

The peer sees `read() == 0` — that is what EOF *is* on a socket: a FIN arrived.
HTTP/1.0's "read until EOF" response framing relies on exactly this, and
`Connection: close` is the modern remnant of it.

`shutdown` affects the connection; `close` affects your descriptor (and only
tears down the connection when the last descriptor referring to it goes away —
relevant after `fork()`).

### `SIGPIPE`: the footgun

Write to a socket whose peer has closed and:

* the first write may well succeed (it goes into your send buffer),
* the peer's TCP stack answers with RST,
* your next write raises **`SIGPIPE`**, whose default action is to **kill your
  process**.

A server that dies because a browser tab closed is not a server. Two fixes:

```c
signal(SIGPIPE, SIG_IGN);            // process-wide; write() now returns -1/EPIPE
send(fd, buf, n, MSG_NOSIGNAL);      // per-call; often preferred in libraries
```

Ignore it once at startup and check `EPIPE` from your writes.

---

## Exercises

Sections 2.0 → 2.9 in `src/mod2.cpp`. They build on each other, and from 2.5
onwards the tests do a real loopback round trip inside one process.

| section | what you implement | the test's angle |
| --- | --- | --- |
| 2.0 | `make_tcp_socket` | checks `SO_TYPE` and `SO_DOMAIN` via `getsockopt` |
| 2.1 | byte swaps, `fill_sockaddr` | compares against `htons`; poisons the struct first so you must `memset`; rejects `"localhost"` and `"999.1.1.1"` |
| 2.2 | `set_reuseaddr` | reads the option back |
| 2.3 | `bind_tcp` | port 0, a specific port, and `EACCES` on port 80 |
| 2.4 | `listen_tcp`, `local_port` | `SO_ACCEPTCONN`; `getsockname` in host order |
| 2.5 | `accept_conn` | new fd ≠ listening fd; peer address filled; listener still usable |
| 2.6 | `connect_tcp` | success, and `ECONNREFUSED` with `errno` intact |
| 2.7 | `listen_on` | the four steps in the right order, then a `ping`/`pong` round trip |
| 2.8 | `recv_until_eof`, `half_close_write` | 5 KB through a half-closed connection, and the peer can still reply |
| 2.9 | `ignore_sigpipe` | without it, the test is reported as `CRASH: killed by signal 13` |

### Watch it from outside

```bash
ss -ltnp                        # your listener should appear
ss -tnp | grep 8080             # established connections
strace -e trace=network ./build/unit_tests --module 2
```

While your server from module 4 is running, `ss -tn state time-wait` shows the
`TIME_WAIT` entries that make `SO_REUSEADDR` necessary.

---

## Common mistakes

| symptom | cause |
| --- | --- |
| `bind` fails with `EADDRINUSE` on restart | `SO_REUSEADDR` not set, or set after `bind` |
| `connect` "works" against a dead port | you did not check the return value |
| `errno` is wrong after `connect_tcp` fails | `close()` overwrote it — save and restore |
| Test 2.9 reports `CRASH signal 13` | that is the point: SIGPIPE's default action killed you |
| `accept` returns `EINVAL` | you never called `listen()` |
| Peer sees no data after `half_close_write` | you used `SHUT_RDWR` |

**Next:** [`03-http.md`](03-http.md)
