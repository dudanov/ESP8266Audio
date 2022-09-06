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

static const uint32_t CLK_SPECTRUM = 3546900;

inline uint16_t StcEmu::Channel::GetTonePeriod(uint8_t tone) {
  static const uint16_t TABLE[] PROGMEM = {
      0x0EF8, 0x0E10, 0x0D60, 0x0C80, 0x0BD8, 0x0B28, 0x0A88, 0x09F0, 0x0960, 0x08E0, 0x0858, 0x07E0, 0x077C, 0x0708,
      0x06B0, 0x0640, 0x05EC, 0x0594, 0x0544, 0x04F8, 0x04B0, 0x0470, 0x042C, 0x03F0, 0x03BE, 0x0384, 0x0358, 0x0320,
      0x02F6, 0x02CA, 0x02A2, 0x027C, 0x0258, 0x0238, 0x0216, 0x01F8, 0x01DF, 0x01C2, 0x01AC, 0x0190, 0x017B, 0x0165,
      0x0151, 0x013E, 0x012C, 0x011C, 0x010B, 0x00FC, 0x00EF, 0x00E1, 0x00D6, 0x00C8, 0x00BD, 0x00B2, 0x00A8, 0x009F,
      0x0096, 0x008E, 0x0085, 0x007E, 0x0077, 0x0070, 0x006B, 0x0064, 0x005E, 0x0059, 0x0054, 0x004F, 0x004B, 0x0047,
      0x0042, 0x003F, 0x003B, 0x0038, 0x0035, 0x0032, 0x002F, 0x002C, 0x002A, 0x0027, 0x0025, 0x0023, 0x0021, 0x001F,
      0x001D, 0x001C, 0x001A, 0x0019, 0x0017, 0x0016, 0x0015, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F,
  };
  return pgm_read_word(TABLE + ((tone <= 95) ? tone : 95));
}

