// tinytest.hpp — the assignment's test framework. You should not need to edit this.
//
// Every test is tagged with a syllabus section id ("2.3") so the runner can tell
// you exactly how far up the ladder you have climbed.
//
// Each test runs in a forked child process with an alarm(), so a segfault or an
// infinite loop in your code is reported as CRASH/TIMEOUT for that one test
// instead of killing the whole run.
#pragma once

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace tt {

struct TestCase {
    std::string section;  // e.g. "2.3"
    std::string name;
    std::function<void()> fn;
    const char* file;
    int line;
};

std::vector<TestCase>& registry();

struct Registrar {
    Registrar(const char* section, const char* name, std::function<void()> fn,
              const char* file, int line);
};

// Thrown by CHECK_* on failure.
struct Failure {
    std::string msg;
};
// Thrown by SKIP().
struct Skipped {
    std::string msg;
};

[[noreturn]] void throw_failure(const char* file, int line, const std::string& msg);
std::string quote(std::string_view s);  // printable, escapes \r \n \t

}  // namespace tt

#define HX_CAT_(a, b) a##b
#define HX_CAT(a, b) HX_CAT_(a, b)

// HX_TEST("1.2", "write_all handles partial writes") { ... }
#define HX_TEST(SECTION, NAME)                                                     \
    static void HX_CAT(hx_test_fn_, __LINE__)();                                   \
    static ::tt::Registrar HX_CAT(hx_test_reg_, __LINE__)(                         \
        SECTION, NAME, HX_CAT(hx_test_fn_, __LINE__), __FILE__, __LINE__);         \
    static void HX_CAT(hx_test_fn_, __LINE__)()

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) ::tt::throw_failure(__FILE__, __LINE__, "CHECK(" #cond ") is false"); \
    } while (0)

#define CHECK_MSG(cond, msg)                                                       \
    do {                                                                           \
        if (!(cond))                                                               \
            ::tt::throw_failure(__FILE__, __LINE__,                                \
                                std::string("CHECK(" #cond ") is false: ") + (msg)); \
    } while (0)

#define CHECK_EQ(a, b)                                                             \
    do {                                                                           \
        auto&& hx_a_ = (a);                                                        \
        auto&& hx_b_ = (b);                                                        \
        if (!(hx_a_ == hx_b_))                                                     \
            ::tt::throw_failure(__FILE__, __LINE__,                                \
                                std::string(#a " == " #b " failed: got ") +        \
                                    std::to_string(hx_a_) + ", want " +            \
                                    std::to_string(hx_b_));                        \
    } while (0)

#define CHECK_NE(a, b)                                                             \
    do {                                                                           \
        auto&& hx_a_ = (a);                                                        \
        auto&& hx_b_ = (b);                                                        \
        if (hx_a_ == hx_b_)                                                        \
            ::tt::throw_failure(__FILE__, __LINE__,                                \
                                std::string(#a " != " #b " failed: both are ") +   \
                                    std::to_string(hx_a_));                        \
    } while (0)

#define CHECK_STR_EQ(a, b)                                                         \
    do {                                                                           \
        std::string hx_a_{a};                                                      \
        std::string hx_b_{b};                                                      \
        if (hx_a_ != hx_b_)                                                        \
            ::tt::throw_failure(__FILE__, __LINE__,                                \
                                std::string(#a " == " #b " failed:\n     got  ") + \
                                    ::tt::quote(hx_a_) + "\n     want " +          \
                                    ::tt::quote(hx_b_));                           \
    } while (0)

// For syscall-style returns: expects >= 0, otherwise reports errno.
#define CHECK_SYS(expr)                                                            \
    do {                                                                           \
        errno = 0;                                                                 \
        long hx_r_ = (long)(expr);                                                 \
        if (hx_r_ < 0)                                                             \
            ::tt::throw_failure(__FILE__, __LINE__,                                \
                                std::string(#expr " returned ") +                  \
                                    std::to_string(hx_r_) + " (errno=" +           \
                                    std::to_string(errno) + " " +                  \
                                    std::strerror(errno) + ")");                   \
    } while (0)

#define SKIP(msg) throw ::tt::Skipped{msg}
