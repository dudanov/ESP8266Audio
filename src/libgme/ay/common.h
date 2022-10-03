#pragma once
#include <cstdint>

namespace gme {
namespace emu {
namespace ay {

using PatternData = uint8_t;

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

struct Pattern {
  Pattern() = delete;
  Pattern(const Pattern &) = delete;
  const DataOffset<PatternData> &GetOffset(uint8_t channel) const { return mData[channel]; }
  const PatternData *GetData(const void *start, uint8_t channel) const { return mData[channel].GetPointer(start); }

 private:
  DataOffset<PatternData> mData[3];
};

template<typename T> class Numerable {
 public:
  bool HasNumber(uint8_t number) const { return mNumber == number; }
  const T *GetObjectByNumber(uint8_t number) const {
    const Numerable<T> *it = this;
    while (!it->HasNumber(number))
      ++it;
    return &it->mObject;
  }
  const T *FindObjectByNumber(uint8_t number) const {
    for (const Numerable<T> *it = this; !it->HasNumber(0xFF); ++it)
      if (it->HasNumber(number))
        return &it->mObject;
    return nullptr;
  }
  template<uint8_t max_count> const T *FindObjectByNumber(uint8_t number) const {
    const Numerable<T> *it = this;
    for (uint8_t n = 0; n != max_count; ++n, ++it)
      if (it->HasNumber(number))
        return &it->mObject;
    return nullptr;
  }

 private:
  uint8_t mNumber;
  T mObject;
};

}  // namespace ay
}  // namespace emu
}  // namespace gme
