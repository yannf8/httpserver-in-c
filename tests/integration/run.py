#!/usr/bin/env python3
"""End-to-end tests: they speak HTTP to the server binaries you build.

Nothing in here uses your C++ code, on purpose. These tests are a client, and
they only care about the bytes on the wire — which is exactly how the rest of
the world will judge your server.

    python3 tests/integration/run.py                 # everything
    python3 tests/integration/run.py --module 4      # just module 4
    python3 tests/integration/run.py --section 4.4
    python3 tests/integration/run.py --machine       # RESULT lines for `make status`
    python3 tests/integration/run.py -v              # show server stderr on failure
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.path.join(ROOT, "build")

CASES: list["Case"] = []


class Skip(Exception):
    pass


class Fail(Exception):
    pass


class Case:
    def __init__(self, section, name, binary, fn):
        self.section = section
        self.name = name
        self.binary = binary
        self.fn = fn


def case(section, name, binary):
    def deco(fn):
        CASES.append(Case(section, name, binary, fn))
        return fn

    return deco


# --------------------------------------------------------------- utilities ----
def free_port() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class Server:
    """Starts a server binary and waits until it accepts connections."""

    def __init__(self, binary, docroot, timeout_ms=1000, extra=()):
        self.path = os.path.join(BUILD, binary)
        if not os.path.exists(self.path):
            raise Skip(f"{binary} has not been built yet (run: make)")
        self.port = free_port()
        self.docroot = docroot
        self.log = tempfile.TemporaryFile()
        self.proc = subprocess.Popen(
            [self.path, "--port", str(self.port), "--docroot", docroot,
             "--timeout-ms", str(timeout_ms), *extra],
            stdout=self.log, stderr=subprocess.STDOUT, cwd=ROOT,
        )

    def stderr_tail(self, limit=1200) -> str:
        try:
            self.log.seek(0)
            data = self.log.read().decode("utf-8", "replace").strip()
        except Exception:
            return ""
        return data[-limit:]

    def wait_ready(self, seconds=5.0):
        deadline = time.time() + seconds
        while time.time() < deadline:
            rc = self.proc.poll()
            if rc is not None:
                raise Fail(
                    f"{os.path.basename(self.path)} exited with code {rc} before accepting "
                    f"connections.\n      --- its output ---\n      "
                    + self.stderr_tail().replace("\n", "\n      ")
                )
            try:
                s = socket.create_connection(("127.0.0.1", self.port), timeout=0.25)
                s.close()
                return
            except OSError:
                time.sleep(0.02)
        raise Fail(f"server did not accept a connection on port {self.port} within {seconds}s")

    def connect(self, timeout=3.0) -> socket.socket:
        s = socket.create_connection(("127.0.0.1", self.port), timeout=timeout)
        s.settimeout(timeout)
        return s

    def stop(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=2)
        self.log.close()

    def __enter__(self):
        self.wait_ready()
        return self

    def __exit__(self, *exc):
        self.stop()
        return False


class Docroot:
    """A throwaway document root."""

    def __init__(self, files: dict[str, bytes]):
        self.path = tempfile.mkdtemp(prefix="hxroot_")
        for name, content in files.items():
            full = os.path.join(self.path, name)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, "wb") as f:
                f.write(content)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        shutil.rmtree(self.path, ignore_errors=True)
        return False


DEFAULT_FILES = {
    "index.html": b"<!doctype html><title>hx</title><h1>it works</h1>\n",
    "notes.txt": b"plain text body\n",
    "sub/page.html": b"<p>nested</p>\n",
}


class Response:
    def __init__(self, status, reason, version, headers, body, raw):
        self.status = status
        self.reason = reason
        self.version = version
        self.headers = headers  # lowercased name -> value (first wins)
        self.body = body
        self.raw = raw

    def header(self, name, default=None):
        return self.headers.get(name.lower(), default)

    def __repr__(self):
        return f"<{self.version} {self.status} {self.reason} headers={self.headers} body={self.body[:60]!r}>"


def read_response(sock, buf=b"", deadline=None, no_body=False) -> tuple[Response, bytes]:
    """Reads one response. Returns it plus whatever bytes were left over.

    `no_body=True` for responses to HEAD: they carry the Content-Length the
    equivalent GET would have, but no body at all.
    """
    if deadline is None:
        deadline = time.time() + 3.0
    while b"\r\n\r\n" not in buf:
        if time.time() > deadline:
            raise Fail(f"timed out waiting for a response head; got {buf[:200]!r}")
        try:
            chunk = sock.recv(65536)
        except socket.timeout:
            raise Fail(f"timed out waiting for a response head; got {buf[:200]!r}")
        if not chunk:
            raise Fail(
                "the server closed the connection without sending a complete response head; "
                f"got {buf[:200]!r}"
            )
        buf += chunk

    head, rest = buf.split(b"\r\n\r\n", 1)
    lines = head.split(b"\r\n")
    m = re.match(rb"^(HTTP/1\.[01]) (\d{3}) ?(.*)$", lines[0])
    if not m:
        raise Fail(f"malformed status line: {lines[0]!r}")
    version = m.group(1).decode()
    status = int(m.group(2))
    reason = m.group(3).decode()

    headers = {}
    for line in lines[1:]:
        if b":" not in line:
            raise Fail(f"malformed header line: {line!r}")
        name, _, value = line.partition(b":")
        key = name.decode("latin1").lower()
        if key not in headers:
            headers[key] = value.decode("latin1").strip()

    body = rest
    clen = headers.get("content-length")
    if no_body:
        return Response(status, reason, version, headers, b"", head), rest
    if clen is not None and clen.isdigit():
        want = int(clen)
        while len(body) < want:
            if time.time() > deadline:
                raise Fail(f"timed out reading the body: got {len(body)} of {want} bytes")
            try:
                chunk = sock.recv(65536)
            except socket.timeout:
                raise Fail(f"timed out reading the body: got {len(body)} of {want} bytes")
            if not chunk:
                raise Fail(
                    f"connection closed mid-body: got {len(body)} of the {want} bytes that "
                    "Content-Length promised"
                )
            body += chunk
        leftover = body[want:]
        body = body[:want]
    else:
        leftover = b""

    return Response(status, reason, version, headers, body, head), leftover


def get(server, target="/", extra_headers=(), method="GET", version="HTTP/1.1"):
    """One request on a fresh connection."""
    req = f"{method} {target} {version}\r\nHost: 127.0.0.1\r\n"
    for h in extra_headers:
        req += h + "\r\n"
    req += "\r\n"
    with server.connect() as s:
        s.sendall(req.encode())
        resp, _ = read_response(s, no_body=(method == "HEAD"))
        return resp


def expect(cond, msg):
    if not cond:
        raise Fail(msg)


def expect_status(resp, want, ctx=""):
    if isinstance(want, int):
        want = (want,)
    if resp.status not in want:
        raise Fail(
            f"{ctx}expected status {' or '.join(str(w) for w in want)}, got "
            f"{resp.status} {resp.reason}"
        )


# ============================== MODULE 4: the blocking server ==============
@case("4.0", "the server starts, accepts a connection, and answers GET / with 200", "server_blocking")
def _(binary):
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path) as srv:
        resp = get(srv, "/")
        expect_status(resp, 200)
        expect(resp.version in ("HTTP/1.1", "HTTP/1.0"), f"odd version: {resp.version}")
        expect(resp.reason == "OK", f"reason phrase should be 'OK', got {resp.reason!r}")


@case("4.1", "GET a static file: exact bytes, Content-Length and Content-Type", "server_blocking")
def _(binary):
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path) as srv:
        resp = get(srv, "/notes.txt")
        expect_status(resp, 200)
        expect(resp.body == DEFAULT_FILES["notes.txt"],
               f"body mismatch: got {resp.body!r}, want {DEFAULT_FILES['notes.txt']!r}")
        cl = resp.header("content-length")
        expect(cl is not None, "Content-Length is missing — a persistent connection needs it")
        expect(int(cl) == len(resp.body), f"Content-Length {cl} != body length {len(resp.body)}")
        ct = resp.header("content-type", "")
        expect("text/plain" in ct, f"Content-Type for .txt should be text/plain, got {ct!r}")

        resp = get(srv, "/index.html")
        expect_status(resp, 200)
        expect("text/html" in resp.header("content-type", ""),
               f"Content-Type for .html should be text/html, got {resp.header('content-type')!r}")

        resp = get(srv, "/sub/page.html")
        expect_status(resp, 200, "nested path: ")
        expect(resp.body == DEFAULT_FILES["sub/page.html"], "nested file body mismatch")


@case("4.1", "GET / serves the directory index", "server_blocking")
def _(binary):
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path) as srv:
        resp = get(srv, "/")
        expect_status(resp, 200)
        expect(resp.body == DEFAULT_FILES["index.html"],
               "GET / should serve index.html from the docroot")


@case("4.1", "a 1 MiB file arrives intact", "server_blocking")
def _(binary):
    big = bytes((i * 31 + 7) & 0xFF for i in range(1024 * 1024))
    with Docroot({**DEFAULT_FILES, "big.bin": big}) as d, Server(binary, d.path) as srv:
        resp = get(srv, "/big.bin")
        expect_status(resp, 200)
        expect(len(resp.body) == len(big),
               f"got {len(resp.body)} bytes, want {len(big)} — partial writes are not handled")
        expect(resp.body == big, "the bytes are wrong, though the length is right")


@case("4.2", "404 for a file that is not there", "server_blocking")
def _(binary):
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path) as srv:
        expect_status(get(srv, "/nope.html"), 404)
        expect_status(get(srv, "/sub/nope.html"), 404)


@case("4.2", "400 for a malformed request line", "server_blocking")
def _(binary):
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path) as srv:
        with srv.connect() as s:
            s.sendall(b"THIS IS NOT HTTP\r\n\r\n")
            resp, _ = read_response(s)
            expect_status(resp, (400, 505), "garbage request: ")


@case("4.2", "405 or 501 for a method the server does not implement", "server_blocking")
def _(binary):
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path) as srv:
        resp = get(srv, "/", method="BREW")
        expect_status(resp, (405, 501), "unknown method: ")


@case("4.3", "path traversal cannot reach outside the docroot", "server_blocking")
def _(binary):
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path) as srv:
        for target in ["/../../../../etc/passwd",
                       "/..%2f..%2f..%2fetc/passwd",
                       "/%2e%2e/%2e%2e/etc/passwd",
                       "/sub/../../etc/passwd"]:
            resp = get(srv, target)
            expect(resp.status != 200, f"{target} returned 200 — the docroot is not a boundary")
            expect(b"root:" not in resp.body, f"{target} leaked /etc/passwd")


@case("4.4", "two requests on one connection (keep-alive)", "server_blocking")
def _(binary):
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path) as srv:
        with srv.connect() as s:
            s.sendall(b"GET /notes.txt HTTP/1.1\r\nHost: h\r\n\r\n")
            r1, left = read_response(s)
            expect_status(r1, 200, "first request: ")
            expect(r1.header("connection", "").lower() != "close",
                   "the server said Connection: close on an HTTP/1.1 request without being asked")
            s.sendall(b"GET /index.html HTTP/1.1\r\nHost: h\r\n\r\n")
            r2, _ = read_response(s, left)
            expect_status(r2, 200, "second request on the same connection: ")
            expect(r2.body == DEFAULT_FILES["index.html"], "second response has the wrong body")


@case("4.4", "Connection: close is honoured", "server_blocking")
def _(binary):
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path) as srv:
        with srv.connect() as s:
            s.sendall(b"GET / HTTP/1.1\r\nHost: h\r\nConnection: close\r\n\r\n")
            resp, _ = read_response(s)
            expect_status(resp, 200)
            expect(resp.header("connection", "").lower() == "close",
                   "when the client asks to close, say Connection: close in the response")
            s.settimeout(3.0)
            deadline = time.time() + 3
            while time.time() < deadline:
                if s.recv(4096) == b"":
                    break
            else:
                raise Fail("the server never closed the connection")


@case("4.4", "an HTTP/1.0 request without keep-alive gets a closed connection", "server_blocking")
def _(binary):
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path) as srv:
        with srv.connect() as s:
            s.sendall(b"GET / HTTP/1.0\r\n\r\n")
            resp, _ = read_response(s)
            expect_status(resp, 200)
            deadline = time.time() + 3
            while time.time() < deadline:
                if s.recv(4096) == b"":
                    return
            raise Fail("HTTP/1.0 defaults to close, but the connection stayed open")


@case("4.4", "two pipelined requests in a single write get two responses", "server_blocking")
def _(binary):
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path) as srv:
        with srv.connect() as s:
            s.sendall(b"GET /notes.txt HTTP/1.1\r\nHost: h\r\n\r\n"
                      b"GET /index.html HTTP/1.1\r\nHost: h\r\n\r\n")
            r1, left = read_response(s)
            expect_status(r1, 200, "first of two pipelined: ")
            expect(r1.body == DEFAULT_FILES["notes.txt"], "first pipelined body is wrong")
            r2, _ = read_response(s, left)
            expect_status(r2, 200, "second of two pipelined: ")
            expect(r2.body == DEFAULT_FILES["index.html"], "second pipelined body is wrong")


@case("4.5", "HEAD sends the headers of a GET and no body", "server_blocking")
def _(binary):
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path) as srv:
        g = get(srv, "/notes.txt")
        h = get(srv, "/notes.txt", method="HEAD")
        expect_status(h, 200, "HEAD: ")
        expect(h.body == b"", f"HEAD must not send a body, got {h.body!r}")
        expect(h.header("content-length") == g.header("content-length"),
               "HEAD should report the same Content-Length as GET would")


@case("4.6", "a client that connects and says nothing is timed out, not held forever", "server_blocking")
def _(binary):
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path, timeout_ms=700) as srv:
        with srv.connect(timeout=6) as s:
            s.sendall(b"GET / HTTP/1.1\r\nHost: h\r\n")  # deliberately unfinished
            start = time.time()
            try:
                resp, _ = read_response(s, deadline=start + 5)
                expect_status(resp, (400, 408), "partial request: ")
            except Fail as e:
                # Closing the connection is an acceptable answer; hanging is not.
                if "closed the connection" not in str(e):
                    raise
            elapsed = time.time() - start
            expect(elapsed < 5, f"the server sat on the connection for {elapsed:.1f}s")

        # And it must still be serving afterwards.
        expect_status(get(srv, "/"), 200, "after a timed-out client: ")


# ============================== MODULE 5: the epoll server =================
@case("5.3", "the epoll server answers GET / with 200", "server_epoll")
def _(binary):
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path) as srv:
        expect_status(get(srv, "/"), 200)
        expect(get(srv, "/notes.txt").body == DEFAULT_FILES["notes.txt"], "body mismatch")


@case("5.4", "64 simultaneous connections are all served", "server_epoll")
def _(binary):
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path) as srv:
        socks = []
        try:
            for _i in range(64):
                socks.append(srv.connect(timeout=5))
            for s in socks:
                s.sendall(b"GET /notes.txt HTTP/1.1\r\nHost: h\r\n\r\n")
            for i, s in enumerate(socks):
                resp, _ = read_response(s, deadline=time.time() + 5)
                expect_status(resp, 200, f"connection {i}: ")
                expect(resp.body == DEFAULT_FILES["notes.txt"], f"connection {i}: wrong body")
        finally:
            for s in socks:
                s.close()


@case("5.5", "one silent client does not stall everybody else", "server_epoll")
def _(binary):
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path, timeout_ms=10000) as srv:
        slow = srv.connect(timeout=5)
        slow.sendall(b"GET / HTTP/1.1\r\n")  # never finished
        try:
            start = time.time()
            resp = get(srv, "/notes.txt")
            elapsed = time.time() - start
            expect_status(resp, 200, "healthy client while another is stalled: ")
            expect(elapsed < 2.0,
                   f"a healthy client waited {elapsed:.2f}s behind a stalled one — the loop is "
                   "blocking somewhere")
        finally:
            slow.close()


@case("5.6", "the epoll server handles pipelined requests", "server_epoll")
def _(binary):
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path) as srv:
        with srv.connect() as s:
            s.sendall(b"GET /notes.txt HTTP/1.1\r\nHost: h\r\n\r\n"
                      b"GET /index.html HTTP/1.1\r\nHost: h\r\n\r\n"
                      b"GET /sub/page.html HTTP/1.1\r\nHost: h\r\n\r\n")
            left = b""
            wants = [DEFAULT_FILES["notes.txt"], DEFAULT_FILES["index.html"],
                     DEFAULT_FILES["sub/page.html"]]
            for i, want in enumerate(wants):
                resp, left = read_response(s, left, deadline=time.time() + 5)
                expect_status(resp, 200, f"pipelined response {i}: ")
                expect(resp.body == want, f"pipelined response {i} has the wrong body")


@case("5.5", "a large response to a slow reader is eventually delivered in full", "server_epoll")
def _(binary):
    big = bytes((i * 17 + 3) & 0xFF for i in range(2 * 1024 * 1024))
    with Docroot({**DEFAULT_FILES, "big.bin": big}) as d, \
         Server(binary, d.path, timeout_ms=20000) as srv:
        with srv.connect(timeout=20) as s:
            s.sendall(b"GET /big.bin HTTP/1.1\r\nHost: h\r\n\r\n")
            # Read slowly, in small pieces, forcing the server to park the
            # response and resume it on EPOLLOUT.
            data = b""
            deadline = time.time() + 20
            while b"\r\n\r\n" not in data:
                data += s.recv(4096)
                if time.time() > deadline:
                    raise Fail("no response head within 20s")
            head, rest = data.split(b"\r\n\r\n", 1)
            m = re.search(rb"[Cc]ontent-[Ll]ength: *(\d+)", head)
            expect(m is not None, "no Content-Length on a 2 MiB response")
            want = int(m.group(1))
            expect(want == len(big), f"Content-Length {want} != file size {len(big)}")
            body = rest
            while len(body) < want:
                chunk = s.recv(8192)
                if not chunk:
                    raise Fail(f"connection closed after {len(body)} of {want} bytes")
                body += chunk
                if time.time() > deadline:
                    raise Fail(f"only {len(body)} of {want} bytes after 20s")
            expect(body == big, "the 2 MiB body is corrupted — check your partial-write resume")


# ============================== MODULE 6: measurement ======================
@case("6.4", "the bench tool reports count, p50, p99, p999 and max", "server_epoll")
def _(binary):
    bench = os.path.join(BUILD, "bench")
    if not os.path.exists(bench):
        raise Skip("build/bench has not been built yet")
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path, timeout_ms=20000) as srv:
        proc = subprocess.run(
            [bench, "--port", str(srv.port), "--requests", "200", "--connections", "4",
             "--path", "/notes.txt"],
            capture_output=True, timeout=60, cwd=ROOT,
        )
        out = proc.stdout.decode("utf-8", "replace")
        if proc.returncode != 0:
            raise Fail(f"bench exited {proc.returncode}: "
                       f"{proc.stderr.decode('utf-8', 'replace')[-600:]}")
        vals = {}
        for key in ("count", "p50", "p99", "p999", "max"):
            m = re.search(rf"^{key}\s+(\d+)\s*$", out, re.M)
            expect(m is not None, f"no '{key} <number>' line in the bench output:\n{out[:600]}")
            vals[key] = int(m.group(1))
        expect(vals["count"] == 200, f"count is {vals['count']}, expected 200")
        expect(vals["p50"] <= vals["p99"] <= vals["p999"] <= vals["max"],
               f"percentiles are not monotonic: {vals}")
        expect(vals["p50"] > 0, "a round trip cannot take 0 ns — check your clock")


@case("6.5", "a large file is served correctly (sendfile or not, the bytes must match)", "server_epoll")
def _(binary):
    big = bytes((i * 7 + 11) & 0xFF for i in range(1024 * 1024))
    with Docroot({**DEFAULT_FILES, "big.bin": big}) as d, Server(binary, d.path) as srv:
        resp = get(srv, "/big.bin")
        expect_status(resp, 200)
        expect(len(resp.body) == len(big), f"got {len(resp.body)} of {len(big)} bytes")
        expect(resp.body == big, "content mismatch")


# ============================== MODULE 7: hardening ========================
@case("7.2", "50 idle connections (slowloris) do not stop real traffic", "server_epoll")
def _(binary):
    with Docroot(DEFAULT_FILES) as d, Server(binary, d.path, timeout_ms=1000) as srv:
        idle = []
        try:
            for _i in range(50):
                s = srv.connect(timeout=5)
                s.sendall(b"GET / HTTP/1.1\r\nX-Slow: ")  # never finished
                idle.append(s)
            start = time.time()
            expect_status(get(srv, "/notes.txt"), 200, "while 50 connections idle: ")
            elapsed = time.time() - start
            expect(elapsed < 2.0, f"a real request took {elapsed:.2f}s under slowloris")
        finally:
            for s in idle:
                s.close()


@case("7.3", "SIGTERM shuts the server down cleanly with exit code 0", "server_epoll")
def _(binary):
    with Docroot(DEFAULT_FILES) as d:
        srv = Server(binary, d.path)
        try:
            srv.wait_ready()
            expect_status(get(srv, "/"), 200, "before shutdown: ")
            srv.proc.terminate()
            try:
                rc = srv.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                raise Fail("the server ignored SIGTERM for 3s")
            expect(rc == 0, f"exited with {rc} after SIGTERM; a clean shutdown should exit 0")
        finally:
            srv.stop()


# ------------------------------------------------------------------ runner ----
def module_of(section):
    return section.split(".")[0]


def section_key(section):
    return [int(p) for p in section.split(".")]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--module")
    ap.add_argument("--section")
    ap.add_argument("--filter", default="")
    ap.add_argument("--machine", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    cases = sorted(CASES, key=lambda c: section_key(c.section))
    if args.module:
        cases = [c for c in cases if module_of(c.section) == args.module]
    if args.section:
        cases = [c for c in cases if c.section == args.section]
    if args.filter:
        cases = [c for c in cases if args.filter in c.name or args.filter in c.section]

    if args.list:
        for c in cases:
            print(f"{c.section:<6} {c.name}  [{c.binary}]")
        return 0

    if not cases:
        print("no integration tests matched", file=sys.stderr)
        return 2

    tty = sys.stdout.isatty() and not args.machine
    red = "\033[31m" if tty else ""
    green = "\033[32m" if tty else ""
    yellow = "\033[33m" if tty else ""
    dim = "\033[2m" if tty else ""
    bold = "\033[1m" if tty else ""
    rst = "\033[0m" if tty else ""

    npass = nfail = nskip = 0
    current = None
    first_broken = None

    for c in cases:
        try:
            c.fn(c.binary)
            status, message = "PASS", ""
        except Skip as e:
            status, message = "SKIP", str(e)
        except Fail as e:
            status, message = "FAIL", str(e)
        except (socket.timeout, TimeoutError):
            status, message = "FAIL", "the test timed out talking to the server"
        except subprocess.TimeoutExpired:
            status, message = "FAIL", "a subprocess overran its timeout"
        except Exception as e:  # a bug in the test, or something truly unexpected
            status, message = "FAIL", f"{type(e).__name__}: {e}"

        if args.machine:
            print(f"RESULT\t{c.section}\t{status}\t{c.name}")
        else:
            if c.section != current:
                current = c.section
                print(f"\n{bold}[{current}]{rst}")
            mark = {"PASS": f"{green}✓{rst}", "FAIL": f"{red}✗{rst}",
                    "SKIP": f"{yellow}–{rst}"}[status]
            print(f"  {mark} {c.name} {dim}({c.binary}){rst}")
            if message:
                print(f"      {dim}{message}{rst}")

        if status == "PASS":
            npass += 1
        elif status == "SKIP":
            nskip += 1
        else:
            nfail += 1
            if first_broken is None:
                first_broken = c.section

    if not args.machine:
        print(f"\n{green}{npass} passed{rst}, {red if nfail else dim}{nfail} failed{rst}, "
              f"{nskip} skipped")
        if first_broken:
            print(f"first failing section: {bold}{first_broken}{rst}")
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
