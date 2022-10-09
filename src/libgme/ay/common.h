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
template<typename T> class NumberedList {
 public:
  NumberedList() = delete;
  NumberedList(const NumberedList<T> &) = delete;

  const T *GetItem(const uint8_t number) const {
    const NumberedList<T> *it = this;
    while (it->mNumber != number)
      ++it;
    return &it->mItem;
  }

  const T *FindItem(const uint8_t number) const {
    for (const NumberedList<T> *it = this; !it->mIsEnd(); ++it)
      if (it->mNumber == number)
        return &it->mItem;
    return nullptr;
  }

  bool IsValid(const uint8_t max_count) const {
    const NumberedList<T> *it = this;
    for (uint8_t n = 0; n != max_count; ++n, ++it)
      if (it->mIsEnd())
        return true;
    return false;
  }

 private:
  bool mIsEnd() const { return mNumber == 0xFF; }
  uint8_t mNumber;
  T mItem;
};

template<typename T> struct DataOffset {
  DataOffset() = delete;
  DataOffset(const DataOffset &) = delete;
  uint16_t GetOffset() const { return get_le16(mOffset); }
  bool IsValid() const { return GetOffset() != 0; }
  const T *GetPointer(const void *start) const {
    return reinterpret_cast<const T *>(reinterpret_cast<const uint8_t *>(start) + GetOffset());
  }

 private:
  uint8_t mOffset[2];
};

template<typename T> class LoopDataPlayer {
 public:
  typedef typename T::data_type data_type;
  void Load(const T *data) {
    mData = data->data;
    mPos = 0;
    mEnd = data->end;
    mLoop = data->loop;
  }
  void SetPosition(uint8_t pos) { mPos = pos; }
  const data_type &GetData() const { return mData[mPos]; }
  void Advance() {
    if (++mPos >= mEnd)
      mPos = mLoop;
  }

 private:
  const data_type *mData;
  uint8_t mPos, mEnd, mLoop;
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
