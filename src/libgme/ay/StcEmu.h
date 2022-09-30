#pragma once

// Sinclair Spectrum STC music file emulator

#include "AyApu.h"
#include "common.h"
#include "../ClassicEmu.h"

namespace gme {
namespace emu {
namespace ay {
namespace stc {

/* STC MODULE DATA DESCRIPTION */

struct SampleData {
  SampleData() = delete;
  SampleData(const SampleData &) = delete;
  uint8_t Volume() const { return mData[0] % 16; }
  uint8_t Noise() const { return mData[1] % 32; }
  bool ToneMask() const { return mData[1] & 64; }
  bool NoiseMask() const { return mData[1] & 128; }
  int16_t Transposition() const;

 private:
  uint8_t mData[3];
};

struct Sample {
  Sample() = delete;
  Sample(const Sample &) = delete;
  bool HasNumber(uint8_t number) const { return mNumber == number; }
  const SampleData *Data(size_t pos) const { return mData + pos; }
  bool IsRepeatable() const { return mRepeatPosition > 0; }
  uint8_t RepeatPosition() const { return mRepeatPosition - 1; }
  uint8_t RepeatLength() const { return mRepeatLength; }

 private:
  uint8_t mNumber;
  SampleData mData[32];
  uint8_t mRepeatPosition;
  uint8_t mRepeatLength;
};

struct Ornament {
  Ornament() = delete;
  Ornament(const Ornament &) = delete;
  bool HasNumber(uint8_t number) const { return mNumber == number; }
  const uint8_t *Data() const { return mData; }

 private:
  uint8_t mNumber;
  uint8_t mData[32];
};

struct Position {
  Position() = delete;
  Position(const Position &) = delete;
  uint8_t pattern;
  uint8_t transposition;
};

struct PositionsTable {
  PositionsTable() = delete;
  PositionsTable(const PositionsTable &) = delete;
  uint8_t count;
  Position position[0];
};

struct Pattern {
  Pattern() = delete;
  Pattern(const Pattern &) = delete;
  static const uint8_t MAX_COUNT = 32;
  bool HasNumber(uint8_t number) const { return mNumber == number; }
  const uint8_t *DataOffset(uint8_t channel) const { return mDataOffset[channel]; }

 private:
  uint8_t mNumber;
  uint8_t mDataOffset[3][2];
};

struct STCModule {
  STCModule() = delete;
  STCModule(const STCModule &) = delete;
  // Shared note period table.
  static const uint16_t NOTE_TABLE[96];

  static uint16_t GetTonePeriod(uint8_t tone);

  // Get song global delay.
  uint8_t GetDelay() const { return mDelay; }

  // Begin position iterator.
  const Position *GetPositionBegin() const;

  // End position iterator.
  const Position *GetPositionEnd() const;

  // Get pattern by specified number.
  const Pattern *GetPattern(uint8_t number) const;

  // Get data from specified pattern.
  const uint8_t *GetPatternData(const Pattern *pattern, uint8_t channel) const;

  // Get sample by specified number.
  const Sample *GetSample(uint8_t number) const;

  // Get data of specified ornament number.
  const Ornament *GetOrnament(uint8_t number) const;

  // Return song length in frames.
  unsigned CountSongLength() const;

  // Return song length in miliseconds.
  unsigned CountSongLengthMs() const;

  // Check file integrity.
  bool CheckIntegrity(size_t size) const;

 private:
  template<typename T> const T *mGetPointer(const uint8_t *offset) const {
    return reinterpret_cast<const T *>(&mDelay + get_le16(offset));
  }

  // Count pattern length. Return 0 on error.
  uint8_t mCountPatternLength(const Pattern *pattern, uint8_t channel = 0) const;

  // Check pattern table data by maximum records.
  bool mCheckPatternTable() const;

  // Check song data integrity (positions and linked patterns).
  bool mCheckSongData() const;

  // Find specified pattern by number. Return pointer to pattern on success, else nullptr.
  const Pattern *mFindPattern(uint8_t pattern) const;

  size_t mGetPositionsCount() const;

  const Pattern *mGetPatternBegin() const;

  const Pattern *mGetPatternEnd() const { return GetPattern(0xFF); }

  /* STC MODULE HEADER DATA */

  uint8_t mDelay;
  uint8_t mPositions[2];
  uint8_t mOrnaments[2];
  uint8_t mPatterns[2];
  char mName[18];
  uint8_t mSize[2];
  Sample mSamples[0];
};

// Channel entity
struct Channel {
  /* only create */
  Channel() = default;
  /* not allow make copies */
  Channel(const Channel &) = delete;

  void SetNote(uint8_t note) {
    mNote = note;
    mSamplePosition = 0;
    mSampleCounter = 32;
  }
  void Disable() { mSampleCounter = 0; }
  bool IsEnabled() const { return mSampleCounter > 0; }

  void SetPatternData(const uint8_t *data) { mPatternIt = data; }
  uint8_t PatternCode() { return *mPatternIt++; }

  void SetSample(const STCModule *stc, uint8_t number) { mSample = stc->GetSample(number); }
  void SetOrnament(const STCModule *stc, uint8_t number) { mOrnament = stc->GetOrnament(number)->Data(); }
  void AdvanceSample();

  const SampleData *GetSampleData() const { return mSample->Data(mSamplePosition); }
  uint8_t GetOrnamentNote() const { return mNote + mOrnament[mSamplePosition]; }

  bool IsEnvelopeEnabled() const { return mEnvelope; }
  void EnvelopeEnable() { mEnvelope = true; }
  void EnvelopeDisable() { mEnvelope = false; }

  void SetSkipCount(uint8_t delay) { mSkipNotes.Enable(delay); }
  bool IsEmptyLocation() { return !mSkipNotes.RunSkip(); }

 private:
  // Pointer to sample.
  const Sample *mSample;
  // Pattern data iterator.
  const uint8_t *mPatternIt;
  // Pointer to ornament data.
  const uint8_t *mOrnament;
  DelayRunner mSkipNotes;
  uint8_t mNote;
  uint8_t mSamplePosition;
  uint8_t mSampleCounter;
  bool mEnvelope;
};

class StcEmu : public ClassicEmu {
 public:
  StcEmu();
  ~StcEmu();
  static MusicEmu *createStcEmu() { return BLARGG_NEW StcEmu; }
  static gme_type_t static_type() { return gme_stc_type; }

 protected:
  blargg_err_t mLoad(const uint8_t *data, long size) override;
  blargg_err_t mStartTrack(int) override;
  blargg_err_t mGetTrackInfo(track_info_t *, int track) const override;
  blargg_err_t mRunClocks(blip_clk_time_t &) override;
  void mSetTempo(double) override;
  void mSetChannel(int, BlipBuffer *, BlipBuffer *, BlipBuffer *) override;
  void mUpdateEq(BlipEq const &) override;

  /* PLAYER METHODS AND DATA */

  uint8_t mPositionTransposition() const { return mPositionIt->transposition; }
  void mInit();
  void mPlayPattern();
  void mPlaySamples();
  void mAdvancePosition();

 private:
  // AY APU Emulator
  AyApu mApu;
  // Channels
  std::array<Channel, AyApu::OSCS_NUM> mChannels;
  // Song file header
  const STCModule *mModule;
  // Song position iterators
  const Position *mPositionIt, *mPositionEnd;
  // Current emulation time
  blip_clk_time_t mEmuTime;
  // Play period 50Hz
  blip_clk_time_t mFramePeriod;
  // Global song delay counter
  uint8_t mDelayCounter;
};

}  // namespace stc
}  // namespace ay
}  // namespace emu
}  // namespace gme
