#include "user_interface/encoder.h"

#include "config/config.h"

void Encoder::begin() {
  pinMode(config::ENCODER_A_GPIO, INPUT_PULLUP);
  pinMode(config::ENCODER_B_GPIO, INPUT_PULLUP);
  pinMode(config::ENCODER_BUTTON_GPIO, INPUT_PULLUP);
  previousAb_ = (digitalRead(config::ENCODER_A_GPIO) << 1) |
                digitalRead(config::ENCODER_B_GPIO);
  rawButton_ = stableButton_ = digitalRead(config::ENCODER_BUTTON_GPIO) == LOW;
}

EncoderEvents Encoder::update(uint32_t nowMs) {
  EncoderEvents events;
  static constexpr int8_t transition[16] = {
      0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
  const uint8_t currentAb = (digitalRead(config::ENCODER_A_GPIO) << 1) |
                            digitalRead(config::ENCODER_B_GPIO);
  quarterSteps_ += transition[(previousAb_ << 2) | currentAb];
  previousAb_ = currentAb;
  if (quarterSteps_ >= 4) { events.detents = 1; quarterSteps_ = 0; }
  else if (quarterSteps_ <= -4) { events.detents = -1; quarterSteps_ = 0; }

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
