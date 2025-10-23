#include "FanAnimator.h"

FanAnimator::FanAnimator(U8G2& display) : u8g2_(display) {
  // Initialize slots
  for (uint8_t i = 0; i < FAN_ANIMATOR_MAX_FANS; i++) {
    fans_[i] = {0,0,nullptr,0,0,0,100,0,0,false};
  }
}

uint8_t FanAnimator::addFan(int16_t x, int16_t y,
                            const uint8_t* const* frames, uint8_t frameCount,
                            uint8_t w, uint8_t h,
                            uint32_t intervalMs) {
  if (fanCount >= FAN_ANIMATOR_MAX_FANS) return 255;
  fans_[fanCount] = {
    x, y, frames, frameCount, w, h,
    intervalMs, 0, 0, true
  };
  return fanCount++;
}

void FanAnimator::setFanSpeed(uint8_t idx, uint32_t intervalMs) {
  if (idx >= fanCount) return;
  fans_[idx].intervalMs = intervalMs;
}

void FanAnimator::moveFan(uint8_t idx, int16_t x, int16_t y) {
  if (idx >= fanCount) return;
  fans_[idx].x = x;
  fans_[idx].y = y;
}

void FanAnimator::setFanVisible(uint8_t idx, bool visible) {
  if (idx >= fanCount) return;
  fans_[idx].visible = visible;
}

void FanAnimator::update() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < fanCount; i++) {
    Fan& f = fans_[i];
    if (!f.visible || f.frameCount == 0 || f.frames == nullptr) continue;

    if ((uint32_t)(now - f.lastTick) >= f.intervalMs) {
      f.frameIndex++;
      if (f.frameIndex >= f.frameCount) f.frameIndex = 0;
      f.lastTick = now;
    }
  }
}

void FanAnimator::draw() {
  for (uint8_t i = 0; i < fanCount; i++) {
    Fan& f = fans_[i];
    if (!f.visible || f.frameCount == 0 || f.frames == nullptr) continue;
    const uint8_t* bmp = f.frames[f.frameIndex];
    u8g2_.drawXBMP(f.x, f.y, f.w, f.h, bmp);
  }
}

void FanAnimator::drawScene(bool clearBuffer, bool sendBuffer) {
  if (clearBuffer) u8g2_.clearBuffer();
  draw();
  if (sendBuffer) u8g2_.sendBuffer();
}
