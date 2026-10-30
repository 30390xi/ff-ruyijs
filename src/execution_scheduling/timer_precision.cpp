// Content timer precision (domain/execution_scheduling/timer_precision.h):
// the process secret (nsRFPService's sSecretMidpointSeed) and per-timeline
// seeds (RelativeTimeline::GetRandomTimelineSeed), both from the platform
// random generator as Firefox draws them.
#include "execution_scheduling/timer_precision.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <cstdlib>
#include <mutex>
#pragma comment(lib, "bcrypt.lib")

namespace ruyijs::execution_scheduling {
namespace {
bool Random(void* out, ULONG size) {
  return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, static_cast<PUCHAR>(out), size,
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}
}  // namespace
const domain::execution_scheduling::TimerSecret& ProcessTimerSecret() {
  static domain::execution_scheduling::TimerSecret secret;
  static std::once_flag once;
  std::call_once(once, [] {
    if (!Random(secret.bytes, sizeof secret.bytes))
      for (auto& byte : secret.bytes) byte = uint8_t(std::rand());
  });
  return secret;
}
int64_t NewTimelineSeed() {
  int64_t seed = 0;
  // RelativeTimeline keeps 0 as "not drawn yet"; it falls back to rand().
  if (!Random(&seed, sizeof seed) || seed == 0) seed = std::rand();
  return seed;
}
double ReduceContentTime(double ms, int64_t mixin) {
  using namespace domain::execution_scheduling;
  return ReduceTimePrecision(ProcessTimerSecret(), ms, TimeScale::MilliSeconds,
                             kTimerResolutionUSec, mixin);
}
}  // namespace ruyijs::execution_scheduling
