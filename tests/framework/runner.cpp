// runner.cpp — test registry + fork-isolated runner. You should not need to edit this.
#include "tinytest.hpp"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <map>

namespace tt {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

Registrar::Registrar(const char* section, const char* name, std::function<void()> fn,
                     const char* file, int line) {
    registry().push_back(TestCase{section, name, std::move(fn), file, line});
}

void throw_failure(const char* file, int line, const std::string& msg) {
    std::string s = std::string(file) + ":" + std::to_string(line) + ": " + msg;
    throw Failure{s};
}

std::string quote(std::string_view s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '\r': out += "\\r"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:
                if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\x%02x", (unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += "\"";
    return out;
}

}  // namespace tt

namespace {

enum class Status { Pass, Fail, Crash, Timeout, Skip };

const char* status_word(Status s) {
    switch (s) {
        case Status::Pass: return "PASS";
        case Status::Fail: return "FAIL";
        case Status::Crash: return "CRASH";
        case Status::Timeout: return "TIMEOUT";
        case Status::Skip: return "SKIP";
    }
    return "?";
}

struct Outcome {
    Status status;
    std::string message;
};

// Sort "10.2" after "9.1" and "2.10" after "2.9".
std::vector<int> section_key(const std::string& s) {
    std::vector<int> parts;
    size_t i = 0;
    while (i <= s.size()) {
        size_t dot = s.find('.', i);
        if (dot == std::string::npos) dot = s.size();
        parts.push_back(std::atoi(s.substr(i, dot - i).c_str()));
        if (dot == s.size()) break;
        i = dot + 1;
    }
    return parts;
}

std::string module_of(const std::string& section) {
    size_t dot = section.find('.');
    return dot == std::string::npos ? section : section.substr(0, dot);
}

// Child protocol: exit 0 = pass, 1 = fail, 2 = skip; message written to a pipe.
Outcome run_isolated(const tt::TestCase& tc, unsigned timeout_s, bool fork_isolated) {
    if (!fork_isolated) {
        try {
            tc.fn();
            return {Status::Pass, ""};
        } catch (const tt::Failure& f) {
            return {Status::Fail, f.msg};
        } catch (const tt::Skipped& s) {
            return {Status::Skip, s.msg};
        } catch (const std::exception& e) {
            return {Status::Fail, std::string("threw std::exception: ") + e.what()};
        } catch (...) {
            return {Status::Fail, "threw an unknown exception"};
        }
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) return {Status::Fail, "runner: pipe() failed"};

    // Empty our own stdio buffers before forking: the child inherits a copy of
    // them, and anything still sitting there would be printed twice.
    fflush(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return {Status::Fail, "runner: fork() failed"};
    }

    if (pid == 0) {
        close(pipefd[0]);
        alarm(timeout_s);
        // Each test starts from the default disposition, so a test that needs
        // SIGPIPE ignored has to say so itself (see section 2.9).
        signal(SIGPIPE, SIG_DFL);
        // Unbuffered, so a printf() you add while debugging appears even though
        // this process ends with _exit().
        setvbuf(stdout, nullptr, _IONBF, 0);
        int code = 0;
        std::string msg;
        try {
            tc.fn();
        } catch (const tt::Failure& f) {
            code = 1;
            msg = f.msg;
        } catch (const tt::Skipped& s) {
            code = 2;
            msg = s.msg;
        } catch (const std::exception& e) {
            code = 1;
            msg = std::string("threw std::exception: ") + e.what();
        } catch (...) {
            code = 1;
            msg = "threw an unknown exception";
        }
        if (!msg.empty()) {
            ssize_t ignored = write(pipefd[1], msg.data(), msg.size());
            (void)ignored;
        }
        close(pipefd[1]);
        _exit(code);
    }

    close(pipefd[1]);

    int wstatus = 0;
    while (waitpid(pid, &wstatus, 0) < 0 && errno == EINTR) {}

    // Read the message only after the child is gone, and never wait for EOF:
    // a test may have forked helpers of its own that still hold the write end,
    // and blocking here would hang the whole run.
    std::string msg;
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    char buf[4096];
    for (;;) {
        ssize_t n = read(pipefd[0], buf, sizeof buf);
        if (n > 0) {
            msg.append(buf, (size_t)n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
    close(pipefd[0]);

    if (WIFSIGNALED(wstatus)) {
        int sig = WTERMSIG(wstatus);
        if (sig == SIGALRM)
            return {Status::Timeout,
                    "test did not finish within " + std::to_string(timeout_s) +
                        "s — an unfinished loop, or a blocking read/accept that never returns?"};
        return {Status::Crash, std::string("killed by signal ") + std::to_string(sig) + " (" +
                                   strsignal(sig) + ")"};
    }
    int code = WEXITSTATUS(wstatus);
    if (code == 0) return {Status::Pass, msg};
    if (code == 2) return {Status::Skip, msg};
    if (code == 1) return {Status::Fail, msg};
    return {Status::Fail, "unexpected child exit code " + std::to_string(code) +
                              (msg.empty() ? "" : ": " + msg)};
}

void usage() {
    fputs(
        "usage: unit_tests [options]\n"
        "  --module N        only run sections belonging to module N (e.g. --module 3)\n"
        "  --section X.Y     only run one section (e.g. --section 3.4)\n"
        "  --filter TEXT     only run tests whose name or section contains TEXT\n"
        "  --list            list tests without running them\n"
        "  --machine         emit RESULT<TAB>section<TAB>STATUS<TAB>name lines (used by make status)\n"
        "  --no-fork         run in-process (for gdb; one crash ends the run)\n"
        "  --timeout N       per-test timeout in seconds (default 10)\n",
        stderr);
}

}  // namespace

int main(int argc, char** argv) {
    std::string only_module, only_section, filter;
    bool list_only = false, machine = false, fork_isolated = true;
    unsigned timeout_s = 10;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s needs a value\n", what);
                exit(2);
            }
            return argv[++i];
        };
        if (a == "--module") only_module = next("--module");
        else if (a == "--section") only_section = next("--section");
        else if (a == "--filter") filter = next("--filter");
        else if (a == "--timeout") timeout_s = (unsigned)std::atoi(next("--timeout").c_str());
        else if (a == "--list") list_only = true;
        else if (a == "--machine") machine = true;
        else if (a == "--no-fork") fork_isolated = false;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { fprintf(stderr, "unknown option: %s\n", a.c_str()); usage(); return 2; }
    }

