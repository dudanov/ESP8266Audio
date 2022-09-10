#include "StcEmu.h"
#include "../blargg_endian.h"
#include "../blargg_source.h"

#include <cstring>
#include <pgmspace.h>

/*
  Copyright (C) 2022 Sergey Dudanov. This module is free software; you
  can redistribute it and/or modify it under the terms of the GNU Lesser
  General Public License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version. This
  module is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
  details. You should have received a copy of the GNU Lesser General Public
  License along with this module; if not, write to the Free Software Foundation,
  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

namespace gme {
namespace emu {
namespace ay {
namespace stc {

static const uint32_t CLK_SPECTRUM = 3546900;

/* STC MODULE */

const uint16_t STCModule::NOTE_TABLE[] PROGMEM = {
    0x0EF8, 0x0E10, 0x0D60, 0x0C80, 0x0BD8, 0x0B28, 0x0A88, 0x09F0, 0x0960, 0x08E0, 0x0858, 0x07E0, 0x077C, 0x0708,
    0x06B0, 0x0640, 0x05EC, 0x0594, 0x0544, 0x04F8, 0x04B0, 0x0470, 0x042C, 0x03F0, 0x03BE, 0x0384, 0x0358, 0x0320,
    0x02F6, 0x02CA, 0x02A2, 0x027C, 0x0258, 0x0238, 0x0216, 0x01F8, 0x01DF, 0x01C2, 0x01AC, 0x0190, 0x017B, 0x0165,
    0x0151, 0x013E, 0x012C, 0x011C, 0x010B, 0x00FC, 0x00EF, 0x00E1, 0x00D6, 0x00C8, 0x00BD, 0x00B2, 0x00A8, 0x009F,
    0x0096, 0x008E, 0x0085, 0x007E, 0x0077, 0x0070, 0x006B, 0x0064, 0x005E, 0x0059, 0x0054, 0x004F, 0x004B, 0x0047,
    0x0042, 0x003F, 0x003B, 0x0038, 0x0035, 0x0032, 0x002F, 0x002C, 0x002A, 0x0027, 0x0025, 0x0023, 0x0021, 0x001F,
    0x001D, 0x001C, 0x001A, 0x0019, 0x0017, 0x0016, 0x0015, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F,
};

inline uint16_t STCModule::GetTonePeriod(uint8_t tone) {
  return pgm_read_word(STCModule::NOTE_TABLE + ((tone <= 95) ? tone : 95));
}

inline const Position *STCModule::GetPositionBegin() const { return mGetPointer<PositionsTable>(mPositions)->position; }

inline const Position *STCModule::GetPositionEnd() const {
  auto p = mGetPointer<PositionsTable>(mPositions);
  return p->position + p->count + 1;
}

inline size_t STCModule::mGetPositionsCount() const { return mGetPointer<PositionsTable>(mPositions)->count + 1; }

inline const uint8_t *STCModule::GetPatternData(const Pattern *pattern, uint8_t channel) const {
  return mGetPointer<uint8_t>(pattern->DataOffset(channel));
}

inline const uint8_t *STCModule::GetPatternData(uint8_t pattern, uint8_t channel) const {
  return GetPatternData(GetPattern(pattern), channel);
}

inline const Pattern *STCModule::mGetPatternBegin() const { return mGetPointer<Pattern>(mPatterns); }

inline const Pattern *STCModule::GetPattern(uint8_t number) const {
  auto it = mGetPatternBegin();
  while (!it->HasNumber(number))
    ++it;
  return it;
}

inline const Ornament *STCModule::GetOrnament(uint8_t number) const {
  auto it = mGetPointer<Ornament>(mOrnaments);
  while (!it->HasNumber(number))
    ++it;
  return it;
}

inline const Sample *STCModule::GetSample(uint8_t number) const {
  auto it = mSamples;
  while (!it->HasNumber(number))
    ++it;
  return it;
}

bool STCModule::mCheckPatternTable() const {
  auto it = mGetPatternBegin();
  for (uint8_t n = 0; n != Pattern::MAX_COUNT; ++n, ++it) {
    if (it->HasNumber(0xFF))
      return true;
  }
  return false;
}

const Pattern *STCModule::mFindPattern(uint8_t number) const {
  for (auto it = mGetPatternBegin(); it != mGetPatternEnd(); ++it) {
    if (it->HasNumber(number))
      return it;
  }
  return nullptr;
}

uint8_t STCModule::mCountPatternLength(const Pattern *pattern, uint8_t channel) const {
  unsigned length = 0, skip = 0;
  for (auto it = GetPatternData(pattern, channel); *it != 0xFF; ++it) {
    const uint8_t data = *it;
    if ((data <= 0x5F) || (data == 0x80) || (data == 0x81)) {
      length += skip;
    } else if (data <= 0x82) {
      ;
    } else if (data <= 0x8E) {
      ++it;
    } else if (data >= 0xA1 && data <= 0xE0) {
      skip = data - 0xA0;
    } else {
      // wrong code
      return 0;
    }
  }
  return (length <= 64) ? length : 0;
}

