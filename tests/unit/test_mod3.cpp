// Tests for module 3 — HTTP/1.1 parsing and building.
//
// No sockets here at all. If these pass, your protocol layer is correct
// independently of your I/O layer — which is exactly the property that lets you
// swap a blocking server for an epoll server in module 5 without touching it.
#include <string>

#include "hx/mod3.hpp"
#include "tinytest.hpp"
#include "testutil.hpp"

using namespace hx::m3;

namespace {

// Readable failure messages for the enum.
const char* ps(ParseStatus s) {
    switch (s) {
        case ParseStatus::Incomplete: return "Incomplete";
        case ParseStatus::Complete: return "Complete";
        case ParseStatus::Error: return "Error";
    }
    return "?";
}

std::string headers_of(const Request& r) {
    std::string s;
    for (const auto& h : r.headers) s += h.name + "=" + h.value + ";";
    return s;
}

}  // namespace

// ------------------------------------------------------------------- 3.0 ----
HX_TEST("3.0", "iequals compares ASCII case-insensitively") {
    CHECK(iequals("Content-Length", "content-length"));
    CHECK(iequals("HOST", "host"));
    CHECK(iequals("", ""));
    CHECK(!iequals("Host", "Hosts"));
    CHECK(!iequals("Host", "Hos"));
    CHECK(!iequals("a", "b"));
    // '_' is 0x5f and 'DEL' games: only letters may be folded.
    CHECK(!iequals("a_b", "a-b"));
    CHECK(!iequals("{", "["));
}

HX_TEST("3.0", "trim_ows strips spaces and tabs from both ends only") {
    CHECK_STR_EQ(trim_ows("  hello  "), "hello");
    CHECK_STR_EQ(trim_ows("\t\thello\t"), "hello");
    CHECK_STR_EQ(trim_ows("hello"), "hello");
    CHECK_STR_EQ(trim_ows("   "), "");
    CHECK_STR_EQ(trim_ows(""), "");
    CHECK_STR_EQ(trim_ows("a b"), "a b");
}

// ------------------------------------------------------------------- 3.1 ----
HX_TEST("3.1", "parse_request_line splits the three tokens") {
    std::string m, t, v;
    CHECK(parse_request_line("GET /index.html HTTP/1.1", &m, &t, &v));
    CHECK_STR_EQ(m, "GET");
    CHECK_STR_EQ(t, "/index.html");
    CHECK_STR_EQ(v, "HTTP/1.1");

    CHECK(parse_request_line("POST /a/b?c=d&e=f HTTP/1.0", &m, &t, &v));
    CHECK_STR_EQ(m, "POST");
    CHECK_STR_EQ(t, "/a/b?c=d&e=f");
    CHECK_STR_EQ(v, "HTTP/1.0");

    // An unknown method still parses — deciding it is unsupported is the
    // server's policy, not the parser's.
    CHECK(parse_request_line("BREW /coffee HTTP/1.1", &m, &t, &v));
    CHECK_STR_EQ(m, "BREW");
}

HX_TEST("3.1", "parse_request_line rejects malformed lines") {
    std::string m, t, v;
    CHECK_MSG(!parse_request_line("GET /index.html", &m, &t, &v), "only two tokens");
    CHECK_MSG(!parse_request_line("GET", &m, &t, &v), "one token");
    CHECK_MSG(!parse_request_line("", &m, &t, &v), "empty line");
    CHECK_MSG(!parse_request_line("GET  /x HTTP/1.1", &m, &t, &v), "double space = empty token");
    CHECK_MSG(!parse_request_line("GET /x HTTP/1.1 extra", &m, &t, &v), "four tokens");
    CHECK_MSG(!parse_request_line(" GET /x HTTP/1.1", &m, &t, &v), "leading space");
    CHECK_MSG(!parse_request_line("GET /x\tHTTP/1.1", &m, &t, &v), "tab is not a valid separator");
    CHECK_MSG(!parse_request_line(std::string("GET /x\0y HTTP/1.1", 17), &m, &t, &v),
              "control characters must be rejected");
}