    // A dead child of our own must never be mistaken for a test result.
    signal(SIGPIPE, SIG_IGN);

    auto& all = tt::registry();
    std::stable_sort(all.begin(), all.end(), [](const tt::TestCase& a, const tt::TestCase& b) {
        return section_key(a.section) < section_key(b.section);
    });

    std::vector<const tt::TestCase*> selected;
    for (const auto& tc : all) {
        if (!only_module.empty() && module_of(tc.section) != only_module) continue;
        if (!only_section.empty() && tc.section != only_section) continue;
        if (!filter.empty() && tc.name.find(filter) == std::string::npos &&
            tc.section.find(filter) == std::string::npos)
            continue;
        selected.push_back(&tc);
    }

    if (selected.empty()) {
        fprintf(stderr, "no tests matched your selection (%zu registered in total)\n", all.size());
        return 2;
    }

    if (list_only) {
        for (const auto* tc : selected)
            printf("%-6s %s  [%s:%d]\n", tc->section.c_str(), tc->name.c_str(), tc->file, tc->line);
        return 0;
    }

    const bool color = !machine && isatty(1);
    const char* red = color ? "\033[31m" : "";
    const char* green = color ? "\033[32m" : "";
    const char* yellow = color ? "\033[33m" : "";
    const char* dim = color ? "\033[2m" : "";
    const char* bold = color ? "\033[1m" : "";
    const char* rst = color ? "\033[0m" : "";

    int pass = 0, fail = 0, skip = 0;
    std::string current_section;
    std::string first_broken;

    for (const auto* tc : selected) {
        Outcome o = run_isolated(*tc, timeout_s, fork_isolated);

        if (machine) {
            printf("RESULT\t%s\t%s\t%s\n", tc->section.c_str(), status_word(o.status),
                   tc->name.c_str());
        } else {
            if (tc->section != current_section) {
                current_section = tc->section;
                printf("\n%s%s%s\n", bold, ("[" + current_section + "]").c_str(), rst);
            }
            const char* mark = o.status == Status::Pass ? "✓" : o.status == Status::Skip ? "–" : "✗";
            const char* col = o.status == Status::Pass ? green
                              : o.status == Status::Skip ? yellow
                                                         : red;
            printf("  %s%s%s %s%s\n", col, mark, rst, tc->name.c_str(),
                   o.status == Status::Pass ? "" : "");
            if (o.status != Status::Pass && !o.message.empty())
                printf("      %s%s%s\n", dim, o.message.c_str(), rst);
            else if (o.status != Status::Pass)
                printf("      %s%s%s\n", dim, status_word(o.status), rst);
        }

        switch (o.status) {
            case Status::Pass: pass++; break;
            case Status::Skip: skip++; break;
            default:
                fail++;
                if (first_broken.empty()) first_broken = tc->section;
        }
    }

    if (!machine) {
        printf("\n%s%d passed%s, %s%d failed%s, %d skipped\n", green, pass, rst,
               fail ? red : dim, fail, rst, skip);
        if (!first_broken.empty())
            printf("first failing section: %s%s%s   (see docs/, then: make test SECTION=%s)\n",
                   bold, first_broken.c_str(), rst, first_broken.c_str());
        else
            printf("everything selected is green. Run `make status` to see the whole ladder.\n");
    }
    return fail ? 1 : 0;
}
