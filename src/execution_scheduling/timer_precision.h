#pragma once
#include "domain/execution_scheduling/timer_precision.h"
#include <cstdint>

namespace ruyijs::execution_scheduling {
// This process's midpoint secret, drawn once.
const domain::execution_scheduling::TimerSecret& ProcessTimerSecret();
// A fresh timeline seed (one per Performance owner, i.e. per document).
int64_t NewTimelineSeed();
// nsRFPService::ReduceTimePrecisionAsMSecs for content (Normal precision,
// 1000 us, jitter): `mixin` is the timeline seed, 0 for an epoch timestamp.
double ReduceContentTime(double ms, int64_t mixin);
}  // namespace ruyijs::execution_scheduling
