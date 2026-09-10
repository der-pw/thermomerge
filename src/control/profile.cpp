#include "control/profile.h"

#include <math.h>

bool TemperatureProfile::beginLoad(uint16_t id) {
  if (state_ == ProfileRunState::RUNNING || state_ == ProfileRunState::PAUSED) return false;
  id_ = id; stepCount_ = 0; currentIndex_ = 0; valid_ = false;
  state_ = ProfileRunState::LOADING;
  return true;
}

bool TemperatureProfile::addStep(uint8_t oneBasedIndex, const ProfileStep &step) {
  if (state_ != ProfileRunState::LOADING || oneBasedIndex != stepCount_ + 1 ||
      stepCount_ >= config::PROFILE_MAX_STEPS) return false;
  if (step.type == ProfileStepType::RAMP &&
      (!isfinite(step.targetC) || !isfinite(step.rateKPerMin) ||
       step.rateKPerMin < config::PROFILE_MIN_RATE_K_PER_MIN)) return false;
  if (step.type == ProfileStepType::HOLD && step.holdSeconds == 0) return false;
  steps_[stepCount_++] = step;
  return true;
}

bool TemperatureProfile::endLoad() {
  if (state_ != ProfileRunState::LOADING || stepCount_ == 0 ||
      steps_[stepCount_ - 1].type != ProfileStepType::OFF) {
    valid_ = false; state_ = ProfileRunState::EMPTY; return false;
  }
  valid_ = true; state_ = ProfileRunState::LOADED; return true;
}

bool TemperatureProfile::start(float currentSetpoint, uint32_t nowMs) {
  if (!valid_ || !isfinite(currentSetpoint)) return false;
  currentIndex_ = 0; activeSetpoint_ = stepStartSetpoint_ = currentSetpoint;
  stepStartMs_ = nowMs; state_ = ProfileRunState::RUNNING;
  return true;
}

bool TemperatureProfile::pause(uint32_t nowMs) {
  if (state_ != ProfileRunState::RUNNING) return false;
  pauseStartMs_ = nowMs; state_ = ProfileRunState::PAUSED; return true;
}

bool TemperatureProfile::resume(uint32_t nowMs) {
  if (state_ != ProfileRunState::PAUSED) return false;
  stepStartMs_ += nowMs - pauseStartMs_; state_ = ProfileRunState::RUNNING; return true;
}

void TemperatureProfile::stop() {
  if (state_ == ProfileRunState::RUNNING || state_ == ProfileRunState::PAUSED)
    state_ = ProfileRunState::ABORTED;
}

void TemperatureProfile::clear() {
  stop(); valid_ = false; stepCount_ = 0; currentIndex_ = 0; state_ = ProfileRunState::EMPTY;
}

uint32_t TemperatureProfile::stepDurationMs() const {
  if (currentIndex_ >= stepCount_) return 0;
  const ProfileStep &step = steps_[currentIndex_];
  if (step.type == ProfileStepType::HOLD) return step.holdSeconds * 1000UL;
  if (step.type == ProfileStepType::RAMP) {
    return static_cast<uint32_t>(fabsf(step.targetC - stepStartSetpoint_) /
                                 step.rateKPerMin * 60000.0f);
  }
  return 0;
}

void TemperatureProfile::advance(uint32_t nowMs) {
  stepStartSetpoint_ = activeSetpoint_;
  ++currentIndex_;
  stepStartMs_ = nowMs;
  if (currentIndex_ >= stepCount_) state_ = ProfileRunState::COMPLETED;
}

float TemperatureProfile::update(uint32_t nowMs, bool &requestOff) {
  requestOff = false;
  if (state_ != ProfileRunState::RUNNING || currentIndex_ >= stepCount_) return activeSetpoint_;
  const ProfileStep &step = steps_[currentIndex_];
  if (step.type == ProfileStepType::OFF) {
    requestOff = true; state_ = ProfileRunState::COMPLETED; return activeSetpoint_;
  }
  const uint32_t duration = stepDurationMs();
  const uint32_t elapsed = nowMs - stepStartMs_;
  if (step.type == ProfileStepType::RAMP) {
    if (duration == 0 || elapsed >= duration) activeSetpoint_ = step.targetC;
    else activeSetpoint_ = stepStartSetpoint_ +
         (step.targetC - stepStartSetpoint_) * (static_cast<float>(elapsed) / duration);
  }  // HOLD deliberately keeps the setpoint reached by the preceding step.
  if (elapsed >= duration) advance(nowMs);
  return activeSetpoint_;
}

const ProfileStep *TemperatureProfile::currentStep() const {
  return currentIndex_ < stepCount_ ? &steps_[currentIndex_] : nullptr;
}

uint32_t TemperatureProfile::elapsedSeconds(uint32_t nowMs) const {
  if (state_ != ProfileRunState::RUNNING && state_ != ProfileRunState::PAUSED) return 0;
  const uint32_t effectiveNow = state_ == ProfileRunState::PAUSED ? pauseStartMs_ : nowMs;
  return (effectiveNow - stepStartMs_) / 1000UL;
}

uint32_t TemperatureProfile::remainingSeconds(uint32_t nowMs) const {
  const uint32_t duration = stepDurationMs() / 1000UL;
  const uint32_t elapsed = elapsedSeconds(nowMs);
  return duration > elapsed ? duration - elapsed : 0;
}

const char *TemperatureProfile::typeName(ProfileStepType type) {
  switch (type) { case ProfileStepType::RAMP: return "RAMP"; case ProfileStepType::HOLD: return "HOLD"; case ProfileStepType::OFF: return "OFF"; }
  return "OFF";
}

const char *TemperatureProfile::runStateName(ProfileRunState state) {
  switch (state) {
    case ProfileRunState::EMPTY: return "EMPTY"; case ProfileRunState::LOADING: return "LOADING";
    case ProfileRunState::LOADED: return "LOADED"; case ProfileRunState::RUNNING: return "RUNNING";
    case ProfileRunState::PAUSED: return "PAUSED"; case ProfileRunState::COMPLETED: return "COMPLETED";
    case ProfileRunState::ABORTED: return "ABORTED";
  }
  return "EMPTY";
}