bool STCModule::mCheckSongData() const {
  uint8_t PatternLength = 0;
  for (auto PositionIt = GetPositionBegin(); PositionIt != GetPositionEnd(); ++PositionIt) {
    auto pattern = mFindPattern(PositionIt->pattern);
    if (pattern == nullptr)
      return false;
    for (uint8_t channel = 0; channel != AyApu::OSCS_NUM; ++channel) {
      const uint8_t length = mCountPatternLength(pattern, channel);
      if (length == 0)
        return false;
      if (PatternLength != 0) {
        if (PatternLength == length)
          continue;
        return false;
      }
      PatternLength = length;
    }
  }
  return true;
}

bool STCModule::CheckIntegrity(size_t size) const {
  // Header size
  if (size <= sizeof(STCModule))
    return false;

  // Checking samples section
  constexpr uint16_t SamplesBlockOffset = sizeof(STCModule);
  const uint16_t PositionsTableOffset = get_le16(mPositions);
  if (PositionsTableOffset <= SamplesBlockOffset)
    return false;
  const uint16_t SamplesBlockSize = PositionsTableOffset - SamplesBlockOffset;
  if (SamplesBlockSize % sizeof(Sample))
    return false;

  // Checking positions section
  const uint16_t PositionsBlockOffset = PositionsTableOffset + sizeof(PositionsTable);
  const uint16_t OrnamentsBlockOffset = get_le16(mOrnaments);
  if (OrnamentsBlockOffset <= PositionsBlockOffset)
    return false;
  const uint16_t PositionsBlockSize = OrnamentsBlockOffset - PositionsBlockOffset;
  if (PositionsBlockSize % sizeof(Position))
    return false;
  if (PositionsBlockSize / sizeof(Position) != mGetPositionsCount())
    return false;

  // Checking ornaments section
  const uint16_t PatternsBlockOffset = get_le16(mPatterns);
  if (PatternsBlockOffset <= OrnamentsBlockOffset)
    return false;
  const uint16_t OrnamentsBlockSize = PatternsBlockOffset - OrnamentsBlockOffset;
  if (OrnamentsBlockSize % sizeof(Ornament))
    return false;

  if (size <= OrnamentsBlockOffset)
    return false;

  // Checking pattern and song data
  if (!mCheckPatternTable() || !mCheckSongData())
    return false;

  return true;
}

unsigned STCModule::CountSongLength() const {
  // all patterns has same length
  return mCountPatternLength(GetPattern(GetPositionBegin()->pattern)) * mGetPositionsCount() * mDelay;
}

/* CHANNEL */

inline void Channel::AdvanceSample() {
  if (--mSampleCounter) {
    ++mSamplePosition;
  } else if (mSample->IsRepeatable()) {
    mSamplePosition = mSample->RepeatPosition();
    mSampleCounter = mSample->RepeatLength();
  }
}

inline bool Channel::IsEmptyLocation() {
  if (mSkipCounter > 0) {
    mSkipCounter--;
    return true;
  }
  mSkipCounter = mSkipCount;
  return false;
}

/* SAMPLE DATA */

inline int16_t SampleData::Transposition() const {
  int16_t result = mData[0] / 16 * 256 + mData[2];
  return (mData[1] & 32) ? result : -result;
}

/* STC EMULATOR */

StcEmu::StcEmu() {
  static const char *const CHANNELS_NAMES[] = {"Wave 1", "Wave 2", "Wave 3"};
  static int const CHANNELS_TYPES[] = {WAVE_TYPE | 0, WAVE_TYPE | 1, WAVE_TYPE | 2};
  mSetType(gme_stc_type);
  mSetChannelsNames(CHANNELS_NAMES);
  mSetChannelsTypes(CHANNELS_TYPES);
  mSetSilenceLookahead(1);
}

StcEmu::~StcEmu() {}

blargg_err_t StcEmu::mGetTrackInfo(track_info_t *out, int track) const {
  // copy_stc_fields(mFile, *out);
  return nullptr;
}

struct StcFile : GmeInfo {
  StcFile() { mSetType(gme_stc_type); }
  static MusicEmu *createStcFile() { return BLARGG_NEW StcFile; }

  blargg_err_t mLoad(uint8_t const *begin, long size) override {
    //    RETURN_ERR(parse_header(begin, size, file));
    mSetTrackNum(1);
    return nullptr;
  }

  blargg_err_t mGetTrackInfo(track_info_t *out, int track) const override {
    //  copy_stc_fields(file, *out);
    return nullptr;
  }
};

// Setup

blargg_err_t StcEmu::mLoad(const uint8_t *data, long size) {
  mModule = reinterpret_cast<const STCModule *>(data);
  if (!mModule->CheckIntegrity(size))
    return gme_wrong_file_type;
  mPositionEnd = mModule->GetPositionEnd();
  mSetTrackNum(1);
  mSetChannelsNumber(AyApu::OSCS_NUM);
  mApu.SetVolume(mGetGain());
  return mSetupBuffer(CLK_SPECTRUM);
}

void StcEmu::mUpdateEq(BlipEq const &eq) { mApu.SetTrebleEq(eq); }

