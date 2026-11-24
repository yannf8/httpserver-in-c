// Module 5 — the same server, single-threaded, event-driven, thousands of
// connections at once.
// Theory: docs/05-concurrency.md
//
// Run:  make integration MODULE=5
//
// COMMAND LINE CONTRACT — identical to server_blocking:
//
//   ./build/server_epoll --port N [--docroot DIR] [--timeout-ms MS]
//
// The hard part is no longer HTTP — you already have a parser. The hard part is
// that every operation can now say "not yet", and you have to be able to put
// the work down and pick it up again.

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <unordered_map>

#include "hx/mod2.hpp"
#include "hx/mod3.hpp"
#include "hx/mod5.hpp"
#include "hx/mod6.hpp"

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

// TODO(5.3): one of these per connection. Everything the handler needs to
// resume must live here — there is no call stack to hold it any more.
struct Conn {
    int fd = -1;
    std::string in;            // bytes read, not yet parsed
    hx::m5::OutBuf out;        // response bytes, partially written
    bool keep_alive = true;
    bool closing = false;      // finish flushing, then close
    uint64_t last_active_ns = 0;  // for the idle timeout (5.5 / 7.2)
};

}  // namespace

int main(int argc, char** argv) {
    Options opt = parse_args(argc, argv);

    // TODO(5.3): listen_on(...), then set_nonblocking on the listening fd,
    //            make_epoll(), epoll_add(listen_fd, EPOLLIN).
    // TODO(5.3): the loop:
    //              n = epoll_wait(ep, events, kMaxEvents, next_timeout_ms)
    //              for each ready fd:
    //                listening fd  -> accept in a loop until EAGAIN
    //                                 (with EPOLLET you MUST drain it)
    //                EPOLLIN       -> read until EAGAIN, feed the parser,
    //                                 queue responses into conn->out
    //                EPOLLOUT      -> flush_out; when done, either disarm
    //                                 EPOLLOUT or close
    //                EPOLLHUP/ERR  -> tear the connection down
    // TODO(5.4): accept4(SOCK_NONBLOCK) saves you a syscall per connection.
    // TODO(5.5): never block. If a response does not fit, arm EPOLLOUT and
    //            move on — one slow client must not delay anyone else.
    // TODO(5.6): after a Complete parse, erase `consumed` bytes and try to
    //            parse again before touching the socket: that is pipelining.
    // TODO(5.7): optional — N threads, each with its own epoll and its own
    //            listening socket via SO_REUSEPORT.
    // TODO(6.2): TCP_NODELAY on accepted sockets.
    //
    // Deleting a connection while iterating the epoll_wait result array is the
    // classic use-after-free here. Decide on a rule (e.g. mark and sweep after
    // the loop) before you hit it.

    fprintf(stderr,
            "server_epoll: not implemented yet.\n"
            "  Finish module 4 first, then docs/05-concurrency.md.\n"
            "  (parsed args: port=%u docroot=%s timeout-ms=%d)\n",
            opt.port, opt.docroot, opt.timeout_ms);
    return 3;
}
