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
    bool GetToneMask() const { return mData[1] & 64; }
    bool GetNoiseMask() const { return mData[1] & 128; }
    int16_t GetTransposition() const;

   private:
    uint8_t mData[3];
  };

  struct Sample {
    uint8_t GetRepeatPosition() const { return repeat_pos; }
    const uint8_t GetRepeatLength() const { return repeat_len; }
    bool IsRepeatable() const { return repeat_pos; }
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
    void SampleOff() { SampleCounter = 0; }
    void SetNote(uint8_t note) {
      Note = note;
      SampleCounter = 32;
      SamplePosition = 0;
      PatternDataIt++;
    }
    void SetSample(const Sample *sample) { Sample = sample; }
    bool IsSampleOn() const { return SampleCounter; }
    void SetOrnamentData(const uint8_t *data) {
      Ornament = data;
      EnvelopeEnabled = false;
    }
    const SampleData *GetSampleData() const { return Sample->data + SamplePosition; }
    uint8_t GetOrnamentData() const { return Ornament[SamplePosition]; }
    bool SampleAdvance() {
      if (!IsSampleOn())
        return;
      if (--SampleCounter) {
        ++SamplePosition;
      } else if (Sample->IsRepeatable()) {
        SamplePosition = Sample->GetRepeatPosition();
        SampleCounter = Sample->GetRepeatLength();
      }
    }
    void Reset() { mPlayNext = 0; }
    blip_clk_time_t mPlayNext;
    const Sample *Sample;
    const uint8_t *Ornament, *PatternDataIt;
    uint16_t Tone;
    uint8_t Amplitude, Note, NumberOfNotesToSkip;
    uint8_t SamplePosition;
    uint8_t SampleCounter, NoteSkipCounter;
    bool EnvelopeEnabled;
  };

  void mPlaySample(Channel &channel) {
    if (!channel.IsSampleOn())
      return;
    auto data = channel.GetSampleData();
    if (data->GetNoiseMask())
      ;  // tunoff chip
    else
      mApu.Write(0, AyApu::R6, data->GetNoise());

    if (data->GetToneMask())
      ;  // turn | 8

    channel.Amplitude = data->GetVolume();
    uint8_t note = channel.Note + channel.GetOrnamentData() + mPositionTransposition();
    if (note > 95)
      note = 95;
    channel.Tone = pgm_read_word(&PERIODS[note]) + data->GetTransposition();
    if (channel.EnvelopeEnabled)
      channel.Amplitude |= 16;
    else
      channel.Amplitude = 0;

    TempMixer = TempMixer >> 1;
  }

  enum { HEADER_SIZE = 27 };

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
    const uint8_t *GetOrnamentData(uint8_t number) const;

    // Return song length in frames.
    unsigned CountSongLength() const;

    // Check file integrity.
    bool CheckIntegrity(size_t size) const;

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

  bool mAdvancePosition() {
    if (++mPositionIt != mPositionEnd)
      return mUpdate();
    return false;
  }

  void PatternInterpreter(blip_clk_time_t time);

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
};  // namespace ay

}  // namespace ay
}  // namespace emu
}  // namespace gme
