#pragma once
// CORS request rules (Fetch "CORS protocol", "CORS-preflight fetch", "CORS check")
// and the request field order Firefox 159 puts on the wire; pure values only.
#include "domain/resource_network/header_rules.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ruyijs::domain::resource_network {
using CorsFields = std::vector<std::pair<std::string, std::string>>;

inline bool cors_safelisted_method(std::string_view method) noexcept {
  return method == "GET" || method == "HEAD" || method == "POST";
}
inline bool cors_unsafe_request_byte(unsigned char c) noexcept {
  if (c < 0x20) return c != '\t';
  switch (c) {
    case '"': case '(': case ')': case ':': case '<': case '>': case '?': case '@':
    case '[': case '\\': case ']': case '{': case '}': case 0x7f: return true;
    default: return false;
  }
}
inline bool cors_language_value(std::string_view value) noexcept {
  for (unsigned char c : value)
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          c == ' ' || c == '*' || c == ',' || c == '-' || c == '.' || c == ';' || c == '='))
      return false;
  return true;
}
// "bytes=N-" or "bytes=N-M" with N <= M (Fetch "simple range header value").
inline bool cors_simple_range(std::string_view value) noexcept {
  if (value.substr(0, 6) != "bytes=") return false;
  value.remove_prefix(6);
  auto digits = [&](std::string& out) {
    while (!value.empty() && value.front() >= '0' && value.front() <= '9') {
      out += value.front(); value.remove_prefix(1);
    }
  };
  std::string first, last;
  digits(first);
  if (first.empty() || value.empty() || value.front() != '-') return false;
  value.remove_prefix(1);
  digits(last);
  if (!value.empty()) return false;
  if (last.empty()) return true;
  const auto start = first.find_first_not_of('0'), end = last.find_first_not_of('0');
  const std::string a = start == std::string::npos ? "0" : first.substr(start);
  const std::string b = end == std::string::npos ? "0" : last.substr(end);
  return a.size() != b.size() ? a.size() < b.size() : a <= b;
}
inline std::string mime_essence(std::string_view value) {
  const auto end = value.find(';');
  return lower_header_name(trim_header_value(value.substr(0, end)));
}
inline bool cors_safelisted_request_header(std::string_view name, std::string_view value) {
  if (value.size() > 128) return false;
  const std::string lower = lower_header_name(name);
  if (lower == "accept") {
    return std::none_of(value.begin(), value.end(),
                        [](char c) { return cors_unsafe_request_byte(static_cast<unsigned char>(c)); });
  }
  if (lower == "accept-language" || lower == "content-language") return cors_language_value(value);
  if (lower == "content-type") {
    if (std::any_of(value.begin(), value.end(),
                    [](char c) { return cors_unsafe_request_byte(static_cast<unsigned char>(c)); }))
      return false;
    const std::string essence = mime_essence(value);
    return essence == "application/x-www-form-urlencoded" || essence == "multipart/form-data" ||
           essence == "text/plain";
  }
  if (lower == "range") return cors_simple_range(value);
  return false;
}
// Sorted, lower-cased, unique names that need a preflight (Fetch "CORS-unsafe
// request-header names"); Access-Control-Request-Headers joins them with ",".
inline std::vector<std::string> cors_unsafe_request_header_names(const CorsFields& headers) {
  std::vector<std::string> unsafe, safelisted;
  size_t size = 0;
  for (const auto& [name, value] : headers) {
    if (cors_safelisted_request_header(name, value)) {
      safelisted.push_back(lower_header_name(name));
      size += value.size();
    } else {
      unsafe.push_back(lower_header_name(name));
    }
  }
  if (size > 1024) unsafe.insert(unsafe.end(), safelisted.begin(), safelisted.end());
  std::sort(unsafe.begin(), unsafe.end());
  unsafe.erase(std::unique(unsafe.begin(), unsafe.end()), unsafe.end());
  return unsafe;
}
inline const std::string* cors_field(const CorsFields& headers, std::string_view name) {
  for (const auto& field : headers)
    if (lower_header_name(field.first) == name) return &field.second;
  return nullptr;
}
// Fetch "CORS check": one Access-Control-Allow-Origin naming the request's
// origin, or "*" without credentials; credentials need ACAC exactly "true".
inline bool cors_check(const CorsFields& response, std::string_view origin, bool credentials) {
  const std::string* allow = nullptr;
  for (const auto& field : response)
    if (lower_header_name(field.first) == "access-control-allow-origin") {
      if (allow) return false;
      allow = &field.second;
    }
  if (!allow) return false;
  const std::string_view value = trim_header_value(*allow);
  if (!credentials) return value == "*" || value == origin;
  if (value != origin) return false;
  const std::string* allowCredentials = cors_field(response, "access-control-allow-credentials");
  return allowCredentials && trim_header_value(*allowCredentials) == "true";
}
inline std::vector<std::string> cors_tokens(const std::string* value) {
  std::vector<std::string> tokens;
  if (!value) return tokens;
  std::string_view rest = *value;
  while (true) {
    const auto comma = rest.find(',');
    const std::string_view token = trim_header_value(rest.substr(0, comma));
    if (!token.empty()) tokens.emplace_back(token);
    if (comma == std::string_view::npos) break;
    rest.remove_prefix(comma + 1);
  }
  return tokens;
}
// One successful preflight's grant (Fetch "CORS-preflight fetch" steps 7.3-7.7).
struct CorsPreflightGrant {
  std::vector<std::string> methods, headers;  // as listed; header names lower-cased
  bool anyMethod = false, anyHeader = false;
  // Seconds the grant may be cached; 0 means "do not cache". Firefox caches
  // 5 s without Access-Control-Max-Age, at most 24 h, and nothing for a value
  // that is not plain digits (nsCORSPreflightListener::AddResultToCache).
  double maxAgeSeconds = 5;
};
inline bool cors_preflight_grant(int status, const CorsFields& response, std::string_view origin,
                                 bool credentials, CorsPreflightGrant* grant) {
  if (status < 200 || status > 299 || !cors_check(response, origin, credentials)) return false;
  *grant = {};
  for (auto& method : cors_tokens(cors_field(response, "access-control-allow-methods"))) {
    if (method == "*" && !credentials) grant->anyMethod = true;
    else grant->methods.push_back(std::move(method));
  }
  for (auto& name : cors_tokens(cors_field(response, "access-control-allow-headers"))) {
    if (name == "*" && !credentials) grant->anyHeader = true;
    else grant->headers.push_back(lower_header_name(name));
  }
  if (const std::string* age = cors_field(response, "access-control-max-age")) {
    double seconds = 0;
    for (char c : *age) {
      if (c < '0' || c > '9') { seconds = 0; break; }
      seconds = std::min(86400.0, seconds * 10 + (c - '0'));
    }
    grant->maxAgeSeconds = age->empty() ? 5 : seconds;
  }
  return true;
}
inline bool cors_grant_allows(const CorsPreflightGrant& grant, std::string_view method,
                              const std::vector<std::string>& unsafeNames) {
  const bool methodOk = cors_safelisted_method(method) || grant.anyMethod ||
      std::find(grant.methods.begin(), grant.methods.end(), method) != grant.methods.end();
  if (!methodOk) return false;
  for (const auto& name : unsafeNames) {
    // "*" never covers Authorization (Fetch, CORS-preflight fetch step 7.6).
    if (grant.anyHeader && name != "authorization") continue;
    if (std::find(grant.headers.begin(), grant.headers.end(), name) == grant.headers.end()) return false;
  }
  return true;
}
// Firefox CORSCacheEntry: each method and header name carries its own expiry,
// refreshed by every later preflight to the same key. Wildcards are not
// cached (the cache stores the literal tokens); GET and POST need no method
// token (CheckRequest).
struct CorsPreflightEntry {
  std::vector<std::pair<std::string, double>> methods, headers;  // token, expiry
  void Add(const CorsPreflightGrant& grant, double now) {
    if (grant.maxAgeSeconds <= 0) return;
    const double expires = now + grant.maxAgeSeconds;
    auto put = [&](std::vector<std::pair<std::string, double>>& list, const std::string& token) {
      for (auto& entry : list) if (entry.first == token) { entry.second = expires; return; }
      list.emplace_back(token, expires);
    };
    for (const auto& method : grant.methods) put(methods, method);
    if (grant.anyMethod) put(methods, "*");
    for (const auto& name : grant.headers) put(headers, name);
    if (grant.anyHeader) put(headers, "*");
  }
  bool Allows(std::string_view method, const std::vector<std::string>& unsafeNames, double now) {
    auto live = [&](std::vector<std::pair<std::string, double>>& list) {
      list.erase(std::remove_if(list.begin(), list.end(), [&](const auto& e) { return e.second <= now; }),
                 list.end());
    };
    live(methods); live(headers);
    auto has = [](const std::vector<std::pair<std::string, double>>& list, std::string_view token) {
      for (const auto& entry : list) if (entry.first == token) return true;
      return false;
    };
    if (method != "GET" && method != "POST" && !has(methods, method)) return false;
    for (const auto& name : unsafeNames)
      if (!has(headers, name)) return false;
    return true;
  }
};
inline bool potentially_trustworthy_url(std::string_view url) {
  if (url.substr(0, 8) == "https://" || url.substr(0, 6) == "wss://") return true;
  for (std::string_view host : {"http://127.0.0.1", "http://localhost", "http://[::1]"})
    if (url.substr(0, host.size()) == host &&
        (url.size() == host.size() || url[host.size()] == ':' || url[host.size()] == '/'))
      return true;
  return false;
}
// Fetch "append a request Origin header": the serialized origin, or "null"
// where the referrer policy withholds it from a non-GET/HEAD request.
inline std::string cors_origin_value(std::string_view origin, std::string_view target,
                                     std::string_view targetOrigin, std::string_view method,
                                     std::string_view policy, bool corsTainting) {
  if (corsTainting || method == "GET" || method == "HEAD") return std::string(origin);
  if (policy == "no-referrer") return "null";
  if ((policy == "no-referrer-when-downgrade" || policy == "strict-origin" ||
       policy == "strict-origin-when-cross-origin" || policy.empty()) &&
      origin.substr(0, 8) == "https://" && !potentially_trustworthy_url(target))
    return "null";
  if (policy == "same-origin" && origin != targetOrigin) return "null";
  return std::string(origin);
}