// ------------------------------------------------------------------- 3.2 ----
HX_TEST("3.2", "parse_headers reads name/value pairs and trims the value") {
    std::vector<Header> h;
    CHECK(parse_headers("Host: example.com\r\nContent-Length:  7 \r\nX-Empty:\r\n", &h));
    CHECK_EQ((long)h.size(), 3L);
    CHECK_STR_EQ(h[0].name, "Host");
    CHECK_STR_EQ(h[0].value, "example.com");
    CHECK_STR_EQ(h[1].name, "Content-Length");
    CHECK_STR_EQ(h[1].value, "7");
    CHECK_STR_EQ(h[2].name, "X-Empty");
    CHECK_STR_EQ(h[2].value, "");
}

HX_TEST("3.2", "parse_headers accepts an empty block and preserves name case") {
    std::vector<Header> h;
    CHECK(parse_headers("", &h));
    CHECK_EQ((long)h.size(), 0L);
    CHECK(parse_headers("User-Agent: curl/8.0\r\n", &h));
    CHECK_STR_EQ(h[0].name, "User-Agent");
}

HX_TEST("3.2", "parse_headers rejects smuggling-friendly garbage") {
    std::vector<Header> h;
    CHECK_MSG(!parse_headers("NoColonHere\r\n", &h), "missing colon");
    CHECK_MSG(!parse_headers("Host : example.com\r\n", &h), "space before the colon");
    CHECK_MSG(!parse_headers("Host\t: example.com\r\n", &h), "tab before the colon");
    CHECK_MSG(!parse_headers(": empty-name\r\n", &h), "empty field name");
    CHECK_MSG(!parse_headers(" Host: example.com\r\n", &h), "obs-fold continuation line");
    CHECK_MSG(!parse_headers("Host: a\r\n\tmore\r\n", &h), "obs-fold continuation line");
}

HX_TEST("3.2", "Request::find is case-insensitive and returns the first match") {
    Request r;
    r.headers = {{"Host", "a"}, {"Content-Length", "7"}, {"host", "b"}};
    const std::string* v = r.find("HOST");
    CHECK_MSG(v != nullptr, "Host should be found regardless of case");
    CHECK_STR_EQ(*v, "a");
    v = r.find("content-length");
    CHECK_MSG(v != nullptr, "Content-Length should be found");
    CHECK_STR_EQ(*v, "7");
    CHECK_MSG(r.find("X-Absent") == nullptr, "absent headers must return nullptr");
}

// ------------------------------------------------------------------- 3.3 ----
HX_TEST("3.3", "parse_request is Incomplete until the blank line arrives") {
    const std::string req =
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "User-Agent: hx\r\n"
        "\r\n";
    for (size_t n = 0; n < req.size(); n++) {
        Request out;
        ParseResult r = parse_request(std::string_view(req).substr(0, n), &out);
        CHECK_MSG(r.status == ParseStatus::Incomplete,
                  "prefix of length " + std::to_string(n) + " reported " + ps(r.status) +
                      ", expected Incomplete");
    }
    Request out;
    ParseResult r = parse_request(req, &out);
    CHECK_STR_EQ(ps(r.status), "Complete");
    CHECK_EQ((long)r.consumed, (long)req.size());
    CHECK_STR_EQ(out.method, "GET");
    CHECK_STR_EQ(out.target, "/index.html");
    CHECK_STR_EQ(out.version, "HTTP/1.1");
    CHECK_EQ((long)out.headers.size(), 2L);
    CHECK_STR_EQ(headers_of(out), "Host=localhost;User-Agent=hx;");
    CHECK_EQ((long)out.body.size(), 0L);
}

HX_TEST("3.3", "parse_request handles a request with no headers") {
    const std::string req = "GET / HTTP/1.0\r\n\r\n";
    Request out;
    ParseResult r = parse_request(req, &out);
    CHECK_STR_EQ(ps(r.status), "Complete");
    CHECK_EQ((long)r.consumed, (long)req.size());
    CHECK_EQ((long)out.headers.size(), 0L);
}

