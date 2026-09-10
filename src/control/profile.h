#pragma once

#include <Arduino.h>

#include "config/config.h"

enum class ProfileStepType : uint8_t { RAMP, HOLD, OFF };
enum class ProfileRunState : uint8_t { EMPTY, LOADING, LOADED, RUNNING, PAUSED, COMPLETED, ABORTED };

struct ProfileStep {
  ProfileStepType type = ProfileStepType::OFF;
  float targetC = 0.0f;
  float rateKPerMin = 0.0f;
  uint32_t holdSeconds = 0;
};

class TemperatureProfile {
 public:
  bool beginLoad(uint16_t id);
  bool addStep(uint8_t oneBasedIndex, const ProfileStep &step);
  bool endLoad();
  bool start(float currentSetpoint, uint32_t nowMs);
  bool pause(uint32_t nowMs);
  bool resume(uint32_t nowMs);
  void stop();
  void clear();
  float update(uint32_t nowMs, bool &requestOff);

  bool valid() const { return valid_; }
  uint16_t id() const { return id_; }
  uint8_t stepCount() const { return stepCount_; }
  uint8_t currentStepNumber() const { return currentIndex_ < stepCount_ ? currentIndex_ + 1 : 0; }
  ProfileRunState state() const { return state_; }
  const ProfileStep *currentStep() const;
  uint32_t elapsedSeconds(uint32_t nowMs) const;
  uint32_t remainingSeconds(uint32_t nowMs) const;
  static const char *typeName(ProfileStepType type);
  static const char *runStateName(ProfileRunState state);

 private:
  ProfileStep steps_[config::PROFILE_MAX_STEPS];
  uint16_t id_ = 0;
  uint8_t stepCount_ = 0;
  uint8_t currentIndex_ = 0;
  bool valid_ = false;
  ProfileRunState state_ = ProfileRunState::EMPTY;
  uint32_t stepStartMs_ = 0;
  uint32_t pauseStartMs_ = 0;
  float stepStartSetpoint_ = 0.0f;
  float activeSetpoint_ = 0.0f;

  uint32_t stepDurationMs() const;
  void advance(uint32_t nowMs);
};
