#include "Pt3Emu.h"
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
namespace pt3 {

static const auto CLOCK_RATE = CLK_SPECTRUM;
static const auto FRAME_RATE = FRAMERATE_SPECTRUM;

static const char PT_SIGNATURE[] PROGMEM = {
    'P', 'r', 'o', 'T', 'r', 'a', 'c', 'k', 'e', 'r', ' ', '3', '.',
};

static const char VT_SIGNATURE[] PROGMEM = {
    'V', 'o', 'r', 't', 'e', 'x', ' ', 'T', 'r', 'a', 'c', 'k', 'e', 'r', ' ', 'I', 'I',
};

static inline int8_t limit(int8_t value, int8_t min, int8_t max) {
  if (value >= max)
    return max;
  if (value <= min)
    return min;
  return value;
}

/* PT3 PLAYER */

int16_t Player::GetNotePeriod(const uint8_t tone) const {
  static const int16_t TABLE_PT[][96] PROGMEM = {
      // Table #0 of ProTracker 3.3x - 3.4r
      {0xC21, 0xB73, 0xACE, 0xA33, 0x9A0, 0x916, 0x893, 0x818, 0x7A4, 0x736, 0x6CE, 0x66D, 0x610, 0x5B9, 0x567, 0x519,
       0x4D0, 0x48B, 0x449, 0x40C, 0x3D2, 0x39B, 0x367, 0x336, 0x308, 0x2DC, 0x2B3, 0x28C, 0x268, 0x245, 0x224, 0x206,
       0x1E9, 0x1CD, 0x1B3, 0x19B, 0x184, 0x16E, 0x159, 0x146, 0x134, 0x122, 0x112, 0x103, 0x0F4, 0x0E6, 0x0D9, 0x0CD,
       0x0C2, 0x0B7, 0x0AC, 0x0A3, 0x09A, 0x091, 0x089, 0x081, 0x07A, 0x073, 0x06C, 0x066, 0x061, 0x05B, 0x056, 0x051,
       0x04D, 0x048, 0x044, 0x040, 0x03D, 0x039, 0x036, 0x033, 0x030, 0x02D, 0x02B, 0x028, 0x026, 0x024, 0x022, 0x020,
       0x01E, 0x01C, 0x01B, 0x019, 0x018, 0x016, 0x015, 0x014, 0x013, 0x012, 0x011, 0x010, 0x00F, 0x00E, 0x00D, 0x00C},
      // Table #0 of ProTracker 3.4x and above
      {0xC22, 0xB73, 0xACF, 0xA33, 0x9A1, 0x917, 0x894, 0x819, 0x7A4, 0x737, 0x6CF, 0x66D, 0x611, 0x5BA, 0x567, 0x51A,
       0x4D0, 0x48B, 0x44A, 0x40C, 0x3D2, 0x39B, 0x367, 0x337, 0x308, 0x2DD, 0x2B4, 0x28D, 0x268, 0x246, 0x225, 0x206,
       0x1E9, 0x1CE, 0x1B4, 0x19B, 0x184, 0x16E, 0x15A, 0x146, 0x134, 0x123, 0x112, 0x103, 0x0F5, 0x0E7, 0x0DA, 0x0CE,
       0x0C2, 0x0B7, 0x0AD, 0x0A3, 0x09A, 0x091, 0x089, 0x082, 0x07A, 0x073, 0x06D, 0x067, 0x061, 0x05C, 0x056, 0x052,
       0x04D, 0x049, 0x045, 0x041, 0x03D, 0x03A, 0x036, 0x033, 0x031, 0x02E, 0x02B, 0x029, 0x027, 0x024, 0x022, 0x020,
       0x01F, 0x01D, 0x01B, 0x01A, 0x018, 0x017, 0x016, 0x014, 0x013, 0x012, 0x011, 0x010, 0x00F, 0x00E, 0x00D, 0x00C},
  };

  static const int16_t TABLE_ST[] PROGMEM = {
      // Table #1 of ProTracker 3.3x and above
      0xEF8, 0xE10, 0xD60, 0xC80, 0xBD8, 0xB28, 0xA88, 0x9F0, 0x960, 0x8E0, 0x858, 0x7E0, 0x77C, 0x708, 0x6B0, 0x640,
      0x5EC, 0x594, 0x544, 0x4F8, 0x4B0, 0x470, 0x42C, 0x3FD, 0x3BE, 0x384, 0x358, 0x320, 0x2F6, 0x2CA, 0x2A2, 0x27C,
      0x258, 0x238, 0x216, 0x1F8, 0x1DF, 0x1C2, 0x1AC, 0x190, 0x17B, 0x165, 0x151, 0x13E, 0x12C, 0x11C, 0x10A, 0x0FC,
      0x0EF, 0x0E1, 0x0D6, 0x0C8, 0x0BD, 0x0B2, 0x0A8, 0x09F, 0x096, 0x08E, 0x085, 0x07E, 0x077, 0x070, 0x06B, 0x064,
      0x05E, 0x059, 0x054, 0x04F, 0x04B, 0x047, 0x042, 0x03F, 0x03B, 0x038, 0x035, 0x032, 0x02F, 0x02C, 0x02A, 0x027,
      0x025, 0x023, 0x021, 0x01F, 0x01D, 0x01C, 0x01A, 0x019, 0x017, 0x016, 0x015, 0x013, 0x012, 0x011, 0x010, 0x00F,
  };

  static const int16_t TABLE_ASM[][96] PROGMEM = {
      // Table #2 of ProTracker 3.4r
      {0xD3E, 0xC80, 0xBCC, 0xB22, 0xA82, 0x9EC, 0x95C, 0x8D6, 0x858, 0x7E0, 0x76E, 0x704, 0x69F, 0x640, 0x5E6, 0x591,
       0x541, 0x4F6, 0x4AE, 0x46B, 0x42C, 0x3F0, 0x3B7, 0x382, 0x34F, 0x320, 0x2F3, 0x2C8, 0x2A1, 0x27B, 0x257, 0x236,
       0x216, 0x1F8, 0x1DC, 0x1C1, 0x1A8, 0x190, 0x179, 0x164, 0x150, 0x13D, 0x12C, 0x11B, 0x10B, 0x0FC, 0x0EE, 0x0E0,
       0x0D4, 0x0C8, 0x0BD, 0x0B2, 0x0A8, 0x09F, 0x096, 0x08D, 0x085, 0x07E, 0x077, 0x070, 0x06A, 0x064, 0x05E, 0x059,
       0x054, 0x050, 0x04B, 0x047, 0x043, 0x03F, 0x03C, 0x038, 0x035, 0x032, 0x02F, 0x02D, 0x02A, 0x028, 0x026, 0x024,
       0x022, 0x020, 0x01E, 0x01D, 0x01B, 0x01A, 0x019, 0x018, 0x015, 0x014, 0x013, 0x012, 0x011, 0x010, 0x00F, 0x00E},
      // Table #2 of ProTracker 3.4x and above
      {0xD10, 0xC55, 0xBA4, 0xAFC, 0xA5F, 0x9CA, 0x93D, 0x8B8, 0x83B, 0x7C5, 0x755, 0x6EC, 0x688, 0x62A, 0x5D2, 0x57E,
       0x52F, 0x4E5, 0x49E, 0x45C, 0x41D, 0x3E2, 0x3AB, 0x376, 0x344, 0x315, 0x2E9, 0x2BF, 0x298, 0x272, 0x24F, 0x22E,
       0x20F, 0x1F1, 0x1D5, 0x1BB, 0x1A2, 0x18B, 0x174, 0x160, 0x14C, 0x139, 0x128, 0x117, 0x107, 0x0F9, 0x0EB, 0x0DD,
       0x0D1, 0x0C5, 0x0BA, 0x0B0, 0x0A6, 0x09D, 0x094, 0x08C, 0x084, 0x07C, 0x075, 0x06F, 0x069, 0x063, 0x05D, 0x058,
       0x053, 0x04E, 0x04A, 0x046, 0x042, 0x03E, 0x03B, 0x037, 0x034, 0x031, 0x02F, 0x02C, 0x029, 0x027, 0x025, 0x023,
       0x021, 0x01F, 0x01D, 0x01C, 0x01A, 0x019, 0x017, 0x016, 0x015, 0x014, 0x012, 0x011, 0x010, 0x00F, 0x00E, 0x00D},
  };

  static const int16_t TABLE_REAL[][96] PROGMEM = {
      // Table #3 of ProTracker 3.4r
      {0xCDA, 0xC22, 0xB73, 0xACF, 0xA33, 0x9A1, 0x917, 0x894, 0x819, 0x7A4, 0x737, 0x6CF, 0x66D, 0x611, 0x5BA, 0x567,
       0x51A, 0x4D0, 0x48B, 0x44A, 0x40C, 0x3D2, 0x39B, 0x367, 0x337, 0x308, 0x2DD, 0x2B4, 0x28D, 0x268, 0x246, 0x225,
       0x206, 0x1E9, 0x1CE, 0x1B4, 0x19B, 0x184, 0x16E, 0x15A, 0x146, 0x134, 0x123, 0x113, 0x103, 0x0F5, 0x0E7, 0x0DA,
       0x0CE, 0x0C2, 0x0B7, 0x0AD, 0x0A3, 0x09A, 0x091, 0x089, 0x082, 0x07A, 0x073, 0x06D, 0x067, 0x061, 0x05C, 0x056,
       0x052, 0x04D, 0x049, 0x045, 0x041, 0x03D, 0x03A, 0x036, 0x033, 0x031, 0x02E, 0x02B, 0x029, 0x027, 0x024, 0x022,
       0x020, 0x01F, 0x01D, 0x01B, 0x01A, 0x018, 0x017, 0x016, 0x014, 0x013, 0x012, 0x011, 0x010, 0x00F, 0x00E, 0x00D},
      // Table #3 of ProTracker 3.4x and above
      {0xCDA, 0xC22, 0xB73, 0xACF, 0xA33, 0x9A1, 0x917, 0x894, 0x819, 0x7A4, 0x737, 0x6CF, 0x66D, 0x611, 0x5BA, 0x567,
       0x51A, 0x4D0, 0x48B, 0x44A, 0x40C, 0x3D2, 0x39B, 0x367, 0x337, 0x308, 0x2DD, 0x2B4, 0x28D, 0x268, 0x246, 0x225,
       0x206, 0x1E9, 0x1CE, 0x1B4, 0x19B, 0x184, 0x16E, 0x15A, 0x146, 0x134, 0x123, 0x112, 0x103, 0x0F5, 0x0E7, 0x0DA,
       0x0CE, 0x0C2, 0x0B7, 0x0AD, 0x0A3, 0x09A, 0x091, 0x089, 0x082, 0x07A, 0x073, 0x06D, 0x067, 0x061, 0x05C, 0x056,
       0x052, 0x04D, 0x049, 0x045, 0x041, 0x03D, 0x03A, 0x036, 0x033, 0x031, 0x02E, 0x02B, 0x029, 0x027, 0x024, 0x022,
       0x020, 0x01F, 0x01D, 0x01B, 0x01A, 0x018, 0x017, 0x016, 0x014, 0x013, 0x012, 0x011, 0x010, 0x00F, 0x00E, 0x00D},
  };

  const int16_t *table = TABLE_ST;

  if (!mModule->HasNoteTable(1)) {
    if (mModule->HasNoteTable(2))
      table = TABLE_ASM[0];
    else if (mModule->HasNoteTable(3))
      table = TABLE_REAL[0];
    else
      table = TABLE_PT[0];
    if (mModule->GetSubVersion() >= 4)
      table += 96;
  }

  return pgm_read_word(table + tone);
}

uint8_t Player::mGetAmplitude(const uint8_t volume, const uint8_t amplitude) const {
  static const uint8_t TABLE_VOLUME[][16][16] PROGMEM = {
      // ProTracker v3.3x-v3.4x
      {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
       {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01},
       {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02},
       {0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03},
       {0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04},
       {0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x05, 0x05},
       {0x00, 0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06},
       {0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07},
       {0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07, 0x08},
       {0x00, 0x00, 0x01, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x08, 0x08, 0x09},
       {0x00, 0x00, 0x01, 0x02, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x08, 0x09, 0x0A},
       {0x00, 0x00, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x09, 0x09, 0x0A, 0x0B},
       {0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x07, 0x08, 0x08, 0x09, 0x0A, 0x0B, 0x0C},
       {0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D},
       {0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E},
       {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F}},
      // ProTracker v3.5x and above
      {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
       {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01},
       {0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02},
       {0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03},
       {0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04},
       {0x00, 0x00, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x05, 0x05},
       {0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06},
       {0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07},
       {0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07, 0x08},
       {0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x07, 0x07, 0x08, 0x08, 0x09},
       {0x00, 0x01, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x05, 0x06, 0x07, 0x07, 0x08, 0x09, 0x09, 0x0A},
       {0x00, 0x01, 0x01, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x07, 0x07, 0x08, 0x09, 0x0A, 0x0A, 0x0B},
       {0x00, 0x01, 0x02, 0x02, 0x03, 0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0A, 0x0B, 0x0C},
       {0x00, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0A, 0x0B, 0x0C, 0x0D},
       {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E},
       {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F}},
  };

  return pgm_read_byte(&TABLE_VOLUME[mModule->GetSubVersion() >= 5][volume][amplitude]);
}

void Player::mInit() {
  mApu.Reset();
  mPlayDelay.SetDelay(mModule->GetDelay(), 1);
  mPositionIt = mModule->GetPositionBegin();
  memset(&mChannels, 0, sizeof(mChannels));
  auto pattern = mModule->GetPattern(mPositionIt);
  for (uint8_t idx = 0; idx != mChannels.size(); ++idx) {
    Channel &c = mChannels[idx];
    c.SetVolume(15);
    c.SetPatternData(mModule->GetPatternData(pattern, idx));
    c.SetSample(mModule->GetSample(1));
    c.SetOrnament(mModule->GetOrnament(0));
    c.SetSkipLocations(1);
  }
}

void Player::mSetupEnvelope(Channel &channel) {
  channel.EnvelopeEnable();
  channel.SetOrnamentPosition(0);
  mEnvelopeBase = channel.PatternCodeBE16();
  mEnvelopeSlider.Reset();
}

inline void Player::mAdvancePosition() {
  if (*++mPositionIt == 0xFF)
    mPositionIt = mModule->GetPositionLoop();
  auto pattern = mModule->GetPattern(mPositionIt);
  for (uint8_t idx = 0; idx != mChannels.size(); ++idx)
    mChannels[idx].SetPatternData(mModule->GetPatternData(pattern, idx));
}

void Player::mPlayPattern(const blip_clk_time_t time) {
  for (Channel &c : mChannels) {
    if (c.IsEmptyLocation())
      continue;
    const uint8_t prevNote = c.GetNote();
    const int16_t prevSliding = c.GetToneSlide();
    while (true) {
      const uint8_t val = c.PatternCode();
      if (val >= 0xF0) {
        // Set ornament and sample. Envelope disable.
        c.SetOrnament(mModule->GetOrnament(val - 0xF0));
        c.SetSample(mModule->GetSample(c.PatternCode() / 2));
        c.EnvelopeDisable();
      } else if (val >= 0xD1) {
        // Set sample.
        c.SetSample(mModule->GetSample(val - 0xD0));
      } else if (val == 0xD0) {
        // Empty location. End position.
        break;
      } else if (val >= 0xC1) {
        // Set volume.
        c.SetVolume(val - 0xC0);
      } else if (val == 0xC0) {
        // Pause. End position.
        c.Disable();
        c.Reset();
        break;
      } else if (val >= 0xB2) {
        // Set envelope.
        mSetupEnvelope(c);
        mApu.Write(time, AyApu::AY_ENV_SHAPE, val - 0xB1);
      } else if (val == 0xB1) {
        // Set number of empty locations after the subsequent code.
        c.SetSkipLocations(c.PatternCode());
      } else if (val == 0xB0) {
        // Disable envelope.
        c.EnvelopeDisable();
        c.SetOrnamentPosition(0);
      } else if (val >= 0x50) {
        // Set note in semitones. End position.
        c.SetNote(val - 0x50);
        c.Reset();
        c.Enable();
        break;
      } else if (val >= 0x40) {
        // Set ornament.
        c.SetOrnament(mModule->GetOrnament(val - 0x40));
      } else if (val >= 0x20) {
        // Set noise offset (occurs only in channel B).
        mNoiseBase = val - 0x20;
      } else if (val >= 0x11) {
        // Set envelope and sample.
        mSetupEnvelope(c);
        mApu.Write(time, AyApu::AY_ENV_SHAPE, val - 0x10);
        c.SetSample(mModule->GetSample(c.PatternCode() / 2));
      } else if (val == 0x10) {
        // Disable envelope, reset ornament and set sample.
        c.EnvelopeDisable();
        c.SetOrnamentPosition(0);
        c.SetSample(mModule->GetSample(c.PatternCode() / 2));
      } else if (val != 0x00) {
        mCmdStack.push(val);
      } else {
        mAdvancePosition();
      }
    }

    for (; !mCmdStack.empty(); mCmdStack.pop()) {
      switch (mCmdStack.top()) {
        case 1:
          // Gliss Effect
          c.SetupGliss(this);
          break;
        case 2:
          // Portamento Effect
          c.SetupPortamento(this, prevNote, prevSliding);
          break;
        case 3:
          // Play Sample From Custom Position
          c.SetSamplePosition(c.PatternCode());
          break;
        case 4:
          // Play Ornament From Custom Position
          c.SetOrnamentPosition(c.PatternCode());
          break;
        case 5:
          // Vibrate Effect
          c.SetupVibrato();
          break;
        case 8:
          // Slide Envelope Effect
          mEnvelopeSlider.Enable(c.PatternCode());
          mEnvelopeSlider.SetStep(c.PatternCodeLE16());
          break;
        case 9:
          // Song Delay
          mPlayDelay.SetDelay(c.PatternCode());
          break;
      }
    }
  }
}

void Player::mPlaySamples(const blip_clk_time_t time) {
  int8_t envAdd = 0;
  uint8_t mixer = 0;
  for (uint8_t idx = 0; idx != mChannels.size(); ++idx, mixer >>= 1) {
    Channel &c = mChannels[idx];
    uint8_t amplitude = 0;

    if (c.IsEnabled()) {
      const SampleData &s = c.GetSampleData();
      amplitude = mGetAmplitude(c.GetVolume(), c.SlideAmplitude());

      if (c.IsEnvelopeEnabled() && !s.EnvelopeMask())
        amplitude |= 16;

      if (!s.NoiseMask()) {
        mApu.Write(time, AyApu::AY_NOISE_PERIOD, (mNoiseBase + c.SlideNoise()) % 32);
      } else {
        c.SlideEnvelope(envAdd);
      }

      mixer |= 64 * s.NoiseMask() | 8 * s.ToneMask();

      const uint16_t tone = c.PlayTone(this);
      mApu.Write(time, AyApu::AY_CHNL_A_FINE + idx * 2, tone % 256);
      mApu.Write(time, AyApu::AY_CHNL_A_COARSE + idx * 2, tone / 256);

      c.Advance();
    }

    c.RunVibrato();
    mApu.Write(time, AyApu::AY_CHNL_A_VOL + idx, amplitude);

    if (amplitude == 0)
      mixer |= 64 | 8;
  }

  const uint16_t envelope = mEnvelopeBase + mEnvelopeSlider.GetValue() + envAdd;
  mApu.Write(time, AyApu::AY_MIXER, mixer);
  mApu.Write(time, AyApu::AY_ENV_FINE, envelope % 256);
  mApu.Write(time, AyApu::AY_ENV_COARSE, envelope / 256);

  mEnvelopeSlider.Run();
}

/* PT3 CHANNEL */

void Channel::Reset() {
  SetSamplePosition(0);
  SetOrnamentPosition(0);
  mDisableVibrato();
  mToneSlide.Reset();
  mAmplitudeSlideStore = 0;
  mNoiseSlideStore = 0;
  mEnvelopeSlideStore = 0;
  mTranspositionAccumulator = 0;
}

void Channel::SetupGliss(const Player *player) {
  mPortamento = false;
  mDisableVibrato();
  uint8_t delay = PatternCode();
  if ((delay == 0) && (player->GetSubVersion() >= 7))
    delay++;
  mToneSlide.Enable(delay);
  mToneSlide.SetStep(PatternCodeLE16());
}

void Channel::SetupPortamento(const Player *player, const uint8_t prevNote, const int16_t prevSliding) {
  mPortamento = true;
  mDisableVibrato();
  mToneSlide.Enable(PatternCode());
  mSkipPatternCode(2);
  int16_t step = PatternCodeLE16();
  if (step < 0)
    step = -step;
  mNoteSlide = mNote;
  mNote = prevNote;
  mToneDelta = player->GetNotePeriod(mNoteSlide) - player->GetNotePeriod(mNote);
  if (player->GetSubVersion() >= 6)
    mToneSlide.SetValue(prevSliding);
  if (mToneDelta < mToneSlide.GetValue())
    step = -step;
  mToneSlide.SetStep(step);
}

inline uint8_t Channel::SlideNoise() {
  auto &sample = GetSampleData();
  const uint8_t value = sample.Noise() + mNoiseSlideStore;
  if (sample.NoiseEnvelopeStore())
    mNoiseSlideStore = value;
  return value;
}

inline void Channel::SlideEnvelope(int8_t &value) {
  auto &sample = GetSampleData();
  const int8_t tmp = sample.EnvelopeSlide() + mEnvelopeSlideStore;
  value += tmp;
  if (sample.NoiseEnvelopeStore())
    mEnvelopeSlideStore = tmp;
}

inline uint8_t Channel::SlideAmplitude() {
  auto &sample = GetSampleData();
  if (sample.VolumeSlide()) {
    if (sample.VolumeSlideUp()) {
      if (mAmplitudeSlideStore < 15)
        mAmplitudeSlideStore++;
    } else if (mAmplitudeSlideStore > -15) {
      mAmplitudeSlideStore--;
    }
  }
  return limit(sample.Volume() + mAmplitudeSlideStore, 0, 15);
}

inline void Channel::mRunPortamento() {
  if (((mToneSlide.GetStep() < 0) && (mToneSlide.GetValue() <= mToneDelta)) ||
      ((mToneSlide.GetStep() >= 0) && (mToneSlide.GetValue() >= mToneDelta))) {
    mToneSlide.Reset();
    mNote = mNoteSlide;
  }
}

uint16_t Channel::PlayTone(const Player *player) {
  auto &s = GetSampleData();
  int16_t tone = s.Transposition() + mTranspositionAccumulator;
  if (s.ToneStore())
    mTranspositionAccumulator = tone;
  const uint8_t note = limit(mNote + mOrnamentPlayer.GetData(), 0, 95);
  tone += player->GetNotePeriod(note) + mToneSlide.GetValue();
  if (mToneSlide.Run() && mPortamento)
    mRunPortamento();
  return tone & 0xFFF;
}

/* PT3 MODULE */

const PT3Module *PT3Module::GetModule(const uint8_t *data, const size_t size) {
  if (size <= sizeof(PT3Module))
    return nullptr;
  if (!memcmp_P(data, PT_SIGNATURE, sizeof(PT_SIGNATURE)) || !memcmp_P(data, VT_SIGNATURE, sizeof(VT_SIGNATURE)))
    return reinterpret_cast<const PT3Module *>(data);
  return nullptr;
}

const PT3Module *PT3Module::FindTSModule(const uint8_t *data, size_t size) {
  if (size <= sizeof(PT3Module) * 2)
    return nullptr;
  data += sizeof(PT3Module);
  size -= sizeof(PT3Module);
  const void *ptr = memmem_P(data, size, PT_SIGNATURE, sizeof(PT_SIGNATURE));
  if (ptr == nullptr)
    ptr = memmem_P(data, size, VT_SIGNATURE, sizeof(VT_SIGNATURE));
  return reinterpret_cast<const PT3Module *>(ptr);
}

inline uint8_t PT3Module::GetSubVersion() const {
  const uint8_t version = mSubVersion - '0';
  return (version < 10) ? version : 6;
}

unsigned PT3Module::CountSongLengthMs(unsigned &loop) const {
  const unsigned length = CountSongLength(loop) * 1000 / FRAME_RATE;
  loop = loop * 1000 / FRAME_RATE;
  return length;
}

unsigned PT3Module::LengthCounter::GetFrameLength(const PT3Module *module, unsigned &loopFrame) {
  unsigned frame = 0;

  auto it = module->GetPositionBegin();
  auto pattern = module->GetPattern(it);

  mPlayDelay = module->GetDelay();

  for (uint8_t idx = 0; idx != mChannels.size(); ++idx) {
    auto &c = mChannels[idx];

    c.data = module->GetPatternData(pattern, idx);
    c.delay.SetDelay(1);
  }

  for (; it != module->GetPositionEnd(); ++it) {
    if (it == module->GetPositionLoop())
      loopFrame = frame;

    for (uint8_t idx = 0; idx != mChannels.size(); ++idx)
      mChannels[idx].data = module->GetPatternData(module->GetPattern(it), idx);

    frame += mGetPositionLength();
  }

  return frame;
}

unsigned PT3Module::LengthCounter::mGetPositionLength() {
  for (unsigned frames = 0;; frames += mPlayDelay) {
    for (auto &c : mChannels) {
      if (!c.delay.Run())
        continue;
      while (true) {
        const PatternData val = *c.data++;
        if ((val >= 0x50 && val <= 0xAF) || val == 0xD0 || val == 0xC0) {
          break;
        } else if (val >= 0xF0 || val == 0x10) {
          c.data += 1;
        } else if (val >= 0xB2 && val <= 0xBF) {
          c.data += 2;
        } else if (val >= 0x11 && val <= 0x1F) {
          c.data += 3;
        } else if (val == 0xB1) {
          c.delay.SetDelay(*c.data++);
        } else if (val <= 0x09 && val >= 0x01) {
          mStack.push(val);
        } else if (val == 0x00) {
          return frames;
        }
      }
      for (; !mStack.empty(); mStack.pop()) {
        const PatternData val = mStack.top();
        if (val == 0x09)
          mPlayDelay = *c.data++;
        else if (val == 0x02)
          c.data += 5;
        else if (val == 0x05)
          c.data += 2;
        else if ((val == 0x01) || (val == 0x08))
          c.data += 3;
        else if ((val == 0x03) || (val == 0x04))
          c.data += 1;
      }
    }
  }
}

/* PT3 FILE */

struct Pt3File : GmeInfo {
  const PT3Module *mModule;
  bool mHasTS;
  Pt3File() { mSetType(gme_pt3_type); }
  static MusicEmu *createPt3File() { return new Pt3File; }

