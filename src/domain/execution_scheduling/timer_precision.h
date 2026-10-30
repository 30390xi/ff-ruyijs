/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
// Firefox 159 timer precision reduction, ported from
// toolkit/components/resistfingerprinting/nsRFPService.cpp
// (RandomMidpoint, ReduceTimePrecisionImpl) and mfbt/XorShift128PlusRNG.h.
//
// Defaults are Firefox 159's: privacy.reduceTimerPrecision = true,
// privacy.resistFingerprinting.reduceTimerPrecision.microseconds = 1000 and
// .jitter = true, so content timestamps (TimerPrecisionType::Normal) clamp to
// 1 ms with a pseudo-random rounding midpoint inside each millisecond.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace ruyijs::domain::execution_scheduling {
enum class TimeScale : int64_t { Seconds = 1, MilliSeconds = 1000, MicroSeconds = 1000000 };
enum class TimerPrecision { DangerouslyNone, Normal, UnconditionalHighRes };

inline constexpr double kTimerResolutionUSec = 1000;     // ...reduceTimerPrecision.microseconds
inline constexpr double kUnconditionalUSec = 20;         // RFP_TIMER_UNCONDITIONAL_VALUE

// mozilla::non_crypto::XorShift128PlusRNG.
class XorShift128Plus {
 public:
  XorShift128Plus(uint64_t a, uint64_t b) : state_{a, b} {}
  uint64_t next() {
    uint64_t s1 = state_[0];
    const uint64_t s0 = state_[1];
    state_[0] = s0;
    s1 ^= s1 << 23;
    state_[1] = s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26);
    return state_[1] + s0;
  }
 private:
  uint64_t state_[2];
};

// The per-process 16-byte secret (sSecretMidpointSeed). Firefox fills it once
// from the platform random generator; the owner sets it at process start.
struct TimerSecret { uint8_t bytes[16] = {}; };

// nsRFPService::RandomMidpoint: a midpoint in [0, resolution) from the
// secret, the timeline's context mix-in and the clamped time. Returns false
// for a non-positive resolution (Firefox: NS_ERROR_FAILURE, no jitter).
inline bool RandomMidpoint(const TimerSecret& secret, long long clampedUSec,
                           long long resolutionUSec, int64_t mixin, long long* midpoint) {
  uint64_t low = 0, high = 0;
  std::memcpy(&low, secret.bytes, 8);
  std::memcpy(&high, secret.bytes + 8, 8);
  XorShift128Plus rng(uint64_t(mixin) ^ low, uint64_t(clampedUSec) ^ high);
  if (resolutionUSec <= 0) return false;
  // `rng.next() % aResolutionUSec` with uint64_t % long long: the usual
  // arithmetic conversions make it an unsigned 64-bit remainder.
  *midpoint = (long long)(rng.next() % uint64_t(resolutionUSec));
  return true;
}

// nsRFPService::ReduceTimePrecisionImpl. `mixin` is the timeline's random seed
// for a relative timestamp (RelativeTimeline::GetRandomTimelineSeed), 0 for
// an absolute (epoch) timestamp.
inline double ReduceTimePrecision(const TimerSecret& secret, double time, TimeScale scale,
                                  double resolutionUSec, int64_t mixin,
                                  TimerPrecision type = TimerPrecision::Normal,
                                  bool jitter = true) {
  if (type == TimerPrecision::DangerouslyNone) return time;
  bool unconditional = false;
  if (type == TimerPrecision::UnconditionalHighRes || resolutionUSec <= 0) {
    unconditional = true;
    resolutionUSec = kUnconditionalUSec;
    mixin = 0;
  }
  const double timeScaled = time * double(1000000 / int64_t(scale));
  const long long timeAsInt = (long long)timeScaled;  // cut below a microsecond
  const long long resolution = (long long)resolutionUSec;
  const long long clamped = (long long)(std::floor(double(timeAsInt) / resolution) * resolution);
  long long jittered = clamped, midpoint = 0;
  if (!unconditional && jitter &&
      RandomMidpoint(secret, clamped, resolution, mixin, &midpoint) &&
      timeAsInt >= clamped + midpoint)
    jittered += resolution;
  return double(jittered) / (1000000.0 / double(int64_t(scale)));
}
}  // namespace ruyijs::domain::execution_scheduling