HX_TEST("3.3", "consumed lets the caller find a pipelined second request") {
    const std::string first = "GET /a HTTP/1.1\r\nHost: h\r\n\r\n";
    const std::string second = "GET /b HTTP/1.1\r\nHost: h\r\n\r\n";
    std::string buf = first + second;

    Request out;
    ParseResult r = parse_request(buf, &out);
    CHECK_STR_EQ(ps(r.status), "Complete");
    CHECK_EQ((long)r.consumed, (long)first.size());
    CHECK_STR_EQ(out.target, "/a");

    buf.erase(0, r.consumed);
    Request out2;
    ParseResult r2 = parse_request(buf, &out2);
    CHECK_STR_EQ(ps(r2.status), "Complete");
    CHECK_EQ((long)r2.consumed, (long)second.size());
    CHECK_STR_EQ(out2.target, "/b");
}

HX_TEST("3.3", "parse_request rejects a malformed request line or header block") {
    struct Case {
        const char* in;
        int want;
        const char* why;
    } cases[] = {
        {"GET  /x HTTP/1.1\r\n\r\n", 400, "empty token in the request line"},
        {"GET /x\r\n\r\n", 400, "missing version"},
        {"GET /x HTTP/1.1\r\nNoColon\r\n\r\n", 400, "header without a colon"},
        {"GET /x HTTP/1.1\r\nHost : h\r\n\r\n", 400, "space before the colon"},
        {"GET /x HTTP/3.0\r\n\r\n", 505, "unsupported version"},
        {"GET /x HTTP/1.2\r\n\r\n", 505, "unsupported version"},
    };
    for (const Case& c : cases) {
        Request out;
        ParseResult r = parse_request(c.in, &out);
        CHECK_MSG(r.status == ParseStatus::Error,
                  std::string(c.why) + ": got " + ps(r.status) + " for " + tt::quote(c.in));
        CHECK_MSG(r.http_status == c.want,
                  std::string(c.why) + ": got status " + std::to_string(r.http_status) +
                      ", want " + std::to_string(c.want));
    }
}

HX_TEST("3.3", "an over-long request line is 414 and huge headers are 431") {
    {
        std::string req = "GET /" + std::string(kMaxRequestLine + 100, 'a') + " HTTP/1.1\r\n\r\n";
        Request out;
        ParseResult r = parse_request(req, &out);
        CHECK_STR_EQ(ps(r.status), "Error");
        CHECK_EQ(r.http_status, 414);
    }
    {
        std::string req = "GET / HTTP/1.1\r\n";
        while (req.size() < kMaxHeaderBytes + 2000) req += "X-Pad: 0123456789abcdef\r\n";
        req += "\r\n";
        Request out;
        ParseResult r = parse_request(req, &out);
        CHECK_STR_EQ(ps(r.status), "Error");
        CHECK_EQ(r.http_status, 431);
    }
    {
        // Never-ending head with no terminator: must not buffer forever.
        std::string req = "GET / HTTP/1.1\r\n";
        while (req.size() < kMaxRequestLine + kMaxHeaderBytes + 1000)
            req += "X-Pad: 0123456789abcdef\r\n";
        Request out;
        ParseResult r = parse_request(req, &out);
        CHECK_MSG(r.status == ParseStatus::Error,
                  "a head that exceeds every limit without terminating must be an error, "
                  "not Incomplete — otherwise a client can exhaust your memory");
        CHECK_EQ(r.http_status, 431);
    }
}

// ------------------------------------------------------------------- 3.4 ----
HX_TEST("3.4", "parse_content_length accepts digits and nothing else") {
    CHECK(parse_content_length("0").has_value());
    CHECK_EQ((long)*parse_content_length("0"), 0L);
    CHECK_EQ((long)*parse_content_length("7"), 7L);
    CHECK_EQ((long)*parse_content_length("1048576"), 1048576L);
    CHECK_MSG(!parse_content_length("").has_value(), "empty");
    CHECK_MSG(!parse_content_length("+5").has_value(), "sign");
    CHECK_MSG(!parse_content_length("-5").has_value(), "negative");
    CHECK_MSG(!parse_content_length("5 ").has_value(), "trailing space (trim before you get here)");
    CHECK_MSG(!parse_content_length("0x10").has_value(), "hex");
    CHECK_MSG(!parse_content_length("5,5").has_value(), "list");
    CHECK_MSG(!parse_content_length("abc").has_value(), "letters");
    CHECK_MSG(!parse_content_length("99999999999999999999999999").has_value(), "overflow");
}

