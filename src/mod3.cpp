// Module 3 — HTTP/1.1 parsing and building.  Theory: docs/03-http.md
//
// Run:  make test MODULE=3
//
// The exact contracts (limits, status codes, formats) are in include/hx/mod3.hpp.
// Read them before you start: the tests check them literally.

#include "hx/mod3.hpp"

#include <string.h>

namespace hx::m3 {

// ------------------------------------------------------------- 3.2 lookup ----
const std::string* Request::find(std::string_view name) const {
    // TODO(3.2): case-insensitive search over `headers`, first match wins.
    (void)name;
    return nullptr;
}

// -------------------------------------------------------------- 3.0 utils ----
bool iequals(std::string_view a, std::string_view b) {
    // TODO(3.0): ASCII-only, length check first. Careful with char signedness
    // if you use ::tolower — cast to unsigned char.
    (void)a;
    (void)b;
    return false;
}

std::string_view trim_ows(std::string_view s) {
    // TODO(3.0): strip leading/trailing ' ' and '\t'. Returns a view into the
    // same buffer — no allocation.
    return s;
}

// ------------------------------------------------------- 3.1 request line ----
bool parse_request_line(std::string_view line, std::string* method, std::string* target,
                        std::string* version) {
    // TODO(3.1): "GET /path HTTP/1.1" -> three tokens split on single spaces.
    // Reject: fewer/more tokens, empty tokens, any byte < 0x20 or == 0x7f.
    (void)line;
    (void)method;
    (void)target;
    (void)version;
    return false;
}

// ------------------------------------------------------------ 3.2 headers ----
bool parse_headers(std::string_view block, std::vector<Header>* out) {
    // TODO(3.2): split on CRLF; for each line, split at the first ':';
    // reject whitespace before the ':' (that is a request-smuggling vector);
    // trim OWS from the value; keep the name's original case.
    // Reject obs-fold (a line starting with ' ' or '\t').
    (void)block;
    (void)out;
    return false;
}

// ----------------------------------------------------- 3.3/3.4 the parser ----
ParseResult parse_request(std::string_view in, Request* out) {
    // TODO(3.3): find "\r\n\r\n". Not there yet?
    //     - if in.size() > kMaxRequestLine + kMaxHeaderBytes -> Error 431
    //     - otherwise Incomplete
    //   Found it? Parse the request line (over kMaxRequestLine -> 414) and the
    //   header block (over kMaxHeaderBytes -> 431).
    // TODO(3.4): then the body.
    //     - Content-Length present and valid -> need that many more bytes;
    //       Incomplete until they arrive; > kMaxBodyBytes -> Error 413
    //     - Content-Length invalid or repeated with different values -> 400
    //     - no Content-Length -> body is empty
    //   Set `consumed` to head + body so a pipelined next request is findable.
    //   Version other than HTTP/1.0 or HTTP/1.1 -> Error 505.
    (void)in;
    (void)out;
    return ParseResult{};
}

std::optional<size_t> parse_content_length(std::string_view v) {
    // TODO(3.4): digits only. "" is invalid. "+5", "5 ", "0x5", "5,5" invalid.
    // Watch for overflow.
    (void)v;
    return std::nullopt;
}

// ---------------------------------------------------------- 3.5 responses ----
std::string_view reason_phrase(int status) {
    // TODO(3.5): at least 200, 400, 404, 405, 408, 413, 414, 431, 500, 501,
    // 505. Default "Unknown".
    (void)status;
    return "Unknown";
}

std::string build_response(int status, const std::vector<Header>& headers,
                           std::string_view body) {
    // TODO(3.5): exact bytes matter — CRLF everywhere, blank line before body.
    (void)status;
    (void)headers;
    (void)body;
    return {};
}

std::string http_date(time_t t) {
    // TODO(3.5): gmtime_r + strftime, or format it yourself. The format is
    // fixed by the RFC and must not depend on the locale — "%a, %d %b %Y
    // %H:%M:%S GMT" under the C locale is fine, doing it by hand is safer.
    (void)t;
    return {};
}

// --------------------------------------------------------- 3.6 keep-alive ----
bool wants_keep_alive(const Request& req) {
    // TODO(3.6): default by version, then let the Connection header override.
    // Split the value on ',' and compare whole tokens.
    (void)req;
    return false;
}

// --------------------------------------------------- 3.7 paths and types ----
std::optional<std::string> resolve_path(std::string_view docroot, std::string_view target) {
    // TODO(3.7): this function is your security boundary. Decode *first*, then
    // normalise, then check — an attacker will send "%2e%2e%2f".
    // Every rejected case in the tests is a real CVE somebody shipped.
    (void)docroot;
    (void)target;
    return std::nullopt;
}

std::string_view mime_type(std::string_view path) {
    // TODO(3.7): match on the last '.' in the final path segment.
    (void)path;
    return "application/octet-stream";
}

// -------------------------------------------------------- 3.8 (advanced) ----
ParseStatus decode_chunked(std::string_view in, std::string* out, size_t* consumed) {
    // TODO(3.8): loop: hex size line (ignore ";extensions"), then that many
    // bytes, then CRLF. Size 0 ends the body; consume optional trailers and
    // the final CRLF.
    (void)in;
    (void)out;
    (void)consumed;
    return ParseStatus::Error;
}

}  // namespace hx::m3
