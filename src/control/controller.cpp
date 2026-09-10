#include "control/controller.h"

#include <math.h>

#include "config/config.h"

void PIDController::setTunings(float kp, float ki, float kd) {
  if (isfinite(kp) && isfinite(ki) && isfinite(kd) && kp >= 0.0f && ki >= 0.0f && kd >= 0.0f) {
    kp_ = kp; ki_ = ki; kd_ = kd;
  }
}

void PIDController::getTunings(float &kp, float &ki, float &kd) const {
  kp = kp_; ki = ki_; kd = kd_;
}

void PIDController::reset(float input, float output, float setpoint) {
  lastInput_ = input;
  integral_ = constrain(output - kp_ * (setpoint - input), -100.0f, 100.0f);
}

float PIDController::compute(float setpoint, float input) {
  if (!isfinite(input) || !isfinite(setpoint)) return 0.0f;
  const float dt = config::PID_SAMPLE_MS / 1000.0f;
  const float error = setpoint - input;
  const float derivative = isfinite(lastInput_) ? -(input - lastInput_) / dt : 0.0f;
  const float candidateIntegral = integral_ + ki_ * error * dt;
  const float unclamped = kp_ * error + candidateIntegral + kd_ * derivative;
  // Conditional integration prevents winding farther into a saturated output.
  if ((unclamped < 100.0f || error < 0.0f) && (unclamped > 0.0f || error > 0.0f)) {
    integral_ = constrain(candidateIntegral, -100.0f, 100.0f);
  }
  lastInput_ = input;
  return constrain(kp_ * error + integral_ + kd_ * derivative, 0.0f, 100.0f);
}

bool PIDAutotune::start(float target, float currentTemperature, uint32_t nowMs) {
  if (!isfinite(target) || !isfinite(currentTemperature)) return false;
  target_ = target;
  bias_ = 50.0f;
  relayHigh_ = currentTemperature < target;
  startMs_ = nowMs;
  lastRisingCrossMs_ = 0;
  periodSumSec_ = 0.0f;
  highPeak_ = currentTemperature;
  lowPeak_ = currentTemperature;
  amplitudeSum_ = 0.0f;
  cycles_ = 0;
  kp_ = ki_ = kd_ = 0.0f;
  result_ = AutotuneResult::RUNNING;
  return true;
}

void PIDAutotune::stop() {
  if (result_ == AutotuneResult::RUNNING) result_ = AutotuneResult::FAILED;
}

float PIDAutotune::update(float temperature, uint32_t nowMs) {
  if (result_ != AutotuneResult::RUNNING) return 0.0f;
  if (!isfinite(temperature) || nowMs - startMs_ > config::AUTOTUNE_TIMEOUT_MS) {
    result_ = AutotuneResult::FAILED;
    return 0.0f;
  }
  highPeak_ = fmaxf(highPeak_, temperature);
  lowPeak_ = fminf(lowPeak_, temperature);

  if (relayHigh_ && temperature >= target_ + config::AUTOTUNE_NOISE_BAND_C) {
    relayHigh_ = false;
  } else if (!relayHigh_ && temperature <= target_ - config::AUTOTUNE_NOISE_BAND_C) {
    relayHigh_ = true;
    if (lastRisingCrossMs_ != 0) {
      const float periodSec = (nowMs - lastRisingCrossMs_) / 1000.0f;
      const float amplitude = (highPeak_ - lowPeak_) * 0.5f;
      if (periodSec > 1.0f && amplitude > 0.1f) {
        periodSumSec_ += periodSec;
        amplitudeSum_ += amplitude;
        ++cycles_;
      }
    }
    lastRisingCrossMs_ = nowMs;
    highPeak_ = lowPeak_ = temperature;
  }

  if (cycles_ >= config::AUTOTUNE_REQUIRED_CYCLES) {
    const float pu = periodSumSec_ / cycles_;
    const float amplitude = amplitudeSum_ / cycles_;
    const float ku = 4.0f * config::AUTOTUNE_RELAY_PERCENT / (PI * amplitude);
    // Conservative Tyreus-Luyben-like settings for a slow thermal plant.
    kp_ = 0.31f * ku;
    const float ti = 2.2f * pu;
    const float td = pu / 6.3f;
    ki_ = kp_ / ti;
    kd_ = kp_ * td;
    result_ = AutotuneResult::SUCCEEDED;
    return 0.0f;
  }

  return constrain(bias_ + (relayHigh_ ? config::AUTOTUNE_RELAY_PERCENT
                                       : -config::AUTOTUNE_RELAY_PERCENT),
                   0.0f, 100.0f);
}

void PIDAutotune::tunings(float &kp, float &ki, float &kd) const {
  kp = kp_; ki = ki_; kd = kd_;
}
