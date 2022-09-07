#pragma once

// Sinclair Spectrum STC music file emulator

#include "AyApu.h"
#include "../ClassicEmu.h"

namespace gme {
namespace emu {
namespace ay {

class StcEmu : public ClassicEmu {
 public:
  StcEmu();
  ~StcEmu();
  static MusicEmu *createStcEmu() { return BLARGG_NEW StcEmu; }
  static gme_type_t static_type() { return gme_stc_type; }

  struct SampleData {
    uint8_t Volume() const { return mData[0] % 16; }
    uint8_t Noise() const { return mData[1] % 32; }
    bool ToneMask() const { return mData[1] & 64; }
    bool NoiseMask() const { return mData[1] & 128; }
    int16_t Transposition() const;

   private:
    uint8_t mData[3];
  };

  struct Sample {
    bool HasNumber(uint8_t number) const { return mNumber == number; }
    const SampleData *Data(size_t pos) const { return mData + pos; }
    bool IsRepeatable() const { return mRepeatPosition != 0; }
    uint8_t RepeatPosition() const { return mRepeatPosition; }
    const uint8_t RepeatLength() const { return mRepeatLength; }

   private:
    uint8_t mNumber;
    SampleData mData[32];
    uint8_t mRepeatPosition;
    uint8_t mRepeatLength;
  };

  struct Ornament {
    bool HasNumber(uint8_t number) const { return mNumber == number; }
    const uint8_t *Data() const { return mData; }

   private:
    uint8_t mNumber;
    uint8_t mData[32];
  };

  struct Position {
    uint8_t pattern;
    uint8_t transposition;
  };

  struct PositionsTable {
    uint8_t count;
    Position position[0];
  };

  struct Pattern {
    static const uint8_t MAX_COUNT = 32;
    bool HasNumber(uint8_t number) const { return mNumber == number; }
    const uint8_t *DataOffset(uint8_t channel) const { return mDataOffset[channel]; }

   private:
    uint8_t mNumber;
    uint8_t mDataOffset[3][2];
  };

  struct Channel {
    void SetNote(uint8_t note) {
      mNote = note;
      mSamplePosition = 0;
      mSampleCounter = 32;
    }
    void Disable() { mSampleCounter = 0; }
    bool IsEnabled() const { return mSampleCounter > 0; }

    void SetPatternData(const uint8_t *data) { mPatternIt = data; }
    uint8_t PatternCode() { return *mPatternIt++; }

    void SetSample(const Sample *sample) { mSample = sample; }
    void SetOrnament(const Ornament *ornament) { mOrnament = ornament->Data(); }
    void AdvanceSample();

    const SampleData *GetSampleData() const { return mSample->Data(mSamplePosition); }
    uint8_t GetOrnamentNote() const { return mNote + mOrnament[mSamplePosition]; }

    bool IsEnvelopeEnabled() const { return mEnvelope; }
    void EnvelopeEnable() { mEnvelope = true; }
    void EnvelopeDisable() { mEnvelope = false; }

    void SetSkipCount(uint8_t delay) { mSkipCount = mSkipCounter = delay; }
    bool IsEmptyLocation();

    static uint16_t GetTonePeriod(uint8_t tone);

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

  struct STCModule {
    // Get song global delay.
    uint8_t GetDelay() const { return mDelay; }

    // Begin position iterator.
    const Position *GetPositionBegin() const;

    // End position iterator.
    const Position *GetPositionEnd() const;

    // Get pattern by specified number.
    const Pattern *GetPattern(uint8_t number) const;

    // Get pattern data by specified number.
    const uint8_t *GetPatternData(uint8_t pattern, uint8_t channel) const;

    // Get data from specified pattern.
    const uint8_t *GetPatternData(const Pattern *pattern, uint8_t channel) const;

    // Get sample by specified number.
    const Sample *GetSample(uint8_t number) const;

    // Get data of specified ornament number.
    const Ornament *GetOrnament(uint8_t number) const;

    // Return song length in frames.
    unsigned CountSongLength() const;

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
  bool mRunDelay();
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
  blip_clk_time_t mPlayPeriod;
  // Global song delay
  uint8_t mDelayCounter;
};

}  // namespace ay
}  // namespace emu
}  // namespace gme