  blargg_err_t mLoad(const uint8_t *data, const long size) override {
    mModule = PT3Module::GetModule(data, size);
    if (mModule == nullptr)
      return gme_wrong_file_type;
    mHasTS = PT3Module::FindTSModule(data, size) != nullptr;
    mSetTrackNum(1);
    return nullptr;
  }

  blargg_err_t mGetTrackInfo(track_info_t *out, const int track) const override {
    GmeFile::copyField(out->song, mModule->GetName(), 32);
    GmeFile::copyField(out->author, mModule->GetAuthor(), 32);
    unsigned loop;
    out->length = mModule->CountSongLengthMs(loop);
    out->loop_length = loop;
    if (mHasTS)
      strcpy_P(out->comment, PSTR("6-ch TurboSound (TS)"));
    return nullptr;
  }
};

/* PT3 EMULATOR */

Pt3Emu::Pt3Emu() : mTurboSound(nullptr) {
  static const char *const CHANNELS_NAMES[] = {
      "Wave 1", "Wave 2", "Wave 3", "Wave 4", "Wave 5", "Wave 6",
  };
  static int const CHANNELS_TYPES[] = {
      WAVE_TYPE | 0, WAVE_TYPE | 1, WAVE_TYPE | 2, WAVE_TYPE | 3, WAVE_TYPE | 4, WAVE_TYPE | 5,
  };
  mSetType(gme_pt3_type);
  mSetChannelsNames(CHANNELS_NAMES);
  mSetChannelsTypes(CHANNELS_TYPES);
  mSetSilenceLookahead(1);
}

Pt3Emu::~Pt3Emu() { mDestroyTS(); }

blargg_err_t Pt3Emu::mGetTrackInfo(track_info_t *out, const int track) const {
  GmeFile::copyField(out->song, mPlayer.GetName(), 32);
  GmeFile::copyField(out->author, mPlayer.GetAuthor(), 32);
  unsigned loop;
  out->length = mPlayer.CountSongLengthMs(loop);
  out->loop_length = loop;
  if (mHasTS())
    strcpy_P(out->comment, PSTR("6-ch TurboSound (TS)"));
  return nullptr;
}

bool Pt3Emu::mCreateTS() {
  if (!mHasTS())
    mTurboSound = new Player;
  return mHasTS();
}

void Pt3Emu::mDestroyTS() {
  if (!mHasTS())
    return;
  delete mTurboSound;
  mTurboSound = nullptr;
}

blargg_err_t Pt3Emu::mLoad(const uint8_t *data, const long size) {
  auto module = PT3Module::GetModule(data, size);
  if (module == nullptr)
    return gme_wrong_file_type;
  mSetTrackNum(1);
  mPlayer.Load(module);
  module = PT3Module::FindTSModule(data, size);
  if (module == nullptr) {
    mDestroyTS();
  } else if (mCreateTS()) {
    mTurboSound->Load(module);
    mPlayer.SetVolume(mGetGain() * 0.7);
    mTurboSound->SetVolume(mGetGain() * 0.7);
    mSetChannelsNumber(AyApu::OSCS_NUM * 2);
    return mSetupBuffer(CLOCK_RATE);
  }
  mPlayer.SetVolume(mGetGain());
  mSetChannelsNumber(AyApu::OSCS_NUM);
  return mSetupBuffer(CLOCK_RATE);
}

void Pt3Emu::mUpdateEq(const BlipEq &eq) {}  // mApu.SetTrebleEq(eq); }

void Pt3Emu::mSetChannel(const int idx, BlipBuffer *center, BlipBuffer *, BlipBuffer *) {
  if (idx < AyApu::OSCS_NUM)
    mPlayer.SetOscOutput(idx, center);
  else if (mHasTS())
    mTurboSound->SetOscOutput(idx - AyApu::OSCS_NUM, center);
}

void Pt3Emu::mSetTempo(double temp) {
  mFramePeriod = static_cast<blip_clk_time_t>(mGetClockRate() / FRAME_RATE / temp);
}

blargg_err_t Pt3Emu::mStartTrack(const int track) {
  RETURN_ERR(ClassicEmu::mStartTrack(track));
  mEmuTime = 0;
  mPlayer.Init();
  if (mHasTS())
    mTurboSound->Init();
  SetTempo(mGetTempo());
  return nullptr;
}

blargg_err_t Pt3Emu::mRunClocks(blip_clk_time_t &duration) {
  for (; mEmuTime <= duration; mEmuTime += mFramePeriod) {
    mPlayer.RunUntil(mEmuTime);
    if (mHasTS())
      mTurboSound->RunUntil(mEmuTime);
  }
  mEmuTime -= duration;
  mPlayer.EndFrame(duration);
  if (mHasTS())
    mTurboSound->EndFrame(duration);
  return nullptr;
}

}  // namespace pt3
}  // namespace ay
}  // namespace emu
}  // namespace gme

static const gme_type_t_ gme_pt3_type_ = {
    "ZX Spectrum (PT 3.x)",
    1,
    0,
    &gme::emu::ay::pt3::Pt3Emu::createPt3Emu,
    &gme::emu::ay::pt3::Pt3File::createPt3File,
    "PT3",
    1,
};
extern gme_type_t const gme_pt3_type = &gme_pt3_type_;
