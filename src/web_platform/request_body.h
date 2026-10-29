#ifndef RUYIJS_WEB_PLATFORM_REQUEST_BODY_H
#define RUYIJS_WEB_PLATFORM_REQUEST_BODY_H
#include <string>
#include "js/RootingAPI.h"

namespace ruyijs::web_platform {
inline constexpr size_t kRequestBodyLimit = 1024 * 1024;
// Internal read port (Fetch "extract a body"). Owns a bounded snapshot, never a
// second platform object; the caller sets `limit` before reading (sendBeacon
// and forms keep 1 MiB, fetch()/XHR pass their own bound).
struct RequestBody {
  std::string bytes, contentType;
  bool tooLarge = false;
  size_t limit = kRequestBodyLimit;
};
bool ReadRequestBody(JSContext*, JS::HandleValue, RequestBody*);
bool ReadFormRequestBody(JSContext*, JS::HandleObject, RequestBody*);
bool ReadRequestText(JSContext*, JS::HandleValue, std::string*);
std::string NormalizeFormLines(const std::string&);
}
#endif