HX_TEST("3.4", "parse_request waits for the body, then reports it") {
    const std::string head =
        "POST /submit HTTP/1.1\r\nHost: h\r\nContent-Length: 5\r\n\r\n";
    for (size_t n = 0; n < 5; n++) {
        Request out;
        std::string partial = head + std::string("hello").substr(0, n);
        ParseResult r = parse_request(partial, &out);
        CHECK_MSG(r.status == ParseStatus::Incomplete,
                  "with " + std::to_string(n) + " of 5 body bytes we got " + ps(r.status));
    }
    Request out;
    std::string full = head + "hello";
    ParseResult r = parse_request(full, &out);
    CHECK_STR_EQ(ps(r.status), "Complete");
    CHECK_STR_EQ(out.body, "hello");
    CHECK_EQ((long)r.consumed, (long)full.size());

    // A pipelined request after the body must not be swallowed.
    Request out2;
    ParseResult r2 = parse_request(full + "GET /next HTTP/1.1\r\n\r\n", &out2);
    CHECK_STR_EQ(ps(r2.status), "Complete");
    CHECK_EQ((long)r2.consumed, (long)full.size());
    CHECK_STR_EQ(out2.body, "hello");
}

HX_TEST("3.4", "bad, conflicting or oversized Content-Length is rejected") {
    struct Case {
        std::string in;
        int want;
        const char* why;
    } cases[] = {
        {"POST /x HTTP/1.1\r\nContent-Length: abc\r\n\r\n", 400, "not a number"},
        {"POST /x HTTP/1.1\r\nContent-Length: \r\n\r\n", 400, "empty"},
        {"POST /x HTTP/1.1\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\nhello", 400,
         "two different lengths is a smuggling attempt"},
        {"POST /x HTTP/1.1\r\nContent-Length: 99999999\r\n\r\n", 413, "over kMaxBodyBytes"},
    };
    for (const Case& c : cases) {
        Request out;
        ParseResult r = parse_request(c.in, &out);
        CHECK_MSG(r.status == ParseStatus::Error, std::string(c.why) + ": got " + ps(r.status));
        CHECK_MSG(r.http_status == c.want,
                  std::string(c.why) + ": got " + std::to_string(r.http_status) + ", want " +
                      std::to_string(c.want));
    }
}

// ------------------------------------------------------------------- 3.5 ----
HX_TEST("3.5", "reason_phrase knows the codes this server can emit") {
    CHECK_STR_EQ(reason_phrase(200), "OK");
    CHECK_STR_EQ(reason_phrase(400), "Bad Request");
    CHECK_STR_EQ(reason_phrase(404), "Not Found");
    CHECK_STR_EQ(reason_phrase(405), "Method Not Allowed");
    CHECK_STR_EQ(reason_phrase(408), "Request Timeout");
    CHECK_STR_EQ(reason_phrase(413), "Content Too Large");
    CHECK_STR_EQ(reason_phrase(414), "URI Too Long");
    CHECK_STR_EQ(reason_phrase(431), "Request Header Fields Too Large");
    CHECK_STR_EQ(reason_phrase(500), "Internal Server Error");
    CHECK_STR_EQ(reason_phrase(501), "Not Implemented");
    CHECK_STR_EQ(reason_phrase(505), "HTTP Version Not Supported");
    CHECK_STR_EQ(reason_phrase(499), "Unknown");
}

