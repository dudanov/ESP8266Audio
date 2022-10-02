#pragma once
#include <cstdint>

namespace gme {
namespace emu {
namespace ay {

class DelayRunner {
 public:
  // Init delay. Tick() method returns TRUE on next call.
  void Init(uint8_t delay) {
    mCounter = 1;
    mDelay = delay;
  }

  // Set delay value.
  void Set(uint8_t delay) { mDelay = mCounter = delay; }

  // Disable. Tick() method always return FALSE.
  void Disable() { mCounter = 0; }

  // Returns TRUE every N-th call. Always FALSE if delay is 0.
  bool Tick() {
    if (!mCounter || --mCounter)
      return false;
    mCounter = mDelay;
    return true;
  }

 private:
  uint8_t mCounter, mDelay;
};

class SimpleSlider {
 public:
  int16_t GetValue() const { return mValue; }
  int16_t GetStep() const { return mStep; }
  void SetValue(int16_t value) { mValue = value; }
  void SetStep(int16_t step) { mStep = step; }
  void Enable(uint8_t delay) { mDelay.Set(delay); }
  void Disable() { mDelay.Disable(); }
  bool Run() {
    if (mDelay.Tick()) {
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
  template<typename T> const T *GetPointer(const void *module) const {
    return reinterpret_cast<const T *>(reinterpret_cast<const uint8_t *>(module) + GetDataOffset());
  }

 private:
  uint8_t mOffset[2];
};

}  // namespace ay
}  // namespace emu
}  // namespace gme
