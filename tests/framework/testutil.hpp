// testutil.hpp — helpers the tests use. You should not need to edit this, but
// reading it is useful: it is written with the same syscalls you are learning.
#pragma once

#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <errno.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace tu {

// A deterministic byte pattern, so a wrong offset shows up as a wrong hash.
inline std::string pattern(size_t n, uint8_t seed = 7) {
    std::string s;
    s.resize(n);
    uint8_t v = seed;
    for (size_t i = 0; i < n; i++) {
        s[i] = (char)v;
        v = (uint8_t)(v * 31u + 7u);
    }
    return s;
}

inline uint64_t fnv1a(const void* data, size_t n) {
    const unsigned char* p = (const unsigned char*)data;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}
inline uint64_t fnv1a(const std::string& s) { return fnv1a(s.data(), s.size()); }

// ------------------------------------------------------------ temp files ----
inline std::string temp_dir() {
    static std::string dir;
    if (dir.empty()) {
        mkdir("build", 0755);
        dir = "build/tmp";
        mkdir(dir.c_str(), 0755);
    }
    return dir;
}

// Writes `content` to a unique file. Removed by the destructor.
struct TempFile {
    std::string path;

    explicit TempFile(const std::string& content, const char* suffix = ".bin") {
        static int counter = 0;
        path = temp_dir() + "/t" + std::to_string(getpid()) + "_" +
               std::to_string(counter++) + suffix;
        int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror("testutil: open temp file");
            abort();
        }
        size_t off = 0;
        while (off < content.size()) {
            ssize_t n = write(fd, content.data() + off, content.size() - off);
            if (n <= 0) {
                if (errno == EINTR) continue;
                perror("testutil: write temp file");
                abort();
            }
            off += (size_t)n;
        }
        close(fd);
    }
    ~TempFile() { unlink(path.c_str()); }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

// A directory that is removed (with its files) by the destructor.
struct TempDir {
    std::string path;
    TempDir() {
        static int counter = 0;
        path = temp_dir() + "/d" + std::to_string(getpid()) + "_" +
               std::to_string(counter++);
        mkdir(path.c_str(), 0755);
    }
    void file(const char* name, const std::string& content) {
        std::string p = path + "/" + name;
        int fd = open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) abort();
        ssize_t ignored = write(fd, content.data(), content.size());
        (void)ignored;
        close(fd);
        files_.push_back(p);
    }
    ~TempDir() {
        for (const auto& f : files_) unlink(f.c_str());
        rmdir(path.c_str());
    }

private:
    std::vector<std::string> files_;
};

// ------------------------------------------------------------- drain fork ----
// A child process that reads `fd` until EOF and reports how many bytes it saw
// and their hash. Use it whenever the writer under test would otherwise block
// because nobody is reading.
struct Drainer {
    pid_t pid = -1;
    int result_fd = -1;
    unsigned delay_us = 0;

    // `fd` is the read end (closed in the parent before start() returns).
    // `peer_fd` is the matching write end, which the child MUST close: a fork
    // duplicates every descriptor, and a reader that still holds the write end
    // will never see EOF. That bug is the reason this parameter exists.
    static Drainer start(int fd, int peer_fd, unsigned delay_us = 0,
                         bool signal_parent = false) {
        Drainer d;
        d.delay_us = delay_us;
        int rp[2];
        if (pipe(rp) != 0) abort();
        pid_t pid = fork();
        if (pid < 0) abort();
        if (pid == 0) {
            close(rp[0]);
            close(peer_fd);
            pid_t parent = getppid();
            char buf[4096];
            uint64_t total = 0;
            uint64_t h = 1469598103934665603ull;
            for (;;) {
                ssize_t n = read(fd, buf, sizeof buf);
                if (n < 0) {
                    if (errno == EINTR) continue;
                    break;
                }
                if (n == 0) break;
                total += (uint64_t)n;
                for (ssize_t i = 0; i < n; i++) {
                    h ^= (unsigned char)buf[i];
                    h *= 1099511628211ull;
                }
                // Poking the writer with a signal is how we prove EINTR
                // handling actually works.
                if (signal_parent) kill(parent, SIGUSR1);
                if (delay_us) usleep(delay_us);
            }
            uint64_t out[2] = {total, h};
            ssize_t ignored = write(rp[1], out, sizeof out);
            (void)ignored;
            close(rp[1]);
            _exit(0);
        }
        close(rp[1]);
        close(fd);
        d.pid = pid;
        d.result_fd = rp[0];
        return d;
    }

    // Waits for the child and returns (bytes, hash). Call after closing the
    // write end so the child sees EOF.
    bool finish(uint64_t* bytes, uint64_t* hash) {
        uint64_t out[2] = {0, 0};
        size_t got = 0;
        while (got < sizeof out) {
            ssize_t n = read(result_fd, (char*)out + got, sizeof out - got);
            if (n < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (n == 0) break;
            got += (size_t)n;
        }
        close(result_fd);
        int st = 0;
        while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
        if (bytes) *bytes = out[0];
        if (hash) *hash = out[1];
        return got == sizeof out;
    }
};

// Installs a no-op, non-restarting SIGUSR1 handler so that signals interrupt
// blocking syscalls with EINTR instead of being ignored.
inline void arm_interrupting_sigusr1() {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = [](int) {};
    sa.sa_flags = 0;  // deliberately NOT SA_RESTART
    sigaction(SIGUSR1, &sa, nullptr);
}

}  // namespace tu
