#pragma once

// Sinclair Spectrum PT3 music file emulator

#include "AyApu.h"
#include "../ClassicEmu.h"
#include <stack>

namespace gme {
namespace emu {
namespace ay {

class DelayRunner {
 public:
  // Set delay and counter.
  void Enable(uint8_t delay) { mDelay = mDelayCounter = delay; }
  // Set delay and counter init value.
  void Enable(uint8_t delay, uint8_t init) {
    mDelay = delay;
    mDelayCounter = init;
  }
  // Reset.
  void Disable() { mDelayCounter = 0; }

  // Returns TRUE every N-th call. Always FALSE if delay is 0.
  bool RunPeriod() {
    if (!mDelayCounter || --mDelayCounter)
      return false;
    mDelayCounter = mDelay;
    return true;
  }

  // Returns TRUE every N+1 call. Always TRUE if delay is 0.
  bool RunSkip() {
    if (mDelayCounter) {
      mDelayCounter--;
      return false;
    }
    mDelayCounter = mDelay;
    return true;
  }

 private:
  uint8_t mDelayCounter, mDelay;
};

class SimpleSlider {
 public:
  int16_t GetValue() const { return mValue; }
  int16_t GetStep() const { return mStep; }
  void SetValue(int16_t value) { mValue = value; }
  void SetStep(int16_t step) { mStep = step; }
  void Enable(uint8_t delay) { mDelay.Enable(delay); }
  void Disable() { mDelay.Disable(); }
  bool Run() {
    if (mDelay.RunPeriod()) {
      mValue += mStep;
      return true;
    }
    return false;
  }
  void Reset() {
    mDelay.Disable();
    mValue = 0;
  }

 private:
  DelayRunner mDelay;
  int16_t mValue, mStep;
};

struct DataOffset {
  DataOffset() = delete;
  DataOffset(const DataOffset &) = delete;
  uint16_t GetDataOffset() const { return get_le16(mOffset); }
  bool IsValid() const { return GetDataOffset() != 0; }

 private:
  uint8_t mOffset[2];
};

}  // namespace ay
}  // namespace emu
}  // namespace gme