StcEmu::StcEmu() {
  static const char *const CHANNELS_NAMES[] = {"Wave 1", "Wave 2", "Wave 3"};
  static int const CHANNELS_TYPES[] = {WAVE_TYPE | 0, WAVE_TYPE | 1, WAVE_TYPE | 2};
  mSetType(gme_stc_type);
  mSetChannelsNames(CHANNELS_NAMES);
  mSetChannelsTypes(CHANNELS_TYPES);
  mSetSilenceLookahead(6);
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

void StcEmu::mSetTempo(double temp) {
  mPlayPeriod = static_cast<blip_clk_time_t>(mGetClockRate() / 50 / temp);
}

blargg_err_t StcEmu::mStartTrack(int track) {
  RETURN_ERR(ClassicEmu::mStartTrack(track));
  mInit();
  SetTempo(mGetTempo());
  return nullptr;
}

void StcEmu::mInit() {
  mApu.Reset();
  mEmuTime = 0;
  mDelay = 1;
  mPositionIt = mModule->GetPositionBegin();
  auto pattern = mModule->GetPattern(mPositionIt->pattern);
  for (unsigned idx = 0; idx < 3; ++idx) {
    auto &c = mChannel[idx];
    c.Reset();
    c.SetPatternData(mModule->GetPatternData(pattern, idx));
    c.SetOrnamentData(mModule->GetOrnamentData(0));
  }
}

/* STC MODULE */

inline int16_t StcEmu::SampleData::Transposition() const {
  int16_t result = mData[0] / 16 * 256 + mData[2];
  return (mData[1] & 32) ? result : -result;
}

inline const StcEmu::Position *StcEmu::STCModule::GetPositionBegin() const {
  return mGetPointer<PositionsTable>(mPositions)->position;
}

inline const StcEmu::Position *StcEmu::STCModule::GetPositionEnd() const {
  auto p = mGetPointer<PositionsTable>(mPositions);
  return p->position + p->count + 1;
}

inline size_t StcEmu::STCModule::mGetPositionsCount() const {
  return mGetPointer<PositionsTable>(mPositions)->count + 1;
}

inline const uint8_t *StcEmu::STCModule::GetPatternData(const StcEmu::Pattern *pattern, uint8_t channel) const {
  return mGetPointer<uint8_t>(pattern->DataOffset(channel));
}

inline const uint8_t *StcEmu::STCModule::GetPatternData(uint8_t pattern, uint8_t channel) const {
  return GetPatternData(GetPattern(pattern), channel);
}

inline const StcEmu::Pattern *StcEmu::STCModule::mGetPatternBegin() const { return mGetPointer<Pattern>(mPatterns); }

inline const StcEmu::Pattern *StcEmu::STCModule::GetPattern(uint8_t number) const {
  auto it = mGetPatternBegin();
  while (!it->HasNumber(number))
    ++it;
  return it;
}

inline const uint8_t *StcEmu::STCModule::GetOrnamentData(uint8_t number) const {
  auto it = mGetPointer<Ornament>(mOrnaments);
  while (!it->HasNumber(number))
    ++it;
  return it->Data();
}

inline const StcEmu::Sample *StcEmu::STCModule::GetSample(uint8_t number) const {
  auto it = mSamples;
  while (!it->HasNumber(number))
    ++it;
  return it;
}

bool StcEmu::STCModule::mCheckPatternTable() const {
  auto it = mGetPatternBegin();
  for (uint8_t n = 0; n < Pattern::MAX_COUNT; ++n, ++it) {
    if (it->HasNumber(0xFF))
      return true;
  }
  return false;
}

const StcEmu::Pattern *StcEmu::STCModule::mFindPattern(uint8_t number) const {
  for (auto it = mGetPatternBegin(); it != mGetPatternEnd(); ++it) {
    if (it->HasNumber(number))
      return it;
  }
  return nullptr;
}

uint8_t StcEmu::STCModule::mCountPatternLength(const StcEmu::Pattern *pattern, uint8_t channel) const {
  unsigned length = 0, skip = 0;
  for (auto it = GetPatternData(pattern, channel); *it != 0xFF; ++it) {
    const uint8_t data = *it;
    if ((data < 0x60) || (data == 0x80) || (data == 0x81)) {
      length += skip;
    } else if (data < 0x83) {
      ;
    } else if (data < 0x8F) {
      ++it;
    } else if (data > 0xA0 && data < 0xE1) {
      skip = data - 0xA0;
    } else {
      // wrong code
      return 0;
    }
  }
  return (length <= 64) ? length : 0;
}

bool StcEmu::STCModule::mCheckSongData() const {
  uint8_t PatternLength = 0;
  for (auto PositionIt = GetPositionBegin(); PositionIt != GetPositionEnd(); ++PositionIt) {
    auto pattern = mFindPattern(PositionIt->pattern);
    if (pattern == nullptr)
      return false;
    for (uint8_t channel = 0; channel < 3; ++channel) {
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

bool StcEmu::STCModule::CheckIntegrity(size_t size) const {
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

unsigned StcEmu::STCModule::CountSongLength() const {
  // all patterns has same length
  return mCountPatternLength(GetPattern(GetPositionBegin()->pattern)) * mGetPositionsCount();
}

void StcEmu::mPlayPattern() {
  for (auto &chan : mChannel) {
    while (chan.IsPlayTime()) {
      const uint8_t code = chan.PatternCode();
      if (code == 0xFF) {
        if (!mAdvancePosition())
          mSetTrackEnded();
        continue;
      } else if (code < 0x60) {
        // Note in semitones (00=C-1). End position.
        chan.SetNote(code);
        chan.AdvancePattern();
        break;
      } else if (code < 0x70) {
        // Bits 0-3 = sample number
        chan.SetSample(mModule->GetSample(code % 16));
      } else if (code < 0x80) {
        // Bits 0-3 = ornament number
        chan.SetOrnamentData(mModule->GetOrnamentData(code % 16));
        chan.EnvelopeOff();
      } else if (code == 0x80) {
        // Rest (shuts channel). End position.
        chan.TurnOff();
        chan.AdvancePattern();
        break;
      } else if (code == 0x81) {
        // Empty location. End position.
        chan.AdvancePattern();
        break;
      } else if (code == 0x82) {
        // Select ornament 0.
        chan.SetOrnamentData(mModule->GetOrnamentData(0));
        chan.EnvelopeOff();
      } else if (code < 0x8F) {
        // Select envelope effect.
        mApu.Write(mEmuTime, 13, code % 16);
        mApu.Write(mEmuTime, 11, chan.AdvancePattern());
        chan.EnvelopeOn();
        chan.SetOrnamentData(mModule->GetOrnamentData(0));
      } else {
        // number of empty locations after the subsequent code
        chan.SetDelay(code - 0xA1);
      }
      chan.AdvancePattern();
    }
  }
}

void StcEmu::mPlaySamples() {
  uint8_t mixer = 0;
  for (uint8_t idx = 0; idx < 3; ++idx) {
    Channel &channel = mChannel[idx];

    channel.AdvanceSample();

    if (!channel.IsOn()) {
      mApu.Write(mEmuTime, idx + 8, 0);
      mixer |= 64 + 8;
      mixer >>= 1;
      continue;
    }

    auto sample = channel.GetSampleData();

    if (!sample->NoiseMask())
      mApu.Write(mEmuTime, 6, sample->Noise());

    mixer |= 64 * sample->NoiseMask() | 8 * sample->ToneMask();
    mixer >>= 1;

    const uint8_t note = channel.OrnamentNote() + mPositionTransposition();
    const uint16_t period = (Channel::GetTonePeriod(note) + sample->Transposition()) % 4096;

    mApu.Write(mEmuTime, idx * 2, period % 256);
    mApu.Write(mEmuTime, idx * 2 + 1, period / 256);
    mApu.Write(mEmuTime, idx + 8, sample->Volume() + 16 * channel.IsEnvelopeOn());

    //channel.AdvanceSample();
  }
  mApu.Write(mEmuTime, 7, mixer);
}

blargg_err_t StcEmu::mRunClocks(blip_clk_time_t &duration) {
  for (; mEmuTime <= duration; mEmuTime += mPlayPeriod) {
    if (--mDelay == 0) {
      mDelay = mModule->GetDelay();
      mPlayPattern();
    }
    mPlaySamples();
  }

  mEmuTime -= duration;
  mApu.EndFrame(duration);
  return nullptr;
}

}  // namespace ay
}  // namespace emu
}  // namespace gme

static const gme_type_t_ gme_stc_type_ = {
    "ZX Spectrum", 1, 0, &gme::emu::ay::StcEmu::createStcEmu, &gme::emu::ay::StcFile::createStcFile, "STC", 1,
};
extern gme_type_t const gme_stc_type = &gme_stc_type_;
