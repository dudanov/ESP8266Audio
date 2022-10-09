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
  bool Run() {
    if (!mStep)
      return false;
    mValue += mStep;
    return true;
  }
  void Reset() { mValue = mStep = 0; }

 private:
  int16_t mValue, mStep;
};

class DelayedSlider {
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

// An object that has a number.
template<typename T> class Numberable {
 public:
  Numberable() = delete;
  Numberable(const Numberable<T> &) = delete;

  const T *GetObject(uint8_t number) const {
    auto it = this;
    while (it->mNumber != number)
      ++it;
    return &it->mObject;
  }

  const T *FindObject(uint8_t number) const {
    for (auto it = this; it->mNumber != 0xFF; ++it)
      if (it->mNumber == number)
        return &it->mObject;
    return nullptr;
  }

  template<uint8_t max_count> const T *FindObject(uint8_t number) const {
    auto it = this;
    for (uint8_t n = 0; n != max_count; ++n, ++it)
      if (it->mNumber == number)
        return &it->mObject;
    return nullptr;
  }

 private:
  uint8_t mNumber;
  T mObject;
};

template<typename T> struct DataOffset {
  DataOffset() = delete;
  DataOffset(const DataOffset &) = delete;
  uint16_t GetValue() const { return get_le16(mOffset); }
  bool IsValid() const { return GetValue() != 0; }
  const T *GetPointer(const void *start) const {
    return reinterpret_cast<const T *>(reinterpret_cast<const uint8_t *>(start) + GetValue());
  }

 private:
  uint8_t mOffset[2];
};

using PatternData = uint8_t;

struct Pattern {
  Pattern() = delete;
  Pattern(const Pattern &) = delete;
  const PatternData *GetData(const void *start, uint8_t channel) const { return mData[channel].GetPointer(start); }

 private:
  DataOffset<PatternData> mData[3];
};

}  // namespace ay
}  // namespace emu
}  // namespace gme
