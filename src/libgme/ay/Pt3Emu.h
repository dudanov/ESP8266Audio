#pragma once

// Sinclair Spectrum PT3 music file emulator

#include "AyApu.h"
#include "../ClassicEmu.h"

namespace gme {
namespace emu {
namespace ay {
namespace pt3 {

/* PT3 MODULE DATA DESCRIPTION */

template<typename T> struct LoopData {
  uint8_t loop;
  uint8_t end;
  T data[0];
};

template<typename T> class LoopDataPlayer {
 public:
  inline void Load(const LoopData<T> *data) {
    mData = data->data;
    mPos = 0;
    mEnd = data->end;
    mLoop = data->loop;
  }
  inline void Reset() { mPos = 0; }
  inline const T &Play() {
    const T &data = mData[mPos];
    if (++mPos >= mEnd)
      mPos = mLoop;
    return data;
  }

 private:
  const T *mData;
  uint8_t mPos, mEnd, mLoop;
};

struct SampleData {
  SampleData() = delete;
  SampleData(const SampleData &) = delete;
  bool EnvelopeMask() const { return mData[0] & 1; }
  uint8_t Noise() const { return mData[0] / 2 % 32; }
  int16_t EnvelopeSlide() const {
    const int16_t val = mData[0] >> 1;
    return (val & 16) ? (val | ~15) : (val & 15);
  }
  uint8_t Volume() const { return mData[1] % 16; }
  bool ToneMask() const { return mData[1] & 16; }
  bool NoiseMask() const { return mData[1] & 128; }
  int16_t Transposition() const { return get_le16(mTransposition); }

 private:
  uint8_t mData[2];
  uint8_t mTransposition[2];
};

using PatternData = uint8_t;
using OrnamentData = int8_t;
using Sample = LoopData<SampleData>;
using Ornament = LoopData<OrnamentData>;
using SamplePlayer = LoopDataPlayer<SampleData>;
using OrnamentPlayer = LoopDataPlayer<OrnamentData>;
using Position = uint8_t;

class PT3Module {
  struct DataOffset {
    DataOffset() = delete;
    DataOffset(const DataOffset &) = delete;
    uint16_t GetDataOffset() const { return get_le16(mOffset); }
    bool IsValid() const { return GetDataOffset() != 0; }

   private:
    uint8_t mOffset[2];
  };

  struct Pattern {
    Pattern() = delete;
    Pattern(const Pattern &) = delete;
    const DataOffset &GetChannelDataOffset(uint8_t channel) const { return mData[channel]; }

   private:
    DataOffset mData[3];
  };

 public:
  PT3Module() = delete;
  PT3Module(const PT3Module &) = delete;

  static const PT3Module *GetModule(const uint8_t *data, size_t size);
  static const PT3Module *FindTSModule(const uint8_t *data, size_t size);

  // Get module format subversion.
  uint8_t GetSubVersion() const;

  // Get song global delay.
  uint8_t GetDelay() const { return mDelay; }

  bool HasNoteTable(uint8_t table) const { return mNoteTable == table; }

  // Begin position iterator.
  const Position *GetPositionBegin() const { return mPositions; }

  // Loop position iterator.
  const Position *GetPositionLoop() const { return mPositions + mLoop; }

  // Get pattern index by specified number.
  const Pattern *GetPattern(const Position *it) const {
    return reinterpret_cast<const Pattern *>(mGetPointer<DataOffset>(mPattern) + *it);
  }

  // Get data from specified pattern.
  const PatternData *GetPatternData(const Pattern *pattern, uint8_t channel) const {
    return mGetPointer<PatternData>(pattern->GetChannelDataOffset(channel));
  }

  // Get sample by specified number.
  const Sample *GetSample(uint8_t number) const { return mGetPointer<Sample>(mSamples[number]); }

  // Get data of specified ornament number.
  const Ornament *GetOrnament(uint8_t number) const { return mGetPointer<Ornament>(mOrnaments[number]); }

  // Return song length in frames.
  unsigned CountSongLength() const;

  // Return song length in miliseconds.
  unsigned CountSongLengthMs() const;

  // Check file integrity.
  bool CheckIntegrity(size_t size) const;

