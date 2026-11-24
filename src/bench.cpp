// Module 6.4 — your own load generator and latency reporter.
//
// Run:  make integration MODULE=6
//
// COMMAND LINE CONTRACT:
//
//   ./build/bench --port N [--host H] [--requests R] [--connections C] [--path P]
//
// It must print, on stdout, at least these lines (numbers in nanoseconds):
//
//   count <n>
//   p50 <ns>
//   p99 <ns>
//   p999 <ns>
//   max <ns>
//
// Anything else you print is yours. The test checks the lines exist, that the
// count matches --requests, and that p50 <= p99 <= p999 <= max.
//
// Measure one request/response round trip per sample: timestamp before the
// write, timestamp after the last byte of the response is read. Nothing else.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hx/mod2.hpp"
#include "hx/mod3.hpp"
#include "hx/mod6.hpp"

int main(int argc, char** argv) {
    const char* host = "127.0.0.1";
    const char* path = "/";
    uint16_t port = 0;
    int requests = 1000;
    int connections = 1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = (uint16_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--host") && i + 1 < argc) host = argv[++i];
        else if (!strcmp(argv[i], "--path") && i + 1 < argc) path = argv[++i];
        else if (!strcmp(argv[i], "--requests") && i + 1 < argc) requests = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--connections") && i + 1 < argc) connections = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: %s --port N [--host H] [--requests R] [--connections C] [--path P]\n",
                    argv[0]);
            return 2;
        }
    }
    if (port == 0) {
        fprintf(stderr, "%s: --port is required\n", argv[0]);
        return 2;
    }

    // TODO(6.4): connect `connections` sockets (hx::m2::connect_tcp), set
    //            TCP_NODELAY on each, then send `requests` requests total,
    //            timing each round trip with hx::m6::now_ns() into a Hist.
    // TODO(6.4): print the contract lines above.
    // TODO(6.4): read docs/06 on coordinated omission before you believe your
    //            own p99.

    fprintf(stderr,
            "bench: not implemented yet (see docs/06-low-latency.md, 6.4).\n"
            "  (parsed args: host=%s port=%u path=%s requests=%d connections=%d)\n",
            host, port, path, requests, connections);
    return 3;
}
