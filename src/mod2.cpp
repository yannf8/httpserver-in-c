// Module 2 — sockets.  Theory: docs/02-sockets.md
//
// Run:  make test MODULE=2

#include "hx/mod2.hpp"

#include <arpa/inet.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

namespace hx::m2 {

// 2.0 — socket(2)
int make_tcp_socket() {
    // TODO(2.0): AF_INET + SOCK_STREAM. What does the third argument do?
    return kUnimplemented;
}

// 2.1 — byte order, by hand
uint16_t host_to_net16(uint16_t v) {
    // TODO(2.1): produce big-endian bytes from a host-order value using only
    // shifts and masks. x86 is little-endian, so this must actually swap.
    (void)v;
    return 0;
}

uint32_t host_to_net32(uint32_t v) {
    // TODO(2.1)
    (void)v;
    return 0;
}

// 2.1 — addresses
bool fill_sockaddr(const char* ip, uint16_t port, struct sockaddr_in* out) {
    // TODO(2.1): memset the struct, set sin_family, sin_port (network order!),
    // and sin_addr via inet_pton. inet_pton returns 1 on success — not 0.
    (void)ip;
    (void)port;
    (void)out;
    return false;
}

// 2.2 — socket options
int set_reuseaddr(int fd) {
    // TODO(2.2): setsockopt(SOL_SOCKET, SO_REUSEADDR) with an int of 1.
    (void)fd;
    return kUnimplemented;
}

// 2.3 — bind(2)
int bind_tcp(int fd, const char* ip, uint16_t port) {
    // TODO(2.3): build the sockaddr_in (reuse fill_sockaddr) then bind().
    (void)fd;
    (void)ip;
    (void)port;
    return kUnimplemented;
}

// 2.4 — listen(2)
int listen_tcp(int fd, int backlog) {
    // TODO(2.4)
    (void)fd;
    (void)backlog;
    return kUnimplemented;
}

uint16_t local_port(int fd) {
    // TODO(2.4): getsockname(2). Note the addrlen in/out parameter.
    (void)fd;
    return 0;
}

// 2.5 — accept(2)
int accept_conn(int listen_fd, struct sockaddr_in* peer) {
    // TODO(2.5): accept() returns a *new* fd for this one connection; the
    // listening fd stays open and keeps accepting. Retry on EINTR.
    (void)listen_fd;
    (void)peer;
    return kUnimplemented;
}

// 2.6 — connect(2)
int connect_tcp(const char* ip, uint16_t port) {
    // TODO(2.6): socket + connect. On failure, close the socket before
    // returning -1, and make sure you do not clobber errno while doing so.
    (void)ip;
    (void)port;
    return kUnimplemented;
}

// 2.7 — the four-step server socket
int listen_on(const char* ip, uint16_t port, int backlog) {
    // TODO(2.7): socket -> SO_REUSEADDR -> bind -> listen. Order matters:
    // which of these must happen before bind()?
    (void)ip;
    (void)port;
    (void)backlog;
    return kUnimplemented;
}

// 2.8 — reading to EOF
ssize_t recv_until_eof(int fd, std::string* out) {
    // TODO(2.8): loop read() into a stack buffer, append, stop on 0.
    (void)fd;
    (void)out;
    return kUnimplemented;
}

int half_close_write(int fd) {
    // TODO(2.8): shutdown(2) with the right SHUT_* constant.
    (void)fd;
    return kUnimplemented;
}

// 2.9 — SIGPIPE
int ignore_sigpipe() {
    // TODO(2.9): the default action for SIGPIPE terminates the process. A
    // server that dies because a browser tab closed is not a server.
    return kUnimplemented;
}

}  // namespace hx::m2
