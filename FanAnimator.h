#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

// You can override this before including the header if you want more fans.
#ifndef FAN_ANIMATOR_MAX_FANS
#define FAN_ANIMATOR_MAX_FANS 16
#endif

class FanAnimator {
public:
  // Constructor needs a reference to your already-created U8g2 instance.
  explicit FanAnimator(U8G2& display);

  /**
   * Add a fan animation.
   * @param x, y        Top-left coordinates (pixels)
   * @param frames      Pointer to array of frame bitmaps (each is const uint8_t*)
   * @param frameCount  How many frames in the array
   * @param w, h        Frame width/height in pixels
   * @param intervalMs  Milliseconds per frame (smaller = faster)
   * @return index of the fan (0..N-1) or 255 if full
   */
  uint8_t addFan(int16_t x, int16_t y,
                 const uint8_t* const* frames, uint8_t frameCount,
                 uint8_t w, uint8_t h,
                 uint32_t intervalMs);

  void setFanSpeed(uint8_t idx, uint32_t intervalMs);
  void moveFan(uint8_t idx, int16_t x, int16_t y);
  void setFanVisible(uint8_t idx, bool visible);

  // Advance animations based on millis() (non-blocking)
  void update();

  // Draw all fans into the CURRENT U8g2 buffer (does not clear or send)
  void draw();

  // Convenience: clear → draw fans → send
  void drawScene(bool clearBuffer = true, bool sendBuffer = true);

  // How many fans currently active
  uint8_t count() const { return fanCount; }

private:
  struct Fan {
    int16_t x;
    int16_t y;
    const uint8_t* const* frames; // pointer to array of frame pointers
    uint8_t frameCount;
    uint8_t w;
    uint8_t h;
    uint32_t intervalMs;
    uint8_t frameIndex;
    uint32_t lastTick;
    bool visible;
  };

  U8G2& u8g2_;
  Fan fans_[FAN_ANIMATOR_MAX_FANS];
  uint8_t fanCount = 0;
};
