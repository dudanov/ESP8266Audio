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
    uint8_t GetVolume() const { return mData[0] % 16; }
    uint8_t GetNoise() const { return mData[1] % 32; }
    int16_t GetTransposition() const {
      int16_t result = mData[0] / 16 * 256 + mData[2];
      return (mData[1] & 32) ? result : -result;
    }
    bool GetToneMask() const { return mData[1] & 64; }
    bool GetNoiseMask() const { return mData[1] & 128; }

   private:
    uint8_t mData[3];
  };

  struct Sample {
    uint8_t number;
    SampleData data[32];
    uint8_t repeat_pos;
    uint8_t repeat_len;
  };

  struct Ornament {
    uint8_t number;
    uint8_t data[32];
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
    uint8_t number;
    uint8_t data_offset[3][2];
  };

  struct Channel {
    const Sample *SamplePointer;
    const uint8_t *OrnamentPointer, *PatternDataIt;
    uint16_t Ton;
    uint8_t Amplitude, Note, PositionInSample, NumberOfNotesToSkip;
    char SampleTikCounter, NoteSkipCounter;
    bool EnvelopeEnabled;
  };

  enum { HEADER_SIZE = 27 };

  struct STCModule {
    // Get song global delay.
    uint8_t GetDelay() const { return mDelay; }

    // Begin position iterator.
    const Position *GetPositionBegin() const { return ptr<PositionsTable>(mPositions)->position; }

    // End position iterator.
    const Position *GetPositionEnd() const {
      auto p = ptr<PositionsTable>(mPositions);
      return p->position + p->count + 1;
    }

    // Get pattern by specified number.
    const Pattern *GetPattern(uint8_t number) const;

    // Get pattern data by specified number.
    const uint8_t *GetPatternData(uint8_t pattern, uint8_t channel) const {
      return GetPatternData(GetPattern(pattern), channel);
    }

    // Get data from specified pattern.
    const uint8_t *GetPatternData(const Pattern *pattern, uint8_t channel) const {
      return ptr<uint8_t>(pattern->data_offset[channel]);
    }

    // Get sample by specified number.
    const Sample *GetSample(uint8_t number) const;

    // Get data of specified ornament number.
    const uint8_t *GetOrnamentData(uint8_t number) const;

    // Return song length in frames.
    unsigned CountSongLength() const;

    // Check file integrity.
    bool CheckIntegrity() const;

   private:
    template<typename T> const T *ptr(const uint8_t *offset) const {
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

    size_t mGetPositionsCount() const { return ptr<PositionsTable>(mPositions)->count + 1; }

    const Pattern *mGetPatternBegin() const { return ptr<Pattern>(mPatterns); }

    const Pattern *mGetPatternEnd() const { return GetPattern(0xFF); }

    uint8_t mDelay;
    uint8_t mPositions[2];
    uint8_t mOrnaments[2];
    uint8_t mPatterns[2];
    char mName[18];
    uint8_t mSize[2];
    Sample mSamples[0];
  };

  bool Load(const uint8_t *data, size_t size) {
    mModule = reinterpret_cast<const STCModule *>(data);
    if (!mModule->CheckIntegrity())
      return false;
    mPositionIt = mModule->GetPositionBegin();
    mPositionEnd = mModule->GetPositionEnd();
    return mUpdate();
  }

  // Update pattern data pointers for current position.
  bool mUpdate() {
    auto pattern = mModule->GetPattern(mPositionIt->pattern);
    for (unsigned idx = 0; idx < 3; ++idx) {
      auto &p = mChannel[idx];
      p.PatternDataIt = mModule->GetPatternData(pattern, idx);
    }
    return true;
  }

  uint8_t mPositionTransposition() const { return mPositionIt->transposition; }

  bool CheckIntegrity() const { return mModule->CheckIntegrity(); }

  bool mAdvancePosition() {
    if (++mPositionIt < mPositionEnd)
      return mUpdate();
    return false;
  }

  void PatternInterpreter(AyApu &apu, blip_clk_time_t time);

 protected:
  blargg_err_t mLoad(const uint8_t *data, long size) override;
  blargg_err_t mStartTrack(int) override;
  blargg_err_t mGetTrackInfo(track_info_t *, int track) const override;
  blargg_err_t mRunClocks(blip_clk_time_t &) override;
  void mSetTempo(double) override;
  void mSetChannel(int, BlipBuffer *, BlipBuffer *, BlipBuffer *) override;
  void mUpdateEq(BlipEq const &) override;
  void mSeekFrame(uint32_t frame);
  blargg_err_t mWriteRegisters();
  void PatternInterpreter(blip_clk_time_t time, Channel &chan);
  void GetRegisters(Channel &chan, uint8_t &TempMixer);

 private:
  AyApu mApu;
  std::array<Channel, AyApu::OSCS_NUM> mChannel;
  const STCModule *mModule;
  const Position *mPositionIt, *mPositionEnd;
  blip_clk_time_t mPlayPeriod;
  blip_clk_time_t mNextPlay;
};

}  // namespace ay
}  // namespace emu
}  // namespace gme
