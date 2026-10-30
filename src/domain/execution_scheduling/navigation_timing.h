#pragma once
#include <cstdint>
#include <string>
#include <optional>
namespace ruyijs::domain::execution_scheduling {
// One document's navigation record (docs/owners/NAVIGATION_TIMING.md).
// `time_origin` is an epoch millisecond timestamp: performance.timeOrigin and
// the legacy timing.navigationStart. Every other time is a millisecond offset
// from it, as PerformanceNavigationTiming reports; 0 means "not reached yet".
struct NavigationTiming {
  bool present = false;
  double time_origin = 0;
  double request_start = 0;
  std::optional<double> fetch_start;
  // Connection establishment (offsets, already resolved to the earlier phase
  // for a reused connection); secure_start is 0 without TLS.
  double dns_start = 0, dns_end = 0, connect_start = 0, connect_end = 0, secure_start = 0;
  bool establishment = false;  // set by a measured navigation
  // CacheablePerformanceTimingData::CheckTimingAllowedForOrigin and
  // CheckBodyInfoAccessAllowedForOrigin: a top-level document load passes
  // both; a frame document is checked against its parent's origin (the
  // loading principal): same origin, or Timing-Allow-Origin for timing. A
  // navigation has no CORS, so a cross-origin frame never gets body info.
  bool timing_allowed = true, body_allowed = true;
  double response_start = 0;
  double response_end = 0;
  double dom_interactive = 0;
  double dom_content_loaded_start = 0;
  double dom_content_loaded_end = 0;
  double dom_complete = 0;
  double load_event_start = 0;
  double load_event_end = 0;
  bool load_event_finished = false;
  std::optional<double> recorded_load_event_end;
  uint16_t redirect_count = 0;
  uint16_t status = 0;
  uint64_t encoded_size = 0, decoded_size = 0;
  std::optional<uint64_t> transfer_size;
  // The committed document URL (the entry name), the response protocol and
  // essence MIME type, as the inherited PerformanceResourceTiming fields report.
  std::string name, protocol, content_type;
  // "navigate", "reload" or "back_forward".
  const char* type = "navigate";
};
}
