// Tests for module 7 — hardening. These are the tests that stop being about
// "does it work" and start being about "what happens when someone attacks it".
#include <string>

#include "hx/mod3.hpp"
#include "tinytest.hpp"
#include "testutil.hpp"

using namespace hx::m3;

namespace {

// Deterministic PRNG: a fuzz failure you cannot reproduce is not a finding.
struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed) {}
    uint32_t next() {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return (uint32_t)(s >> 33);
    }
    uint32_t below(uint32_t n) { return next() % n; }
};

const char kValid[] =
    "GET /index.html?a=1 HTTP/1.1\r\n"
    "Host: localhost\r\n"
    "User-Agent: hx/0.1\r\n"
    "Accept: */*\r\n"
    "Content-Length: 4\r\n"
    "\r\n"
    "body";

std::string mutate(Lcg& rng, std::string s) {
    int ops = 1 + (int)rng.below(4);
    for (int i = 0; i < ops && !s.empty(); i++) {
        size_t pos = rng.below((uint32_t)s.size());
        switch (rng.below(5)) {
            case 0: s[pos] = (char)rng.below(256); break;                       // flip a byte
            case 1: s.erase(pos, 1 + rng.below(8)); break;                      // delete a run
            case 2: s.insert(pos, std::string(1 + rng.below(4), (char)rng.below(256))); break;
            case 3: s.resize(pos); break;                                       // truncate
            case 4: s.insert(pos, "\r\n"); break;                               // stray CRLF
        }
    }
    return s;
}

}  // namespace

HX_TEST("7.0", "the parser survives 20000 mutated requests without crashing or hanging") {
    // Sanity first: if the parser cannot handle a valid request, the fuzzing
    // below proves nothing. Finish module 3 before this one.
    {
        Request out;
        ParseResult r = parse_request(kValid, &out);
        CHECK_MSG(r.status == ParseStatus::Complete,
                  "parse_request cannot handle a known-good request yet — finish 3.3/3.4 first");
    }

    Lcg rng(0x5EED1234);
    for (int i = 0; i < 20000; i++) {
        std::string in = mutate(rng, kValid);
        Request out;
        ParseResult r = parse_request(in, &out);

        // The contract must hold for every input, not just the polite ones.
        if (r.status == ParseStatus::Complete) {
            CHECK_MSG(r.consumed <= in.size(),
                      "iteration " + std::to_string(i) + ": consumed " +
                          std::to_string(r.consumed) + " of a " + std::to_string(in.size()) +
                          "-byte buffer — that is a read past the end");
            CHECK_MSG(r.consumed > 0, "a Complete parse must consume something");
        } else if (r.status == ParseStatus::Error) {
            CHECK_MSG(r.http_status >= 400 && r.http_status <= 505,
                      "iteration " + std::to_string(i) + ": Error without a usable status code (" +
                          std::to_string(r.http_status) + ")");
        }
    }
}

HX_TEST("7.0", "every truncation of a valid request is Incomplete, never Complete") {
    // A parser that reports Complete on a prefix has a length bug, and that is
    // precisely how request smuggling starts.
    std::string full = kValid;
    for (size_t n = 0; n < full.size(); n++) {
        Request out;
        ParseResult r = parse_request(std::string_view(full).substr(0, n), &out);
        CHECK_MSG(r.status != ParseStatus::Complete,
                  "a " + std::to_string(n) + "-byte prefix of a " + std::to_string(full.size()) +
                      "-byte request was reported Complete");
    }
}

HX_TEST("7.1", "hostile head shapes are rejected with the right status, not accepted") {
    struct Case {
        std::string in;
        int want;
        const char* why;
    };
    std::vector<Case> cases;

    cases.push_back({"GET /" + std::string(20000, 'a') + " HTTP/1.1\r\n\r\n", 414,
                     "a 20 KB URI must be 414"});
    {
        std::string s = "GET / HTTP/1.1\r\n";
        for (int i = 0; i < 2000; i++) s += "X-Flood-" + std::to_string(i) + ": padpadpad\r\n";
        s += "\r\n";
        cases.push_back({s, 431, "2000 headers must be 431"});
    }
    cases.push_back({"POST / HTTP/1.1\r\nContent-Length: 4294967296\r\n\r\n", 413,
                     "a 4 GiB body must be refused before you allocate it"});

    for (const Case& c : cases) {
        Request out;
        ParseResult r = parse_request(c.in, &out);
        CHECK_MSG(r.status == ParseStatus::Error, std::string(c.why) + ": it was not an Error");
        CHECK_MSG(r.http_status == c.want,
                  std::string(c.why) + ": got " + std::to_string(r.http_status) + ", want " +
                      std::to_string(c.want));
    }
}

HX_TEST("7.1", "a bare-LF head is never treated as a complete request") {
    // Some clients (and every attacker) send LF instead of CRLF. Whatever you
    // decide to do about it, silently accepting it while a proxy in front of
    // you does something different is how requests get smuggled.
    const char* inputs[] = {
        "GET / HTTP/1.1\n\n",
        "GET / HTTP/1.1\nHost: h\n\n",
        "GET / HTTP/1.1\r\nHost: h\n\n",
    };
    for (const char* in : inputs) {
        Request out;
        ParseResult r = parse_request(in, &out);
        CHECK_MSG(r.status != ParseStatus::Complete,
                  std::string("bare-LF input was accepted: ") + tt::quote(in));
    }
}

HX_TEST("7.1", "NUL bytes and control characters do not sneak through the head") {
    std::string in = std::string("GET /a\0b HTTP/1.1\r\nHost: h\r\n\r\n", 30);
    Request out;
    ParseResult r = parse_request(in, &out);
    CHECK_MSG(r.status == ParseStatus::Error, "an embedded NUL in the target must be an error");
    CHECK_EQ(r.http_status, 400);
}
