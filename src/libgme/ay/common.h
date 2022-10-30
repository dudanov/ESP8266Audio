#pragma once
#include <cstdint>
#include <type_traits>

namespace gme {
namespace emu {
namespace ay {

/* MODULES DATA TYPES */

using PatternData = uint8_t;

template<typename T> struct DataOffset {
  DataOffset() = delete;
  DataOffset(const DataOffset &) = delete;
  bool IsValid() const { return GetOffset() != 0; }
  uint16_t GetOffset() const { return get_le16(mOffset); }
  const T &GetReference(const void *start) const { return *GetPointer(start); }
  const T *GetPointer(const void *start) const {
    return reinterpret_cast<const T *>(reinterpret_cast<const uint8_t *>(start) + GetOffset());
  }

 private:
  uint8_t mOffset[2];
};

struct Pattern {
  Pattern() = delete;
  Pattern(const Pattern &) = delete;
  const PatternData *GetData(const void *start, uint8_t channel) const { return mData[channel].GetPointer(start); }

 private:
  DataOffset<PatternData> mData[3];
};

template<typename T> class NumberedList {
 public:
  NumberedList() = delete;
  NumberedList(const NumberedList<T> &) = delete;

  const T &GetItem(const uint8_t number) const {
    const NumberedList<T> *it = this;
    while (it->mNumber != number)
      ++it;
    return it->mItem;
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

/* PLAYERS HELPER CLASSES */

class DelayRunner {
 public:
  // Init delay. Run() method returns TRUE on next call.
  void Init(uint8_t delay) {
    mCounter = 1;
    mDelay = delay;
  }

  // Set delay value.
  void Enable(uint8_t delay) { mDelay = mCounter = delay; }

  // Disable. Run() method always return FALSE.
  void Disable() { mCounter = 0; }

  // Returns TRUE every N-th call. Always FALSE if delay is 0.
  bool Run() {
    if ((mCounter == 0) || (--mCounter != 0))
      return false;
    mCounter = mDelay;
    return true;
  }

 private:
  uint8_t mCounter, mDelay;
};

template<typename T> class SliderBase {
  static_assert(std::is_signed<T>::value, "T must be an signed integer type.");

 public:
  void SetValue(const T &value) { mValue = value; }
  void SetStep(const T &step) { mStep = step; }
  const T &GetValue() const { return mValue; }
  const T &GetStep() const { return mStep; }

 protected:
  bool mRun() {
    if (mStep == 0)
      return false;
    mValue += mStep;
    return true;
  }
  void mReset() { mValue = mStep = 0; }
  T mValue, mStep;
};

template<typename T> class SimpleSlider : public SliderBase<T> {
 public:
  bool Run() { return mRun(); }
  void Reset() { mReset(); }
};

template<typename T> class DelayedSlider : public SliderBase<T> {
 public:
  void Enable(uint8_t delay) { mDelay.Enable(delay); }
  void Disable() { mDelay.Disable(); }
  bool Run() { return mDelay.Run() && mRun(); }
  void Reset() {
    mDelay.Disable();
    mReset();
  }

 private:
  DelayRunner mDelay;
};

template<typename T> class LoopDataPlayer {
 public:
  typedef typename T::loop_data_t loop_data_t;
  const loop_data_t &GetData() const { return mData->data[mPos]; }
  void SetPosition(uint8_t pos) { mPos = pos; }
  void Load(const T *data) {
    mData = data;
    mPos = 0;
  }
  void Advance() {
    if (++mPos >= mData->end)
      mPos = mData->loop;
  }

 private:
  const T *mData;
  uint8_t mPos;
};

}  // namespace ay
}  // namespace emu
}  // namespace gme
