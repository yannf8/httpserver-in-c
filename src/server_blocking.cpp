// Module 4 — the milestone: a real, blocking, one-connection-at-a-time HTTP
// server built out of everything from modules 1-3.
// Theory: docs/04-blocking-server.md
//
// Run:  make integration            (all end-to-end tests)
//       make integration MODULE=4   (just module 4)
//
// COMMAND LINE CONTRACT — the integration tests depend on this exactly:
//
//   ./build/server_blocking --port N [--docroot DIR] [--timeout-ms MS]
//
//   * bind 127.0.0.1 on the given port, and listen
//   * --docroot defaults to "./www"
//   * --timeout-ms is the per-connection idle/read timeout, default 5000
//   * exit(0) on SIGTERM; anything else on a fatal error
//   * you may log whatever you like to stderr; stdout is not inspected
//
// You are free to restructure this file however you like, as long as the
// command line contract holds.

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hx/mod1.hpp"
#include "hx/mod2.hpp"
#include "hx/mod3.hpp"

namespace {

struct Options {
    uint16_t port = 0;
    const char* docroot = "./www";
    int timeout_ms = 5000;
};

Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc)
            o.port = (uint16_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--docroot") && i + 1 < argc)
            o.docroot = argv[++i];
        else if (!strcmp(argv[i], "--timeout-ms") && i + 1 < argc)
            o.timeout_ms = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: %s --port N [--docroot DIR] [--timeout-ms MS]\n", argv[0]);
            exit(2);
        }
    }
    if (o.port == 0) {
        fprintf(stderr, "%s: --port is required\n", argv[0]);
        exit(2);
    }
    return o;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt = parse_args(argc, argv);

    // TODO(4.0): ignore SIGPIPE (hx::m2::ignore_sigpipe) and install a SIGTERM
    //            handler that flips a volatile sig_atomic_t flag so the accept
    //            loop can exit cleanly. See 7.3.
    // TODO(4.0): listen_fd = hx::m2::listen_on("127.0.0.1", opt.port, 128);
    // TODO(4.0): accept loop:
    //              for each connection:
    //                read into a std::string buffer
    //                hx::m3::parse_request on what you have so far
    //                  Incomplete -> read more (respect the timeout)
    //                  Error      -> send that status, close
    //                  Complete   -> handle it, then decide keep-alive (3.6)
    //                close the connection fd — every time, on every path
    // TODO(4.1): serve files: resolve_path -> read_file -> 200 with
    //            Content-Length and Content-Type, or 404.
    // TODO(4.2): 400 / 404 / 405 / 501 error responses with a tiny HTML body.
    // TODO(4.3): make sure resolve_path really is your only path source.
    // TODO(4.4): keep-alive loop: several requests on one connection, and
    //            handle a pipelined second request already sitting in the buffer.
    // TODO(4.5): HEAD — identical headers to GET, no body.
    // TODO(4.6): SO_RCVTIMEO (or a poll() with a deadline) so a client that
    //            opens a connection and says nothing cannot hold a worker
    //            forever. Reply 408 and close.

    fprintf(stderr,
            "server_blocking: not implemented yet.\n"
            "  Start with docs/04-blocking-server.md, section 4.0.\n"
            "  (parsed args: port=%u docroot=%s timeout-ms=%d)\n",
            opt.port, opt.docroot, opt.timeout_ms);
    return 3;
}
