// Module 2 — Sockets: from socket(2) to a byte round trip.
// Theory: docs/02-sockets.md
#pragma once

#include <netinet/in.h>
#include <sys/socket.h>

#include "hx/common.hpp"

namespace hx::m2 {

// 2.0 — Create a TCP socket over IPv4. Return the fd, or -1 with errno set.
int make_tcp_socket();

// 2.1 — Byte order. Convert a 16-bit host value to network (big-endian) byte
// order *by hand* — shifts and masks only, no htons(). Same for 32-bit.
// (After the test passes, read docs/02 on why you use htons() in real code.)
uint16_t host_to_net16(uint16_t v);
uint32_t host_to_net32(uint32_t v);

// 2.1 — Fill in a sockaddr_in for a dotted-quad IPv4 string and a host-order
// port. Zero the struct first. Use inet_pton(3). Return false if `ip` is not a
// valid IPv4 literal.
bool fill_sockaddr(const char* ip, uint16_t port, struct sockaddr_in* out);

// 2.2 — Set SO_REUSEADDR on a socket. Returns 0 or -1.
int set_reuseaddr(int fd);

// 2.3 — bind(2) the socket to ip:port. Port 0 means "kernel, pick one".
// Returns 0 or -1.
int bind_tcp(int fd, const char* ip, uint16_t port);

// 2.4 — listen(2) with the given backlog. Returns 0 or -1.
int listen_tcp(int fd, int backlog);

// 2.4 — Which port did we actually get? Use getsockname(2). Return the port in
// *host* order, or 0 on failure. (Essential for tests: they bind port 0.)
uint16_t local_port(int fd);

// 2.5 — accept(2) one connection. If `peer` is non-null, fill it with the
// client address. Retry on EINTR. Return the new connected fd, or -1.
int accept_conn(int listen_fd, struct sockaddr_in* peer);

// 2.6 — Client side: create a socket and connect(2) it to ip:port.
// Return the connected fd, or -1. Do not leak the socket if connect fails.
int connect_tcp(const char* ip, uint16_t port);

// 2.7 — Convenience used by later modules: socket + SO_REUSEADDR + bind +
// listen, in the right order. Return the listening fd, or -1.
int listen_on(const char* ip, uint16_t port, int backlog);

// 2.8 — Read from fd until EOF, appending to *out. Retry on EINTR.
// Return total bytes read, or -1 on error.
ssize_t recv_until_eof(int fd, std::string* out);

// 2.8 — Half-close: tell the peer "I am done sending" without destroying the
// socket, so it can still send to us. Returns 0 or -1.
int half_close_write(int fd);

// 2.9 — Make writing to a dead connection return EPIPE instead of killing the
// process with SIGPIPE. Returns 0 or -1.
int ignore_sigpipe();

}  // namespace hx::m2
