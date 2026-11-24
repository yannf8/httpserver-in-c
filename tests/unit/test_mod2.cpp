// Tests for module 2 — sockets.
//
// Every test binds 127.0.0.1 port 0 ("kernel, pick a free port") and then asks
// getsockname(2) what it got, so the tests never collide with each other or
// with anything else on your machine.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include "hx/mod2.hpp"
#include "tinytest.hpp"
#include "testutil.hpp"

namespace {

// Independent of the student's code: used to check their work.
uint16_t port_of(int fd) {
    sockaddr_in a{};
    socklen_t len = sizeof a;
    if (getsockname(fd, (sockaddr*)&a, &len) != 0) return 0;
    return ntohs(a.sin_port);
}

int getsockopt_int(int fd, int level, int opt) {
    int v = -1;
    socklen_t len = sizeof v;
    if (getsockopt(fd, level, opt, &v, &len) != 0) return -1;
    return v;
}

}  // namespace

// ------------------------------------------------------------------- 2.0 ----
HX_TEST("2.0", "make_tcp_socket returns a stream socket in the INET family") {
    int fd = hx::m2::make_tcp_socket();
    CHECK_SYS(fd);
    CHECK_EQ(getsockopt_int(fd, SOL_SOCKET, SO_TYPE), (int)SOCK_STREAM);
    CHECK_EQ(getsockopt_int(fd, SOL_SOCKET, SO_DOMAIN), (int)AF_INET);
    close(fd);
}

// ------------------------------------------------------------------- 2.1 ----
HX_TEST("2.1", "host_to_net16 matches htons") {
    CHECK_EQ((int)hx::m2::host_to_net16(0x1234), (int)htons(0x1234));
    CHECK_EQ((int)hx::m2::host_to_net16(8080), (int)htons(8080));
    CHECK_EQ((int)hx::m2::host_to_net16(0x00ff), (int)htons(0x00ff));
    CHECK_EQ((int)hx::m2::host_to_net16(0), 0);
}

HX_TEST("2.1", "host_to_net32 matches htonl") {
    CHECK_EQ((unsigned long)hx::m2::host_to_net32(0x01020304u), (unsigned long)htonl(0x01020304u));
    CHECK_EQ((unsigned long)hx::m2::host_to_net32(0xdeadbeefu), (unsigned long)htonl(0xdeadbeefu));
}

HX_TEST("2.1", "fill_sockaddr sets family, network-order port, and address") {
    sockaddr_in a;
    memset(&a, 0xAB, sizeof a);  // poison: they must zero it themselves
    CHECK(hx::m2::fill_sockaddr("192.168.1.10", 8080, &a));
    CHECK_EQ((int)a.sin_family, (int)AF_INET);
    CHECK_EQ((int)a.sin_port, (int)htons(8080));
    in_addr want{};
    CHECK_EQ(inet_pton(AF_INET, "192.168.1.10", &want), 1);
    CHECK_EQ((unsigned long)a.sin_addr.s_addr, (unsigned long)want.s_addr);
    // sin_zero must be zeroed, which is what memset-first gives you.
    for (size_t i = 0; i < sizeof a.sin_zero; i++)
        CHECK_MSG(a.sin_zero[i] == 0, "sin_zero is not zeroed — memset the struct first");
}

HX_TEST("2.1", "fill_sockaddr rejects things that are not IPv4 literals") {
    sockaddr_in a{};
    CHECK_MSG(!hx::m2::fill_sockaddr("999.1.1.1", 80, &a), "999.1.1.1 is not valid");
    CHECK_MSG(!hx::m2::fill_sockaddr("localhost", 80, &a), "hostnames need a resolver, not inet_pton");
    CHECK_MSG(!hx::m2::fill_sockaddr("", 80, &a), "empty string is not valid");
}

// ------------------------------------------------------------------- 2.2 ----
HX_TEST("2.2", "set_reuseaddr turns SO_REUSEADDR on") {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK_SYS(fd);
    CHECK_EQ(getsockopt_int(fd, SOL_SOCKET, SO_REUSEADDR), 0);
    CHECK_EQ(hx::m2::set_reuseaddr(fd), 0);
    CHECK_MSG(getsockopt_int(fd, SOL_SOCKET, SO_REUSEADDR) != 0, "SO_REUSEADDR is still off");
    close(fd);
}

