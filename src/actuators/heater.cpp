#include "actuators/heater.h"

#include "config/config.h"

void Heater::begin() {
  // Preload LOW before changing direction to minimize any boot transient.
  digitalWrite(config::SSR_GPIO, config::SSR_ACTIVE_HIGH ? LOW : HIGH);
  pinMode(config::SSR_GPIO, OUTPUT);
  write(false);
  windowStartMs_ = millis();
}

void Heater::setDemand(float percent) {
  demandPercent_ = constrain(percent, 0.0f, 100.0f);
}

bool Heater::startTest(uint32_t nowMs, bool permitted) {
  if (!permitted || testActive_) return false;
  demandPercent_ = 0.0f;
  testPhaseStartMs_ = nowMs;
  testPulsesCompleted_ = 0;
  testOn_ = true;
  testActive_ = true;
  return true;
}

void Heater::cancelTest() {
  testActive_ = false;
  testOn_ = false;
  write(false);
}

void Heater::write(bool on) {
  on_ = on;
  digitalWrite(config::SSR_GPIO,
               (on == config::SSR_ACTIVE_HIGH) ? HIGH : LOW);
}

void Heater::forceOff() {
  demandPercent_ = 0.0f;
  testActive_ = false;
  testOn_ = false;
  write(false);
}

void Heater::update(uint32_t nowMs, bool permitted, bool testPermitted) {
  if (testActive_) {
    if (!testPermitted) {
      cancelTest();
      return;
    }
    const uint32_t phaseMs = testOn_ ? config::SSR_TEST_PULSE_MS
                                    : config::SSR_TEST_GAP_MS;
    if (nowMs - testPhaseStartMs_ >= phaseMs) {
      if (testOn_) {
        ++testPulsesCompleted_;
        if (testPulsesCompleted_ >= config::SSR_TEST_PULSE_COUNT) {
          cancelTest();
          return;
        }
        testOn_ = false;
      } else {
        testOn_ = true;
      }
      testPhaseStartMs_ = nowMs;
    }
    write(testOn_);
    return;
  }
  if (!permitted || demandPercent_ <= 0.0f) {
    write(false);
    if (nowMs - windowStartMs_ >= config::SSR_WINDOW_MS) windowStartMs_ = nowMs;
    return;
  }
  while (nowMs - windowStartMs_ >= config::SSR_WINDOW_MS) {
    windowStartMs_ += config::SSR_WINDOW_MS;
  }
  const uint32_t onTime = static_cast<uint32_t>(config::SSR_WINDOW_MS * demandPercent_ / 100.0f);
  write((nowMs - windowStartMs_) < onTime);
}
