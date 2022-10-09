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
  const SampleData *Data(size_t pos) const { return mData + pos; }
  bool IsRepeatable() const { return mRepeatPosition > 0; }
  uint8_t RepeatPosition() const { return mRepeatPosition - 1; }
  uint8_t RepeatLength() const { return mRepeatLength; }

 private:
  SampleData mData[32];
  uint8_t mRepeatPosition;
  uint8_t mRepeatLength;
};

struct Ornament {
  Ornament() = delete;
  Ornament(const Ornament &) = delete;
  const uint8_t *Data() const { return mData; }

 private:
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

struct STCModule {
  STCModule() = delete;
  STCModule(const STCModule &) = delete;

  // Get song global delay.
  uint8_t GetDelay() const { return mDelay; }

  // Begin position iterator.
  const Position *GetPositionBegin() const { return mPositions.GetPointer(this)->position; }

  // End position iterator.
  const Position *GetPositionEnd() const;

  // Get pattern by specified number.
  const Pattern *GetPattern(uint8_t number) const { return mPatterns.GetPointer(this)->GetObject(number); }

  // Get data from specified pattern.
  const PatternData *GetPatternData(const Pattern *pattern, uint8_t channel) const {
    return pattern->GetData(this, channel);
  }

  // Get sample by specified number.
  const Sample *GetSample(uint8_t number) const { return mSamples[0].GetObject(number); }

  // Get data of specified ornament number.
  const Ornament *GetOrnament(uint8_t number) const { return mOrnaments.GetPointer(this)->GetObject(number); }

  // Return song length in frames.
  unsigned CountSongLength() const;

  // Return song length in miliseconds.
  unsigned CountSongLengthMs() const;

  // Check file integrity.
  bool CheckIntegrity(size_t size) const;

 private:
  // Count pattern length. Return 0 on error.
  uint8_t mCountPatternLength(const Pattern *pattern, uint8_t channel = 0) const;

  // Check pattern table data by maximum records.
  bool mCheckPatternTable() const { return mPatterns.GetPointer(this)->FindObject<32>(0xFF); }

  // Check song data integrity (positions and linked patterns).
  bool mCheckSongData() const;

  // Find specified pattern by number. Return pointer to pattern on success, else nullptr.
  const Pattern *mFindPattern(uint8_t number) const { return mPatterns.GetPointer(this)->FindObject(number); }

  size_t mGetPositionsCount() const;

  /* STC MODULE HEADER DATA */

  // Song delay.
  uint8_t mDelay;
  // Positions table offset.
  DataOffset<PositionsTable> mPositions;
  // Ornaments table offset.
  DataOffset<Numberable<Ornament>> mOrnaments;
  // Patterns table offset.
  DataOffset<Numberable<Pattern>> mPatterns;
  // Identification string.
  char mName[18];
  // Module size.
  uint8_t mSize[2];
  // Samples table.
  Numberable<Sample> mSamples[0];
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

  void SetSkipCount(uint8_t delay) { mSkip.Set(delay); }
  bool IsEmptyLocation() { return !mSkip.Tick(); }

 private:
  // Pointer to sample.
  const Sample *mSample;
  // Pattern data iterator.
  const uint8_t *mPatternIt;
  // Pointer to ornament data.
  const uint8_t *mOrnament;
  DelayRunner mSkip;
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
  DelayRunner mDelay;
};

}  // namespace stc
}  // namespace ay
}  // namespace emu
}  // namespace gme
