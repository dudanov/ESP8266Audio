#pragma once

// Sinclair Spectrum PT3 music file emulator

#include "AyApu.h"
#include "../ClassicEmu.h"

namespace gme {
namespace emu {
namespace ay {
namespace pt3 {

/* PT3 MODULE DATA DESCRIPTION */

struct SampleData {
  SampleData() = delete;
  SampleData(const SampleData &) = delete;
  bool EnvelopeMask() const { return mData[0] & 1; }
  uint8_t Noise() const { return mData[0] / 2 % 32; }
  int8_t EnvelopeSlide() const {
    const uint8_t val = mData[0] >> 1;
    return (val & 0x10) ? (val | 0xF0) : (val & 0x0F);
  }
  uint8_t Volume() const { return mData[1] % 16; }
  bool ToneMask() const { return mData[1] & 16; }
  bool NoiseMask() const { return mData[1] & 128; }
  int16_t Transposition() const { return get_le16(mTransposition); }

 private:
  uint8_t mData[2];
  uint8_t mTransposition[2];
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
  uint8_t mLoop;
  uint8_t mEnd;
  SampleData mData[32];
};

struct Ornament {
  Ornament() = delete;
  Ornament(const Ornament &) = delete;
  const int8_t *DataBegin() const { return mData; }
  const int8_t *DataLoop() const { return mData + mLoop; }
  const int8_t *DataEnd() const { return mData + mEnd; }

 private:
  uint8_t mLoop;
  uint8_t mEnd;
  int8_t mData[0];
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

struct PT3Module {
  PT3Module() = delete;
  PT3Module(const PT3Module &) = delete;
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

  /* PT3 MODULE HEADER DATA */

  // Identification: "ProTracker 3.".
  char mIdentify[13];
  // Subversion: "3", "4", "5", "6", etc.
  char mSubVersion;
  // " compilation of " or any text of this length.
  char mUnused0[16];
  // Track name. Unused characters are padded with spaces.
  char mName[32];
  // " by " or any text of this length.
  char mUnused1[4];
  // Author's name. Unused characters are padded with spaces.
  char mAuthor[32];
  // One space (any character).
  char mUnused2;
  // Note frequency table number.
  uint8_t mNoteTable;
  // Delay value (tempo).
  uint8_t mDelay;
  // Song end.
  uint8_t mNumberOfPositions;
  // Song loop.
  uint8_t mLoopPosition;
  // Pattern table offset.
  uint8_t mPatternOffset[2];
  // Sample offsets. Starting from sample zero.
  uint8_t mSampleOffset[32][2];
  // Ornament offsets. Starting from ornament zero.
  uint8_t mOrnamentOffset[16][2];
  // 
  PositionsTable mPositionList[0];
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

  void SetSample(const PT3Module *pt3, uint8_t number) { mSample = pt3->GetSample(number); }
  void SetOrnament(const PT3Module *pt3, uint8_t number) { mOrnament = pt3->GetOrnament(number)->Data(); }
  void AdvanceSample();

  const SampleData *GetSampleData() const { return mSample->Data(mSamplePosition); }
  uint8_t GetOrnamentNote() const { return mNote + mOrnament[mSamplePosition]; }

  bool IsEnvelopeEnabled() const { return mEnvelope; }
  void EnvelopeEnable() { mEnvelope = true; }
  void EnvelopeDisable() { mEnvelope = false; }

  void SetSkipCount(uint8_t delay) { mSkipCount = mSkipCounter = delay; }
  bool IsEmptyLocation();

 private:
  // Pointer to sample.
  const Sample *mSample;
  // Pattern data iterator.
  const uint8_t *mPatternIt;
  // Pointer to ornament data.
  const uint8_t *mOrnament;
  uint8_t mNote;
  uint8_t mSamplePosition;
  uint8_t mSampleCounter;
  uint8_t mSkipCounter;
  uint8_t mSkipCount;
  bool mEnvelope;
};

class Pt3Emu : public ClassicEmu {
 public:
  Pt3Emu();
  ~Pt3Emu();
  static MusicEmu *createPt3Emu() { return BLARGG_NEW Pt3Emu; }
  static gme_type_t static_type() { return gme_pt3_type; }

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
  const PT3Module *mModule;
  // Song position iterators
  const Position *mPositionIt, *mPositionEnd;
  // Current emulation time
  blip_clk_time_t mEmuTime;
  // Play period 50Hz
  blip_clk_time_t mFramePeriod;
  // Global song delay counter
  uint8_t mDelayCounter;
};

}  // namespace pt3
}  // namespace ay
}  // namespace emu
}  // namespace gme
