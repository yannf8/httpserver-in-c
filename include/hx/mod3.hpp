// Module 3 — Speaking HTTP/1.1 (parsing and building, no sockets in here).
// Theory: docs/03-http.md
//
// Everything in this module is pure: bytes in, structs out. That is deliberate.
// A parser you can unit test without a network is a parser you can fuzz, and a
// parser you can fuzz is the only kind you should expose to the internet.
#pragma once

#include <time.h>

#include <optional>

#include "hx/common.hpp"

namespace hx::m3 {

// ---------------------------------------------------------------- limits ----
// Exceeding these is a protocol error with the given status code.
inline constexpr size_t kMaxRequestLine = 8192;    // -> 414 URI Too Long
inline constexpr size_t kMaxHeaderBytes = 16384;   // -> 431 Request Header Fields Too Large
inline constexpr size_t kMaxBodyBytes = 1u << 20;  // -> 413 Content Too Large

// ----------------------------------------------------------------- types ----
struct Header {
    std::string name;   // as received, original case
    std::string value;  // leading/trailing OWS stripped
};

struct Request {
    std::string method;   // "GET"
    std::string target;   // "/index.html?q=1" — raw, still percent-encoded
    std::string version;  // "HTTP/1.1"
    std::vector<Header> headers;
    std::string body;

    // 3.2 — case-insensitive lookup of the first header with this name.
    // Returns nullptr when absent.
    const std::string* find(std::string_view name) const;
};

enum class ParseStatus {
    Incomplete,  // need more bytes, nothing is wrong yet
    Complete,    // a whole request (head + body) is in the buffer
    Error,       // malformed or over a limit; respond and close
};

struct ParseResult {
    ParseStatus status = ParseStatus::Incomplete;
    size_t consumed = 0;    // bytes of input this request used (only when Complete)
    int http_status = 0;    // suggested response code when status == Error
};

// ------------------------------------------------------------- 3.0 utils ----
// ASCII case-insensitive comparison. No locale, no <cctype> surprises with
// negative chars.
bool iequals(std::string_view a, std::string_view b);
// Strip leading and trailing spaces and horizontal tabs (HTTP calls this OWS).
std::string_view trim_ows(std::string_view s);

// ------------------------------------------------------ 3.1 request line ----
// `line` excludes the terminating CRLF. Exactly three space-separated tokens.
// Returns false if malformed (wrong token count, empty token, stray control
// characters). Does not validate the method name or version number — that is
// the caller's job, and the reason is in docs/03.
bool parse_request_line(std::string_view line, std::string* method, std::string* target,
                        std::string* version);

// ----------------------------------------------------------- 3.2 headers ----
// `block` is zero or more "Name: value\r\n" lines — i.e. the header section
// *without* the final empty line. Returns false on: missing colon, whitespace
// between the name and the colon, an empty name, or an obs-fold continuation
// line (a line starting with space/tab).
bool parse_headers(std::string_view block, std::vector<Header>* out);

// ---------------------------------------------------- 3.3/3.4 the parser ----
// The one function the server actually calls. Incremental and restartable:
// call it with everything you have buffered; if it returns Incomplete, read
// more bytes and call it again with the longer buffer (starting from the same
// place). It must never read past `in`.
//
// On Complete: *out is filled and `consumed` is the exact byte count of this
// request, so the caller can erase that prefix and find a pipelined request
// after it.
// On Error: `http_status` is the code to reply with — 400 (malformed),
// 413 (body over kMaxBodyBytes), 414 (request line over kMaxRequestLine),
// 431 (header block over kMaxHeaderBytes, or a head that never terminates),
// 505 (version other than HTTP/1.0 or HTTP/1.1).
// The parser does NOT judge methods: replying 405 or 501 to BREW is the
// server's policy decision, made in module 4.
ParseResult parse_request(std::string_view in, Request* out);

// 3.4 — Parse a Content-Length field value. Digits only, no sign, no spaces
// inside, must fit in size_t. Returns nullopt if invalid.
std::optional<size_t> parse_content_length(std::string_view v);

// --------------------------------------------------------- 3.5 responses ----
// "OK", "Not Found", ... Returns "Unknown" for codes you have not listed.
std::string_view reason_phrase(int status);

// Build a complete response head + body:
//   "HTTP/1.1 <code> <reason>\r\n" then "Name: value\r\n" per header,
//   then "\r\n", then the body verbatim.
// Adds no headers of its own — the caller decides. No trailing newline games.
std::string build_response(int status, const std::vector<Header>& headers,
                           std::string_view body);

// IMF-fixdate in GMT, e.g. "Thu, 01 Jan 1970 00:00:00 GMT".
// English day/month names regardless of locale.
std::string http_date(time_t t);

// -------------------------------------------------------- 3.6 keep-alive ----
// HTTP/1.1: persistent unless "Connection: close".
// HTTP/1.0: closed unless "Connection: keep-alive".
// The Connection value is a comma-separated token list; match tokens
// case-insensitively, not substrings.
bool wants_keep_alive(const Request& req);

// -------------------------------------------------- 3.7 paths and types ----
// Map a request target onto a file path under `docroot` (given without a
// trailing slash), or nullopt if the request must be rejected.
// Steps: cut at '?'; require a leading '/'; percent-decode; reject NUL bytes
// and invalid escapes; collapse "//"; resolve "." and ".." segments; reject
// anything that climbs above the root; if the result ends in '/', append
// "index.html". Result is docroot + normalised path.
std::optional<std::string> resolve_path(std::string_view docroot, std::string_view target);

// Extension -> Content-Type. Case-insensitive extension match.
// .html/.htm  text/html; charset=utf-8      .txt   text/plain; charset=utf-8
// .css        text/css                      .js    text/javascript
// .json       application/json              .png   image/png
// .jpg/.jpeg  image/jpeg                    .svg   image/svg+xml
// anything else: application/octet-stream
std::string_view mime_type(std::string_view path);

// ------------------------------------------------------- 3.8 (advanced) ----
// Decode a chunked body. On Complete, *out holds the decoded bytes and
// *consumed is the length of the chunked framing consumed (including the final
// "0\r\n\r\n"). Chunk extensions after the size (";name=v") are ignored.
// Trailers, if present, are consumed and discarded.
ParseStatus decode_chunked(std::string_view in, std::string* out, size_t* consumed);

}  // namespace hx::m3