// ------------------------------------------------------------------- 2.3 ----
HX_TEST("2.3", "bind_tcp binds to a kernel-chosen port on loopback") {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK_SYS(fd);
    CHECK_EQ(hx::m2::bind_tcp(fd, "127.0.0.1", 0), 0);
    uint16_t p = port_of(fd);
    CHECK_MSG(p != 0, "after bind, getsockname should report a real port");
    close(fd);
}

HX_TEST("2.3", "bind_tcp binds to a specific port and then reports it") {
    // Ask the kernel for a free port first, then rebind that exact number.
    int probe = socket(AF_INET, SOCK_STREAM, 0);
    CHECK_SYS(probe);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = 0;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK_SYS(bind(probe, (sockaddr*)&a, sizeof a));
    uint16_t chosen = port_of(probe);
    close(probe);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK_SYS(fd);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    CHECK_EQ(hx::m2::bind_tcp(fd, "127.0.0.1", chosen), 0);
    CHECK_EQ((int)port_of(fd), (int)chosen);
    close(fd);
}

HX_TEST("2.3", "bind_tcp fails, with errno set, on a privileged port") {
    if (geteuid() == 0) SKIP("running as root, so port 80 is allowed");
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK_SYS(fd);
    errno = 0;
    int rc = hx::m2::bind_tcp(fd, "127.0.0.1", 80);
    CHECK_MSG(rc < 0, "binding port 80 as a normal user must fail");
    CHECK_MSG(errno == EACCES || errno == EADDRINUSE,
              "expected EACCES (got a different errno — did you clobber it?)");
    close(fd);
}

// ------------------------------------------------------------------- 2.4 ----
HX_TEST("2.4", "listen_tcp puts the socket into the listening state") {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK_SYS(fd);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK_SYS(bind(fd, (sockaddr*)&a, sizeof a));
    CHECK_EQ(hx::m2::listen_tcp(fd, 16), 0);
    CHECK_MSG(getsockopt_int(fd, SOL_SOCKET, SO_ACCEPTCONN) != 0,
              "the kernel does not consider this socket listening");
    close(fd);
}

HX_TEST("2.4", "local_port reports the bound port in host byte order") {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK_SYS(fd);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK_SYS(bind(fd, (sockaddr*)&a, sizeof a));
    uint16_t truth = port_of(fd);
    CHECK_EQ((int)hx::m2::local_port(fd), (int)truth);
    close(fd);
}

// ------------------------------------------------------------- 2.5 - 2.7 ----
HX_TEST("2.7", "listen_on gives back a listening socket in one call") {
    int fd = hx::m2::listen_on("127.0.0.1", 0, 16);
    CHECK_SYS(fd);
    CHECK_MSG(getsockopt_int(fd, SOL_SOCKET, SO_ACCEPTCONN) != 0, "socket is not listening");
    CHECK_MSG(getsockopt_int(fd, SOL_SOCKET, SO_REUSEADDR) != 0, "SO_REUSEADDR was not set");
    CHECK_MSG(port_of(fd) != 0, "socket is not bound");
    close(fd);
}

HX_TEST("2.6", "connect_tcp reaches a listening socket") {
    int lfd = hx::m2::listen_on("127.0.0.1", 0, 16);
    CHECK_SYS(lfd);
    uint16_t port = port_of(lfd);

    // The backlog holds the connection for us, so no second process is needed.
    int cfd = hx::m2::connect_tcp("127.0.0.1", port);
    CHECK_SYS(cfd);
    close(cfd);
    close(lfd);
}

HX_TEST("2.6", "connect_tcp fails with ECONNREFUSED when nobody is listening") {
    int probe = hx::m2::listen_on("127.0.0.1", 0, 16);
    CHECK_SYS(probe);
    uint16_t dead_port = port_of(probe);
    close(probe);  // nothing is listening there any more

    errno = 0;
    int fd = hx::m2::connect_tcp("127.0.0.1", dead_port);
    CHECK_MSG(fd < 0, "connecting to a closed port must fail");
    CHECK_MSG(errno == ECONNREFUSED, "expected ECONNREFUSED — did you clobber errno with close()?");
}

