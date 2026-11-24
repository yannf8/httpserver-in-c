# Module 3 — Speaking HTTP/1.1

**Goal:** a correct, incremental, allocation-conscious parser and response
builder, with no sockets anywhere near it.

This module is pure functions: bytes in, structs out. That is not a stylistic
choice. It means you can unit test it exhaustively, fuzz it (module 7), and reuse
it unchanged when you swap blocking I/O for `epoll` in module 5. Protocol code
that is tangled up with I/O code can be tested only by starting a server, which
is why so much of it is wrong.

---

## Theory recap

### The wire format

```
GET /index.html?q=1 HTTP/1.1\r\n      <- request line: METHOD SP TARGET SP VERSION
Host: example.com\r\n                 <- header field
User-Agent: curl/8.0\r\n
Content-Length: 5\r\n
\r\n                                  <- empty line: end of head
hello                                 <- body, exactly Content-Length bytes
```

Rules that matter:

* Line terminator is **CRLF** (`\r\n`), not `\n`. A lone LF is not valid HTTP.
  Many servers accept it anyway; when a proxy and a server disagree about that,
  you get **request smuggling**. Section 7.1 tests that you never call a bare-LF
  head "Complete".
* The head ends at the first `\r\n\r\n`. Everything after is body.
* Exactly one space between the request-line tokens. Two spaces mean an empty
  token, which is malformed — not "collapse the whitespace".
* Header names are case-insensitive (`Host` ≡ `host` ≡ `HOST`). Values are not.
* `Name : value` — whitespace before the colon — must be rejected. It is another
  smuggling vector, and the RFC says so explicitly.
* A body's length comes from `Content-Length`, or from
  `Transfer-Encoding: chunked` (3.8), or — for HTTP/1.0 responses — from
  connection close.

A response is the same shape with a status line:

```
HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Type: text/plain\r\n\r\nhello
```

### Incremental parsing

TCP is a byte stream. A single `read()` might give you:

* half a request line,
* three requests at once (pipelining),
* a head and part of a body.

So the parser signature has to be "here is everything I have buffered so far":

```
Incomplete  -> nothing is wrong, read more and call me again
Complete    -> a whole request is in the buffer; `consumed` bytes belong to it
Error       -> malformed or over a limit; reply with `http_status` and close
```

`consumed` is what makes pipelining work: erase that prefix and parse again
before you touch the socket. Getting `consumed` wrong by even one byte is how you
end up parsing `\r\nGET /...` as a method named `"\r\nGET"`.

The version here re-parses from the start each time it is called, which is O(n²)
in the pathological case of one byte per read. Real servers keep a saved state
(`nginx` has a state-machine enum per connection; `picohttpparser` remembers the
last scan offset). Section 6.6 asks you to think about that; the tests do not
require it.

### Limits are part of the protocol

An unbounded buffer that grows while a client dribbles bytes is a memory-exhaustion
attack:

| limit | value here | violation |
| --- | --- | --- |
| request line | 8 KiB | 414 URI Too Long |
| header block | 16 KiB | 431 Request Header Fields Too Large |
| body | 1 MiB | 413 Content Too Large |
| head with no terminator at all | line + header limits | 431 |

That last row is the one people forget: if you only check the limit *after*
finding `\r\n\r\n`, a client that never sends it makes you buffer forever.

### Keep-alive

| version | default | override |
| --- | --- | --- |
| HTTP/1.1 | persistent | `Connection: close` |
| HTTP/1.0 | close | `Connection: keep-alive` |

`Connection` is a comma-separated **token list** (`Connection: TE, close`), so
match whole tokens case-insensitively. `"closely"` does not contain the token
`close`, and a substring search that thinks it does will close connections at
random.

Persistent connections are also why `Content-Length` is mandatory in your
responses: without it the client cannot tell where one response ends and the
next begins.

### The `Date` header

HTTP dates are IMF-fixdate, always GMT, always English names, fixed width:

```
Date: Thu, 01 Jan 1970 00:00:00 GMT
```

`strftime` with `%a`/`%b` is locale-dependent; under a Turkish or German locale
you would emit non-English names and break clients. Either force the C locale or
format it by hand from `struct tm` (the tests use `t=0`, `1234567890`,
`1000000000` and `2147483647`, so a hand-rolled version is easy to verify).

### Paths: your security boundary

`resolve_path` is the most security-sensitive function in the project. The order
of operations is not negotiable:

1. cut the query at `?`
2. require a leading `/` (rejects `index.html` and `http://host/x`)
3. **percent-decode** — `%2e%2e%2f` is `../`
4. reject NUL bytes and malformed escapes
5. collapse `//`, resolve `.` and `..` **on the decoded string**
6. if anything tried to climb above the root, refuse
7. if it ends in `/`, append `index.html`

Checking for `".."` *before* decoding is the classic mistake — it is CVE-2021-41773
(Apache), the IIS Unicode bug, and dozens more. Note also what this function
deliberately does not do: touch the filesystem. No `realpath`, no `stat`. It is a
pure string transformation, so it is exhaustively testable.

---

## Exercises

`src/mod3.cpp`; contracts in `include/hx/mod3.hpp` — read them first, the tests
check them literally.

| section | function(s) | notes |
| --- | --- | --- |
| 3.0 | `iequals`, `trim_ows` | ASCII only; cast to `unsigned char` before `tolower` |
| 3.1 | `parse_request_line` | three tokens, no control characters; do **not** validate the method |
| 3.2 | `parse_headers`, `Request::find` | reject obs-fold, space-before-colon, empty name |
| 3.3 | `parse_request` | Incomplete/Complete/Error, `consumed`, the no-terminator limit |
| 3.4 | `parse_content_length`, bodies | digits only; conflicting lengths are 400; oversized is 413 |
| 3.5 | `reason_phrase`, `build_response`, `http_date` | exact bytes; body copied verbatim including NULs |
| 3.6 | `wants_keep_alive` | token list, not substring |
| 3.7 | `resolve_path`, `mime_type` | decode → normalise → check |
| 3.8 | `decode_chunked` | *advanced*; skip it on a first pass and come back |

Suggested order: 3.0 → 3.1 → 3.2 → 3.5 (so you can build responses) → 3.3 → 3.4
→ 3.6 → 3.7. Then you have everything module 4 needs, and 3.8 is optional.

### Try it by hand

```bash
printf 'GET / HTTP/1.1\r\nHost: h\r\n\r\n' | nc 127.0.0.1 8080   # once your server runs
curl -v --http1.1 http://127.0.0.1:8080/     # -v shows the real bytes both ways
curl --raw -H 'Connection: close' -v http://127.0.0.1:8080/
```

`nc` (netcat) is the honest way to see your own protocol: type the request
yourself and watch exactly what comes back, CRLFs and all. `curl` is polite and
will paper over some of your mistakes.

---

## Common mistakes

| symptom | cause |
| --- | --- |
| 3.3 says `Complete` on a prefix | you searched for `\n\n` or forgot the body length |
| `consumed` is off by 2 or 4 | the `\r\n\r\n` is 4 bytes; the last header's CRLF is part of the header block |
| Pipelining test fails | you cleared the whole buffer instead of erasing `consumed` bytes |
| Traversal test passes `/%2e%2e/` through | you checked for `..` before decoding |
| `http_date` off by an hour | you used `localtime_r` instead of `gmtime_r` |
| Keep-alive closes randomly | substring search found `close` inside another token |

**Next:** [`04-blocking-server.md`](04-blocking-server.md) — where it all becomes
a real server.
