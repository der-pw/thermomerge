#pragma once

#include <Arduino.h>

class PIDController {
 public:
  void setTunings(float kp, float ki, float kd);
  void getTunings(float &kp, float &ki, float &kd) const;
  void reset(float input, float output, float setpoint);
  float compute(float setpoint, float input);

 private:
  float kp_ = 2.0f, ki_ = 0.1f, kd_ = 5.0f;
  float integral_ = 0.0f;
  float lastInput_ = NAN;
};

enum class AutotuneResult : uint8_t { IDLE, RUNNING, SUCCEEDED, FAILED };

class PIDAutotune {
 public:
  bool start(float target, float currentTemperature, uint32_t nowMs);
  void stop();
  float update(float temperature, uint32_t nowMs);
  AutotuneResult result() const { return result_; }
  float target() const { return target_; }
  void tunings(float &kp, float &ki, float &kd) const;

 private:
  AutotuneResult result_ = AutotuneResult::IDLE;
  float target_ = 0.0f;
  float bias_ = 50.0f;
  bool relayHigh_ = true;
  uint32_t startMs_ = 0;
  uint32_t lastRisingCrossMs_ = 0;
  float periodSumSec_ = 0.0f;
  float highPeak_ = -INFINITY;
  float lowPeak_ = INFINITY;
  float amplitudeSum_ = 0.0f;
  uint8_t cycles_ = 0;
  float kp_ = 0.0f, ki_ = 0.0f, kd_ = 0.0f;
};