HX_TEST("3.5", "build_response emits exactly the right bytes") {
    CHECK_STR_EQ(build_response(200, {{"Content-Length", "5"}, {"Content-Type", "text/plain"}},
                                "hello"),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Length: 5\r\n"
                 "Content-Type: text/plain\r\n"
                 "\r\n"
                 "hello");
    CHECK_STR_EQ(build_response(404, {{"Content-Length", "0"}}, ""),
                 "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    CHECK_STR_EQ(build_response(200, {}, ""), "HTTP/1.1 200 OK\r\n\r\n");
    // Binary-safe: the body is copied verbatim, NUL bytes and all.
    std::string body("a\0b", 3);
    CHECK_STR_EQ(build_response(200, {}, body), std::string("HTTP/1.1 200 OK\r\n\r\na\0b", 22));
}

HX_TEST("3.5", "http_date formats IMF-fixdate in GMT") {
    CHECK_STR_EQ(http_date(0), "Thu, 01 Jan 1970 00:00:00 GMT");
    CHECK_STR_EQ(http_date(1234567890), "Fri, 13 Feb 2009 23:31:30 GMT");
    CHECK_STR_EQ(http_date(1000000000), "Sun, 09 Sep 2001 01:46:40 GMT");
    CHECK_STR_EQ(http_date(2147483647), "Tue, 19 Jan 2038 03:14:07 GMT");
}

// ------------------------------------------------------------------- 3.6 ----
HX_TEST("3.6", "wants_keep_alive follows the version default, then the header") {
    auto req = [](const char* ver, const char* conn) {
        Request r;
        r.version = ver;
        if (conn) r.headers.push_back({"Connection", conn});
        return r;
    };
    CHECK_MSG(wants_keep_alive(req("HTTP/1.1", nullptr)), "1.1 is persistent by default");
    CHECK_MSG(!wants_keep_alive(req("HTTP/1.0", nullptr)), "1.0 closes by default");
    CHECK_MSG(!wants_keep_alive(req("HTTP/1.1", "close")), "explicit close");
    CHECK_MSG(!wants_keep_alive(req("HTTP/1.1", "Close")), "case-insensitive");
    CHECK_MSG(wants_keep_alive(req("HTTP/1.0", "keep-alive")), "1.0 opt-in");
    CHECK_MSG(wants_keep_alive(req("HTTP/1.0", "Keep-Alive")), "case-insensitive opt-in");
    CHECK_MSG(!wants_keep_alive(req("HTTP/1.1", "keep-alive, close")), "close wins in a list");
    CHECK_MSG(!wants_keep_alive(req("HTTP/1.1", "TE, close")), "token list with close");
    CHECK_MSG(wants_keep_alive(req("HTTP/1.1", "TE")), "unrelated token changes nothing");
    CHECK_MSG(wants_keep_alive(req("HTTP/1.1", "closely")), "match whole tokens, not substrings");
}

// ------------------------------------------------------------------- 3.7 ----
HX_TEST("3.7", "resolve_path maps safe targets under the docroot") {
    struct Case {
        const char* target;
        const char* want;
    } cases[] = {
        {"/index.html", "/srv/www/index.html"},
        {"/", "/srv/www/index.html"},
        {"/a/b.txt", "/srv/www/a/b.txt"},
        {"/a/b/../c.txt", "/srv/www/a/c.txt"},
        {"/a//b.txt", "/srv/www/a/b.txt"},
        {"/./x", "/srv/www/x"},
        {"/a/b/c/../../d", "/srv/www/a/d"},
        {"/x?y=1&z=2", "/srv/www/x"},
        {"/%69ndex.html", "/srv/www/index.html"},
        {"/hello%20world.txt", "/srv/www/hello world.txt"},
        {"/a/", "/srv/www/a/index.html"},
        {"/a/..", "/srv/www/index.html"},
    };
    for (const Case& c : cases) {
        auto got = resolve_path("/srv/www", c.target);
        CHECK_MSG(got.has_value(), std::string("rejected a safe target: ") + c.target);
        CHECK_MSG(*got == c.want, std::string(c.target) + " -> " + tt::quote(*got) + ", want " +
                                      tt::quote(c.want));
    }
}

HX_TEST("3.7", "resolve_path refuses to escape the docroot") {
    const char* bad[] = {
        "/../etc/passwd",
        "/a/../../etc/passwd",
        "/%2e%2e/etc/passwd",
        "/%2E%2E%2Fetc/passwd",
        "/..%2fetc/passwd",
        "/a/b/../../../x",
        "index.html",          // no leading slash
        "http://host/x",       // absolute-form target
        "/a%00b",              // embedded NUL
        "/a%zz",               // invalid escape
        "/a%2",                // truncated escape
        "",                    // empty
    };
    for (const char* t : bad) {
        auto got = resolve_path("/srv/www", t);
        CHECK_MSG(!got.has_value(), std::string("accepted a dangerous target: ") + tt::quote(t) +
                                        " -> " + tt::quote(got.value_or("")));
    }
}

HX_TEST("3.7", "mime_type maps the extensions the server serves") {
    CHECK_STR_EQ(mime_type("/a/index.html"), "text/html; charset=utf-8");
    CHECK_STR_EQ(mime_type("/a/index.htm"), "text/html; charset=utf-8");
    CHECK_STR_EQ(mime_type("/a/INDEX.HTML"), "text/html; charset=utf-8");
    CHECK_STR_EQ(mime_type("/style.css"), "text/css");
    CHECK_STR_EQ(mime_type("/app.js"), "text/javascript");
    CHECK_STR_EQ(mime_type("/data.json"), "application/json");
    CHECK_STR_EQ(mime_type("/notes.txt"), "text/plain; charset=utf-8");
    CHECK_STR_EQ(mime_type("/img.png"), "image/png");
    CHECK_STR_EQ(mime_type("/img.jpg"), "image/jpeg");
    CHECK_STR_EQ(mime_type("/img.jpeg"), "image/jpeg");
    CHECK_STR_EQ(mime_type("/logo.svg"), "image/svg+xml");
    CHECK_STR_EQ(mime_type("/archive.tar.gz"), "application/octet-stream");
    CHECK_STR_EQ(mime_type("/no_extension"), "application/octet-stream");
    CHECK_STR_EQ(mime_type("/.bashrc"), "application/octet-stream");
    CHECK_MSG(mime_type("/dir.css/file") == "application/octet-stream",
              "the extension comes from the last path segment, not the whole path");
}

// ------------------------------------------------------------- 3.8 advanced ----
HX_TEST("3.8", "decode_chunked reassembles a chunked body") {
    struct Case {
        std::string in;
        const char* want;
    } cases[] = {
        {"5\r\nhello\r\n0\r\n\r\n", "hello"},
        {"4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n", "Wikipedia"},
        {"0\r\n\r\n", ""},
        {"A\r\n0123456789\r\n0\r\n\r\n", "0123456789"},
        {"a\r\n0123456789\r\n0\r\n\r\n", "0123456789"},
        {"5;name=value\r\nhello\r\n0\r\n\r\n", "hello"},
        {"5\r\nhello\r\n0\r\nX-Checksum: abc\r\n\r\n", "hello"},
    };
    for (const Case& c : cases) {
        std::string out;
        size_t consumed = 0;
        ParseStatus st = decode_chunked(c.in, &out, &consumed);
        CHECK_MSG(st == ParseStatus::Complete,
                  tt::quote(c.in) + " gave " + ps(st) + ", expected Complete");
        CHECK_STR_EQ(out, c.want);
        CHECK_EQ((long)consumed, (long)c.in.size());
    }
}

HX_TEST("3.8", "decode_chunked asks for more bytes when the body is truncated") {
    const char* partials[] = {"", "5", "5\r\n", "5\r\nhel", "5\r\nhello", "5\r\nhello\r\n",
                              "5\r\nhello\r\n0", "5\r\nhello\r\n0\r\n", "5\r\nhello\r\n0\r\n\r"};
    for (const char* p : partials) {
        std::string out;
        size_t consumed = 0;
        ParseStatus st = decode_chunked(p, &out, &consumed);
        CHECK_MSG(st == ParseStatus::Incomplete,
                  std::string("expected Incomplete for ") + tt::quote(p) + ", got " + ps(st));
    }
}

HX_TEST("3.8", "decode_chunked rejects invalid framing") {
    const char* bad[] = {"zz\r\nhello\r\n0\r\n\r\n", "5\r\nhello!!0\r\n\r\n", "-1\r\n\r\n",
                         " 5\r\nhello\r\n0\r\n\r\n"};
    for (const char* b : bad) {
        std::string out;
        size_t consumed = 0;
        ParseStatus st = decode_chunked(b, &out, &consumed);
        CHECK_MSG(st == ParseStatus::Error,
                  std::string("expected Error for ") + tt::quote(b) + ", got " + ps(st));
    }
}