// Firefox resolves a known field name to its nsHttpAtom spelling
// (netwerk/protocol/http/nsHttpAtomList.inc, Firefox 159); any other name keeps
// the spelling the page gave.
inline std::string canonical_header_name(std::string_view name) {
  static constexpr std::string_view kAtoms[] = {
    "Accept", "Accept-Encoding", "Accept-Language", "Accept-Ranges", "Access-Control-Allow-Origin",
    "Age", "Allow", "Alt-Svc", "Alt-Used", "Assoc-Req", "Authentication", "Authorization",
    "Available-Dictionary", "Cache-Control", "Clear-Site-Data", "Connection",
    "Content-Disposition", "Content-Encoding", "Content-Language", "Content-Length",
    "Content-Location", "Content-MD5", "Content-Range", "Content-Security-Policy", "Content-Type",
    "Cookie", "Cross-Origin-Embedder-Policy", "Cross-Origin-Opener-Policy",
    "Cross-Origin-Resource-Policy", "Date", "DAV", "Depth", "Destination", "Dictionary-Id", "DNT",
    "Etag", "Expect", "Expires", "From", "Sec-GPC", "Host", "Idempotency-Key", "If", "If-Match",
    "If-Modified-Since", "If-None-Match", "If-None-Match-Any", "If-Range", "If-Unmodified-Since",
    "Integrity-Policy-WAICT-v1", "Keep-Alive", "Last-Modified", "Lock-Token", "Link", "Location",
    "Max-Forwards", "NEL", "No-Vary-Search", "Origin", "Origin-Agent-Cluster", "Origin-Trial",
    "Overwrite", "Pragma", "Prefer", "Proxy-Authenticate", "Proxy-Authorization",
    "Proxy-Connection", "Range", "Referer", "Referrer-Policy", "Retry-After",
    "Sec-Fetch-Storage-Access", "Sec-Purpose", "Server", "Server-Timing", "Service-Worker-Allowed",
    "Set-Cookie", "Set-Login", "Status-URI", "Strict-Transport-Security", "TE", "Title", "Timeout",
    "Trailer", "Transfer-Encoding", "URI", "Upgrade", "User-Agent", "Use-As-Dictionary", "Vary",
    "Version", "WWW-Authenticate", "Warning", "X-Content-Type-Options", "X-Firefox-Spdy",
    "X-Firefox-Spdy-Proxy", "X-Firefox-Early-Data", "X-Firefox-Http3", "X-Frame-Options",
  };
  const std::string lower = lower_header_name(name);
  for (auto atom : kAtoms)
    if (lower_header_name(atom) == lower) return std::string(atom);
  return std::string(name);
}
// Where Firefox places Referer relative to the fields its channel adds itself.
enum class RefererSlot {
  BeforePage,    // fetch(): the referrer is set before the Request header list
  BeforeOrigin,  // CORS preflight, non-GET navigations
  AfterOrigin,   // a redirected subresource hop: Origin, Referer, Connection
  AfterConnection,  // XHR, sendBeacon and GET subresources/navigations
};
// Firefox 159 request field order (measured on the wire against Firefox 159.0):
// the five standard fields, the page's fields in their order with
// Content-Length right after Content-Type, Sec-Fetch-Storage-Access, Origin,
// Connection, Referer in its slot, then Cookie, Alt-Used, fetch metadata and
// Priority. The transport keeps this order; it only adds Host and HTTP/2's TE.
inline void order_request_fields(CorsFields& headers, RefererSlot slot) {
  static constexpr std::string_view kStandard[] = {"host", "user-agent", "accept", "accept-language",
                                                   "accept-encoding"};
  static constexpr std::string_view kPlaced[] = {"host", "user-agent", "accept", "accept-language",
      "accept-encoding", "referer", "content-length", "sec-fetch-storage-access", "origin",
      "connection", "cookie", "alt-used", "upgrade-insecure-requests", "sec-fetch-dest",
      "sec-fetch-mode", "sec-fetch-site", "sec-fetch-user", "priority", "pragma",
      "if-modified-since", "if-none-match", "cache-control"};
  CorsFields ordered;
  std::vector<bool> used(headers.size(), false);
  auto take = [&](std::string_view name) {
    for (size_t i = 0; i < headers.size(); ++i)
      if (!used[i] && lower_header_name(headers[i].first) == name) {
        used[i] = true;
        ordered.push_back(std::move(headers[i]));
      }
  };
  for (auto name : kStandard) take(name);
  if (slot == RefererSlot::BeforePage) take("referer");
  bool length = false;
  for (size_t i = 0; i < headers.size(); ++i) {
    if (used[i]) continue;
    const std::string name = lower_header_name(headers[i].first);
    if (std::find(std::begin(kPlaced), std::end(kPlaced), name) != std::end(kPlaced)) continue;
    used[i] = true;
    ordered.push_back(std::move(headers[i]));
    if (name == "content-type") { take("content-length"); length = true; }
  }
  if (!length) take("content-length");
  if (slot == RefererSlot::BeforeOrigin) take("referer");
  take("sec-fetch-storage-access");
  take("origin");
  if (slot == RefererSlot::AfterOrigin) take("referer");
  take("connection");
  take("referer");
  for (auto name : {"cookie", "alt-used", "upgrade-insecure-requests", "sec-fetch-dest", "sec-fetch-mode",
                    "sec-fetch-site", "sec-fetch-user", "priority", "pragma", "if-modified-since",
                    "if-none-match", "cache-control"})
    take(name);
  for (size_t i = 0; i < headers.size(); ++i)
    if (!used[i]) ordered.push_back(std::move(headers[i]));
  for (auto& field : ordered) field.first = canonical_header_name(field.first);
  headers = std::move(ordered);
}
}