void StcEmu::mSetChannel(int i, BlipBuffer *center, BlipBuffer *, BlipBuffer *) { mApu.SetOscOutput(i, center); }

// Emulation

void StcEmu::mSetTempo(double temp) { mPlayPeriod = static_cast<blip_clk_time_t>(mGetClockRate() / 50 / temp); }

blargg_err_t StcEmu::mStartTrack(int track) {
  RETURN_ERR(ClassicEmu::mStartTrack(track));
  mInit();
  SetTempo(mGetTempo());
  return nullptr;
}

void StcEmu::mInit() {
  mApu.Reset();
  mEmuTime = 0;
  mDelayCounter = 1;
  mPositionIt = mModule->GetPositionBegin();
  memset(&mChannels, 0, sizeof(mChannels));
  auto pattern = mModule->GetPattern(mPositionIt->pattern);
  for (uint8_t idx = 0; idx != mChannels.size(); ++idx) {
    Channel &c = mChannels[idx];
    c.SetPatternData(mModule->GetPatternData(pattern, idx));
    c.SetOrnament(mModule->GetOrnament(0));
  }
}

void StcEmu::mPlayPattern() {
  for (Channel &channel : mChannels) {
    if (channel.IsEmptyLocation())
      continue;
    while (true) {
      const uint8_t code = channel.PatternCode();
      if (code <= 0x5F) {
        // Note in semitones (00=C-1). End position.
        channel.SetNote(code);
        break;
      } else if (code <= 0x6F) {
        // Select sample (0-15).
        channel.SetSample(mModule->GetSample(code % 16));
      } else if (code <= 0x7F) {
        // Select ornament (0-15).
        channel.EnvelopeDisable();
        channel.SetOrnament(mModule->GetOrnament(code % 16));
      } else if (code == 0x80) {
        // Rest (shuts channel). End position.
        channel.Disable();
        break;
      } else if (code == 0x81) {
        // Empty location. End position.
        break;
      } else if (code == 0x82) {
        // Select ornament 0.
        channel.EnvelopeDisable();
        channel.SetOrnament(mModule->GetOrnament(0));
      } else if (code <= 0x8E) {
        // Select envelope effect (3-14).
        channel.EnvelopeEnable();
        channel.SetOrnament(mModule->GetOrnament(0));
        mApu.Write(mEmuTime, 13, code % 16);
        mApu.Write(mEmuTime, 11, channel.PatternCode());
      } else if (code == 0xFF) {
        // End pattern marker. Advance to next song position and update all channels.
        mAdvancePosition();
      } else {
        // Number of empty locations after the subsequent code (0-63).
        channel.SetSkipCount(code - 0xA1);
      }
    }
  }
}

inline void StcEmu::mAdvancePosition() {
  if (++mPositionIt == mPositionEnd) {
    mPositionIt = mModule->GetPositionBegin();
    mSetTrackEnded();
  }
  auto pattern = mModule->GetPattern(mPositionIt->pattern);
  for (uint8_t idx = 0; idx != mChannels.size(); ++idx)
    mChannels[idx].SetPatternData(mModule->GetPatternData(pattern, idx));
}

void StcEmu::mPlaySamples() {
  uint8_t mixer = 0;
  for (uint8_t idx = 0; idx != mChannels.size(); ++idx, mixer >>= 1) {
    Channel &channel = mChannels[idx];

    if (!channel.IsEnabled()) {
      mixer |= 64 | 8;
      continue;
    }

    auto sample = channel.GetSampleData();

    if (!sample->NoiseMask())
      mApu.Write(mEmuTime, 6, sample->Noise());

    mixer |= 64 * sample->NoiseMask() | 8 * sample->ToneMask();

    const uint8_t note = channel.GetOrnamentNote() + mPositionTransposition();
    const uint16_t period = (STCModule::GetTonePeriod(note) + sample->Transposition()) % 4096;

    mApu.Write(mEmuTime, idx * 2, period % 256);
    mApu.Write(mEmuTime, idx * 2 + 1, period / 256);
    mApu.Write(mEmuTime, idx + 8, sample->Volume() + 16 * channel.IsEnvelopeEnabled());

    channel.AdvanceSample();
  }
  mApu.Write(mEmuTime, 7, mixer);
}

inline bool StcEmu::mRunDelay() {
  if (--mDelayCounter > 0)
    return false;
  mDelayCounter = mModule->GetDelay();
  return true;
}

blargg_err_t StcEmu::mRunClocks(blip_clk_time_t &duration) {
  for (; mEmuTime <= duration; mEmuTime += mPlayPeriod) {
    if (mRunDelay())
      mPlayPattern();
    mPlaySamples();
  }
  mEmuTime -= duration;
  mApu.EndFrame(duration);
  return nullptr;
}

}  // namespace stc
}  // namespace ay
}  // namespace emu
}  // namespace gme

static const gme_type_t_ gme_stc_type_ = {
    "ZX Spectrum", 1, 0, &gme::emu::ay::stc::StcEmu::createStcEmu, &gme::emu::ay::stc::StcFile::createStcFile, "STC", 1,
};
extern gme_type_t const gme_stc_type = &gme_stc_type_;