HX_TEST("2.5", "accept_conn returns a new fd and the peer address") {
    int lfd = hx::m2::listen_on("127.0.0.1", 0, 16);
    CHECK_SYS(lfd);
    uint16_t port = port_of(lfd);
    int cfd = hx::m2::connect_tcp("127.0.0.1", port);
    CHECK_SYS(cfd);

    sockaddr_in peer;
    memset(&peer, 0xAB, sizeof peer);
    int afd = hx::m2::accept_conn(lfd, &peer);
    CHECK_SYS(afd);
    CHECK_MSG(afd != lfd, "accept returns a NEW descriptor; the listener stays open");
    CHECK_EQ((int)peer.sin_family, (int)AF_INET);
    CHECK_EQ((unsigned long)peer.sin_addr.s_addr, (unsigned long)htonl(INADDR_LOOPBACK));
    CHECK_MSG(ntohs(peer.sin_port) != 0, "the client's ephemeral port should be filled in");

    // The listener is still usable afterwards.
    int cfd2 = hx::m2::connect_tcp("127.0.0.1", port);
    CHECK_SYS(cfd2);
    int afd2 = hx::m2::accept_conn(lfd, nullptr);
    CHECK_SYS(afd2);

    close(afd);
    close(afd2);
    close(cfd);
    close(cfd2);
    close(lfd);
}

HX_TEST("2.7", "a byte round trip: client writes, server echoes, client reads") {
    int lfd = hx::m2::listen_on("127.0.0.1", 0, 16);
    CHECK_SYS(lfd);
    uint16_t port = port_of(lfd);
    int cfd = hx::m2::connect_tcp("127.0.0.1", port);
    CHECK_SYS(cfd);
    int sfd = hx::m2::accept_conn(lfd, nullptr);
    CHECK_SYS(sfd);

    const char* req = "ping";
    CHECK_EQ((long)write(cfd, req, 4), 4L);

    char buf[16] = {};
    CHECK_EQ((long)read(sfd, buf, sizeof buf), 4L);
    CHECK_STR_EQ(std::string(buf, 4), "ping");
    CHECK_EQ((long)write(sfd, "pong", 4), 4L);

    char back[16] = {};
    CHECK_EQ((long)read(cfd, back, sizeof back), 4L);
    CHECK_STR_EQ(std::string(back, 4), "pong");

    close(sfd);
    close(cfd);
    close(lfd);
}

// ------------------------------------------------------------------- 2.8 ----
HX_TEST("2.8", "recv_until_eof reads everything up to a half-close") {
    int lfd = hx::m2::listen_on("127.0.0.1", 0, 16);
    CHECK_SYS(lfd);
    uint16_t port = port_of(lfd);
    int cfd = hx::m2::connect_tcp("127.0.0.1", port);
    CHECK_SYS(cfd);
    int sfd = hx::m2::accept_conn(lfd, nullptr);
    CHECK_SYS(sfd);

    std::string payload = tu::pattern(5000, 17);

    pid_t pid = fork();
    CHECK_SYS(pid);
    if (pid == 0) {
        close(lfd);
        close(sfd);
        size_t off = 0;
        while (off < payload.size()) {
            ssize_t n = write(cfd, payload.data() + off, payload.size() - off);
            if (n <= 0) break;
            off += (size_t)n;
        }
        // Half-close: "I am done sending" — the read side stays open.
        hx::m2::half_close_write(cfd);
        char c;
        ssize_t got = read(cfd, &c, 1);  // still able to receive
        _exit(got == 1 && c == 'k' ? 0 : 1);
    }
    close(cfd);

    std::string got;
    ssize_t n = hx::m2::recv_until_eof(sfd, &got);
    CHECK_EQ((long)n, (long)payload.size());
    CHECK_MSG(got == payload, "content mismatch");

    // Because the client only half-closed, we can still reply.
    CHECK_EQ((long)write(sfd, "k", 1), 1L);
    int st = 0;
    waitpid(pid, &st, 0);
    CHECK_MSG(WIFEXITED(st) && WEXITSTATUS(st) == 0,
              "the peer could not receive after half_close_write — did you use SHUT_RDWR?");
    close(sfd);
    close(lfd);
}

// ------------------------------------------------------------------- 2.9 ----
HX_TEST("2.9", "ignore_sigpipe turns a death sentence into EPIPE") {
    // The test runner restores SIGPIPE's default action before each test, so
    // without ignore_sigpipe() this test is reported as CRASH (signal 13).
    CHECK_EQ(hx::m2::ignore_sigpipe(), 0);

    int sp[2];
    CHECK_SYS(socketpair(AF_UNIX, SOCK_STREAM, 0, sp));
    close(sp[1]);  // the peer is gone

    errno = 0;
    ssize_t n = write(sp[0], "x", 1);
    CHECK_MSG(n < 0, "writing to a socket whose peer is closed must fail");
    CHECK_EQ(errno, EPIPE);
    close(sp[0]);
}
