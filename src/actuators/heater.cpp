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

void Heater::write(bool on) {
  on_ = on;
  digitalWrite(config::SSR_GPIO,
               (on == config::SSR_ACTIVE_HIGH) ? HIGH : LOW);
}

void Heater::forceOff() {
  demandPercent_ = 0.0f;
  write(false);
}

void Heater::update(uint32_t nowMs, bool permitted) {
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