 private:
  template<typename T> const T *mGetPointer(const DataOffset &offset) const {
    return reinterpret_cast<const T *>(mIdentify + offset.GetDataOffset());
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

  /* PT3 MODULE HEADER DATA */

  // Identification: "ProTracker 3.".
  uint8_t mIdentify[13];
  // Subversion: "3", "4", "5", "6", etc.
  uint8_t mSubVersion;
  // " compilation of " or any text of this length.
  uint8_t mUnused0[16];
  // Track name. Unused characters are padded with spaces.
  uint8_t mName[32];
  // " by " or any text of this length.
  uint8_t mUnused1[4];
  // Author's name. Unused characters are padded with spaces.
  uint8_t mAuthor[32];
  // One space (any character).
  uint8_t mUnused2;
  // Note frequency table number.
  uint8_t mNoteTable;
  // Delay value (tempo).
  uint8_t mDelay;
  // Song end position. Not used in player.
  uint8_t mEnd;
  // Song loop position.
  uint8_t mLoop;
  // Pattern table offset.
  DataOffset mPattern;
  // Sample offsets. Starting from sample #0.
  DataOffset mSamples[32];
  // Ornament offsets. Starting from ornament #0.
  DataOffset mOrnaments[16];
  // List of positions. Contains the pattern numbers (0...84) multiplied by 3. The table ends with 0xFF.
  Position mPositions[0];
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

  void SetSample(const PT3Module *pt3, uint8_t number) { mSamplePlayer.Load(pt3->GetSample(number)); }
  void SetOrnament(const PT3Module *pt3, uint8_t number) { mOrnamentPlayer.Load(pt3->GetOrnament(number)); }
  void AdvanceSample();

  const SampleData &GetSampleData() { return mSamplePlayer.Play(); }
  uint8_t GetOrnamentNote() { return mNote + mOrnamentPlayer.Play(); }

  bool IsEnvelopeEnabled() const { return mEnvelope; }
  void EnvelopeEnable() { mEnvelope = true; }
  void EnvelopeDisable() { mEnvelope = false; }

  void SetSkipCount(uint8_t delay) { mSkipCount = mSkipCounter = delay; }
  bool IsEmptyLocation();

 private:
  SamplePlayer mSamplePlayer;
  OrnamentPlayer mOrnamentPlayer;

  // Pattern data iterator.
  const uint8_t *mPatternIt;
  uint8_t mNote;
  uint8_t mSamplePosition;
  uint8_t mSampleCounter;
  uint8_t mSkipCounter;
  uint8_t mSkipCount;
  bool mEnvelope;

  uint16_t Address_In_Pattern, OrnamentPointer, SamplePointer, Ton;
  uint8_t Loop_Ornament_Position, Ornament_Length, Position_In_Ornament, Loop_Sample_Position, Sample_Length,
      Position_In_Sample, Volume, Number_Of_Notes_To_Skip, Note, Slide_To_Note, Amplitude;
  bool Envelope_Enabled, Enabled, SimpleGliss;
  int16_t Current_Amplitude_Sliding, Current_Noise_Sliding, Current_Envelope_Sliding, Ton_Slide_Count, Current_OnOff,
      OnOff_Delay, OffOn_Delay, Ton_Slide_Delay, Current_Ton_Sliding, Ton_Accumulator, Ton_Slide_Step, Ton_Delta;
  int8_t Note_Skip_Counter;
};

class Player {
 public:
  void SetVolume(double volume) { mApu.SetVolume(volume); }
  void SetOscOutput(int idx, BlipBuffer *out) { mApu.SetOscOutput(idx, out); }
  void RunClocks(blip_clk_time_t time);

 private:
  uint8_t mGetAmplitude(uint8_t volume, uint8_t amplitude) const;
  uint16_t mGetTonePeriod(int8_t tone) const;
  void mUpdateTables();
  void mPlayPattern();
  void mPlaySamples();
  void mAdvancePosition();
  // AY APU Emulator
  AyApu mApu;
  // Channels
  std::array<Channel, AyApu::OSCS_NUM> mChannels;
  // Song file header
  const PT3Module *mModule;
  // Song position iterators
  const Position *mPositionIt;
  // Pointer to notes period table
  const uint16_t *mNoteTable;
  // Pointer to volume period table
  const uint8_t *mVolumeTable;
  // Module subversion
  uint8_t mSubVersion;
  // Global song delay counter
  uint8_t mDelayCounter;

  unsigned char Env_Base_lo;
  unsigned char Env_Base_hi;
  short Cur_Env_Slide, Env_Slide_Add;
  signed char Cur_Env_Delay, Env_Delay;
  unsigned char Noise_Base, Delay, AddToNoise, DelayCounter, CurrentPosition;
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

  void mInit();

 private:
  // Player
  Player mPlayer;
  // TurboSound player
  Player *mTurboSound;
  // Current emulation time
  blip_clk_time_t mEmuTime;
  // Play period 50Hz
  blip_clk_time_t mFramePeriod;
};

}  // namespace pt3
}  // namespace ay
}  // namespace emu
}  // namespace gme
