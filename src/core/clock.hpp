#pragma once

#include <atomic>
#include <chrono>

#include "core/types.hpp"

namespace dronebeat {

// Abstract time source so liveness logic can be tested deterministically.
class Clock {
 public:
  virtual ~Clock() = default;
  virtual TimestampMs NowMs() const = 0;
};

class SystemClock final : public Clock {
 public:
  TimestampMs NowMs() const override {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  }
};

// Manually advanced clock for tests.
class FakeClock final : public Clock {
 public:
  explicit FakeClock(TimestampMs start = 1'700'000'000'000) : now_(start) {}
  TimestampMs NowMs() const override { return now_.load(); }
  void Set(TimestampMs t) { now_.store(t); }
  void Advance(TimestampMs delta) { now_.fetch_add(delta); }

 private:
  std::atomic<TimestampMs> now_;
};

}  // namespace dronebeat
