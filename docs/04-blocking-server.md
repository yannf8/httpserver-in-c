# Module 4 — A blocking server, end to end

**Goal:** `curl http://127.0.0.1:8080/` returns your file. This is the milestone
where modules 1–3 become a program.

From here on the tests are **end-to-end**: a Python client speaks HTTP to your
binary over a real socket and judges only the bytes on the wire.

```bash
make integration MODULE=4
make integration SECTION=4.4
python3 tests/integration/run.py --module 4 -v     # same thing, more detail
```

---

## The contract

`src/server_blocking.cpp`, invoked as:

```
./build/server_blocking --port N [--docroot DIR] [--timeout-ms MS]
```

* bind **127.0.0.1** on that port, and listen
* `--docroot` defaults to `./www`, `--timeout-ms` to 5000
* exit **0** on `SIGTERM` (section 7.3 checks this)
* stderr is yours to log to; stdout is not inspected

Restructure the file however you like as long as that holds. The tests pick a
free port themselves and wait for you to accept.

---

## Theory recap

### The shape of a blocking server

```c
listen_fd = listen_on("127.0.0.1", port, 128);
for (;;) {
    conn_fd = accept(listen_fd, ...);       // blocks until a client arrives
    handle_connection(conn_fd);             // blocks until this client is done
    close(conn_fd);
}
```

One connection at a time. Client B waits for client A to finish. That is a real
limitation — and precisely the thing module 5 fixes — but everything else about
the server is already correct here, and it is much easier to debug. Do not skip
ahead: an `epoll` loop wrapped around a parser you have not yet made correct is
miserable to work on.

### The per-connection loop

```
buffer = ""
loop:
    r = parse_request(buffer)
    case Incomplete:  n = read(fd, tmp); n>0 ? buffer += tmp : close and return
    case Error:       send error(r.http_status) with Connection: close; return
    case Complete:    response = handle(request)
                      write_all(fd, response)
                      buffer.erase(0, r.consumed)
                      if (!wants_keep_alive(request)) return
```

Two subtleties worth internalising:

* **Parse before you read.** After answering a request there may already be a
  second one in the buffer (pipelining). If you `read()` first, you block waiting
  for bytes that already arrived — and the 4.4 pipelining test hangs.
* **`Connection: close` in your response.** When you are going to close, say so.
  Clients that assume persistence otherwise wait for a second response that never
  comes.

### Building the response

```
200 OK   + Content-Length + Content-Type + Date  + the file bytes
404      + Content-Length + Content-Type + Date  + a tiny HTML body
405      when the method is not GET or HEAD
400/414/431/413/505   whatever the parser told you
408      when the read timeout fired mid-request
```

`Content-Length` on *every* response, including errors. `HEAD` returns byte-identical
headers to `GET` and no body — that is the whole point of `HEAD`, and it means
you compute the response as if for a `GET` and then drop the body.

### Timeouts

A client that connects and sends nothing holds your only worker forever. Two ways
to bound it:

```c
struct timeval tv = { .tv_sec = 0, .tv_usec = 700000 };
setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
// read() now returns -1 with errno == EAGAIN when the timer expires
```

or `poll(&pfd, 1, timeout_ms)` before each `read`, which is more flexible and is
what you will use in module 5 anyway. Either is accepted by the test; hanging is
not. Reply `408` and close, or just close.

### Signals

* `SIGPIPE` ignored once at startup (2.9), or use `MSG_NOSIGNAL`.
* `SIGTERM`/`SIGINT`: install a handler that sets a `volatile sig_atomic_t`
  flag. Without `SA_RESTART`, the blocked `accept()` returns `EINTR`, you notice
  the flag, and you fall out of the loop and `return 0`.

The only things you may safely do in a signal handler are set such a flag or
`write()` to an fd. No `malloc`, no `printf`, no locks — the handler can
interrupt those mid-update. (`man 7 signal-safety` lists what is allowed.)

---

## Exercises

| section | what the tests check |
| --- | --- |
| 4.0 | server starts, accepts, answers `GET /` with `200 OK` |
| 4.1 | static files: exact bytes, correct `Content-Length` and `Content-Type`; `/` serves `index.html`; a 1 MiB file arrives whole (this is `write_all` earning its keep) |
| 4.2 | `404` for a missing file, `400`/`505` for garbage, `405`/`501` for `BREW` |
| 4.3 | four traversal attempts, including pre-encoded ones, must not return 200 or leak `/etc/passwd` |
| 4.4 | two requests on one connection; `Connection: close` honoured; HTTP/1.0 closes; two **pipelined** requests in one `write` get two responses |
| 4.5 | `HEAD`: same headers, no body |
| 4.6 | a client that stops mid-request is timed out, and the server still serves afterwards |

Suggested build order: 4.0 (hard-code a `200 OK` with a fixed body — it feels
great) → 4.1 → 4.2 → 4.4 → 4.5 → 4.6. 4.3 should already pass if `resolve_path`
is your only path source.

### Drive it yourself

```bash
make run                                   # port 8080, docroot ./www
curl -v http://127.0.0.1:8080/
curl -v http://127.0.0.1:8080/notes.txt
curl -v --head http://127.0.0.1:8080/
curl -v 'http://127.0.0.1:8080/../Makefile'      # must not work
printf 'GET / HTTP/1.1\r\nHost: h\r\n\r\nGET / HTTP/1.1\r\nHost: h\r\n\r\n' | nc 127.0.0.1 8080
strace -f -e trace=network,read,write ./build/server_blocking --port 8080
```

Then open `http://127.0.0.1:8080/` in a browser. A browser sends a dozen headers,
asks for `/favicon.ico`, and may open several connections — a much harsher client
than `curl`, and a good sign your parser is solid.

---

## Common mistakes

| symptom | cause |
| --- | --- |
| Test hangs, then "timed out waiting for a response head" | you did not `write_all` the whole response, or you never replied to a case you thought was impossible |
| Pipelining test hangs | you `read()` before re-parsing the buffer |
| 1 MiB test truncates | short write not handled (module 1.1) |
| The second request on a connection gets a garbled request line | `consumed` off by a few bytes; the leftover `\r\n` became part of the method |
| Browser hangs but `curl` works | no `Content-Length`, so the browser waits for EOF |
| Test says "closed the connection without sending a complete response head" | you closed on an error path without replying |
| `EADDRINUSE` when the tests run twice | `SO_REUSEADDR` (2.2) |

## Where you are

You now have a working HTTP server built from `socket`, `bind`, `listen`,
`accept`, `read`, `write` and nothing else. That is already further than most
people who claim to "know sockets".

What it cannot do is serve two clients at once. That is module 5.

**Next:** [`05-concurrency.md`](05-concurrency.md)
