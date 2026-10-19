#include "user_interface/encoder.h"

#include "config/config.h"

namespace {
volatile int32_t encoderPosition = 0;
volatile uint8_t encoderState = 0;

void encoderInterruptServiceRoutine() {
  const uint8_t currentState =
      (digitalRead(config::ENCODER_A_GPIO) << 1) |
      digitalRead(config::ENCODER_B_GPIO);
  const uint8_t combined = (encoderState << 2) | currentState;
  static constexpr int8_t transitions[16] = {
      0, 1, -1, 0, -1, 0, 0, 1,
      1, 0, 0, -1, 0, -1, 1, 0};
  encoderPosition += transitions[combined];
  encoderState = currentState;
}
}  // namespace

void Encoder::begin() {
  pinMode(config::ENCODER_A_GPIO, INPUT_PULLUP);
  pinMode(config::ENCODER_B_GPIO, INPUT_PULLUP);
  pinMode(config::ENCODER_BUTTON_GPIO, INPUT_PULLUP);
  encoderState = (digitalRead(config::ENCODER_A_GPIO) << 1) |
                 digitalRead(config::ENCODER_B_GPIO);
  encoderPosition = 0;
  processedPosition_ = 0;
  rawButton_ = stableButton_ = digitalRead(config::ENCODER_BUTTON_GPIO) == LOW;
  attachInterrupt(digitalPinToInterrupt(config::ENCODER_A_GPIO),
                  encoderInterruptServiceRoutine, CHANGE);
  attachInterrupt(digitalPinToInterrupt(config::ENCODER_B_GPIO),
                  encoderInterruptServiceRoutine, CHANGE);
}

EncoderEvents Encoder::update(uint32_t nowMs) {
  EncoderEvents events;
  int32_t position;
  noInterrupts();
  position = encoderPosition;
  interrupts();

  const int32_t positionDelta = position - processedPosition_;
  const int32_t detents = positionDelta / config::ENCODER_COUNTS_PER_DETENT;
  if (detents != 0) {
    events.detents = static_cast<int16_t>(detents);
    processedPosition_ += detents * config::ENCODER_COUNTS_PER_DETENT;
  }

  const bool rawPressed = digitalRead(config::ENCODER_BUTTON_GPIO) == LOW;
  if (rawPressed != rawButton_) {
    rawButton_ = rawPressed;
    rawChangedMs_ = nowMs;
  }
  if (rawPressed != stableButton_ && nowMs - rawChangedMs_ >= config::BUTTON_DEBOUNCE_MS) {
    stableButton_ = rawPressed;
    if (stableButton_) {
      pressedMs_ = nowMs;
      longReported_ = false;
    } else if (!longReported_) {
      events.shortPress = true;
    }
  }
  if (stableButton_ && !longReported_ && nowMs - pressedMs_ >= config::BUTTON_LONG_PRESS_MS) {
    events.longPress = true;
    longReported_ = true;
  }
  return events;
}
