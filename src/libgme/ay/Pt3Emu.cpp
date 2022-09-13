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

/* PT3 MODULE */

static const uint16_t NOTE_PT[2][96] PROGMEM = {
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

// Table #1 of ProTracker 3.3x and above
static const uint16_t NOTE_ST[96] PROGMEM = {
    0xEF8, 0xE10, 0xD60, 0xC80, 0xBD8, 0xB28, 0xA88, 0x9F0, 0x960, 0x8E0, 0x858, 0x7E0, 0x77C, 0x708, 0x6B0, 0x640,
    0x5EC, 0x594, 0x544, 0x4F8, 0x4B0, 0x470, 0x42C, 0x3FD, 0x3BE, 0x384, 0x358, 0x320, 0x2F6, 0x2CA, 0x2A2, 0x27C,
    0x258, 0x238, 0x216, 0x1F8, 0x1DF, 0x1C2, 0x1AC, 0x190, 0x17B, 0x165, 0x151, 0x13E, 0x12C, 0x11C, 0x10A, 0x0FC,
    0x0EF, 0x0E1, 0x0D6, 0x0C8, 0x0BD, 0x0B2, 0x0A8, 0x09F, 0x096, 0x08E, 0x085, 0x07E, 0x077, 0x070, 0x06B, 0x064,
    0x05E, 0x059, 0x054, 0x04F, 0x04B, 0x047, 0x042, 0x03F, 0x03B, 0x038, 0x035, 0x032, 0x02F, 0x02C, 0x02A, 0x027,
    0x025, 0x023, 0x021, 0x01F, 0x01D, 0x01C, 0x01A, 0x019, 0x017, 0x016, 0x015, 0x013, 0x012, 0x011, 0x010, 0x00F,
};

static const uint16_t NOTE_ASM[2][96] PROGMEM = {
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

static const uint16_t NOTE_REAL[2][96] PROGMEM = {
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

// Volume table
static const uint8_t VOLUME_TABLE[2][16][16] PROGMEM = {
    // Pro Tracker v3.3x-v3.4x
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

    // Pro Tracker v3.5x and above
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

static const char PT_SIGNATURE[] PROGMEM = {
    'P', 'r', 'o', 'T', 'r', 'a', 'c', 'k', 'e', 'r', ' ', '3', '.',
};
static const char VT_SIGNATURE[] PROGMEM = {
    'V', 'o', 'r', 't', 'e', 'x', ' ', 'T', 'r', 'a', 'c', 'k', 'e', 'r', ' ', 'I', 'I', ' ',
};

int8_t PT3Module::mGetSubVersion() const {
  if (!memcmp_P(mIdentify, PT_SIGNATURE, sizeof(PT_SIGNATURE)))
    return mSubVersion - '0';
  if (!memcmp_P(mIdentify, VT_SIGNATURE, sizeof(VT_SIGNATURE)))
    return 6;
  return -1;
}

inline uint16_t PT3Module::GetTonePeriod(uint8_t tone) {
  return pgm_read_word(NOTE_TABLE + ((tone <= 95) ? tone : 95));
}

inline const Position *PT3Module::GetPositionBegin() const { return mGetPointer<PositionsTable>(mPositions)->position; }

inline const Position *PT3Module::GetPositionEnd() const {
  auto p = mGetPointer<PositionsTable>(mPositions);
  return p->position + p->count + 1;
}

inline size_t PT3Module::mGetPositionsCount() const { return mGetPointer<PositionsTable>(mPositions)->count + 1; }

inline const Ornament *PT3Module::GetOrnament(uint8_t number) const {
  auto it = mGetPointer<Ornament>(mOrnaments);
  while (!it->HasNumber(number))
    ++it;
  return it;
}

inline const Sample *PT3Module::GetSample(uint8_t number) const {
  auto it = mSamples;
  while (!it->HasNumber(number))
    ++it;
  return it;
}

uint8_t PT3Module::mCountPatternLength(const PatternIndex *pattern, uint8_t channel) const {
  unsigned length = 0, skip = 0;
  for (auto it = GetPattern(pattern, channel); *it != 0xFF; ++it) {
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

bool PT3Module::mCheckSongData() const {
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

bool PT3Module::CheckIntegrity(size_t size) const {
  // Header size
  if (size <= sizeof(PT3Module))
    return false;

  // Checking samples section
  constexpr uint16_t SamplesBlockOffset = sizeof(PT3Module);
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

unsigned PT3Module::CountSongLength() const {
  // all patterns has same length
  return mCountPatternLength(GetPatternIndex(GetPositionBegin()->pattern)) * mGetPositionsCount() * mDelay;
}

unsigned PT3Module::CountSongLengthMs() const { return CountSongLength() * 1000 / FRAME_RATE; }

/* CHANNEL */

inline void Channel::AdvanceSample() {
  if (--mSampleCounter) {
    mSamplePosition++;
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

/* PT3 EMULATOR */

Pt3Emu::Pt3Emu() {
  static const char *const CHANNELS_NAMES[] = {"Wave 1", "Wave 2", "Wave 3"};
  static int const CHANNELS_TYPES[] = {WAVE_TYPE | 0, WAVE_TYPE | 1, WAVE_TYPE | 2};
  mSetType(gme_pt3_type);
  mSetChannelsNames(CHANNELS_NAMES);
  mSetChannelsTypes(CHANNELS_TYPES);
  mSetSilenceLookahead(1);
}

Pt3Emu::~Pt3Emu() {}

blargg_err_t Pt3Emu::mGetTrackInfo(track_info_t *out, int track) const {
  out->length = mModule->CountSongLengthMs();
  return nullptr;
}

struct Pt3File : GmeInfo {
  const PT3Module *mModule;
  Pt3File() { mSetType(gme_pt3_type); }
  static MusicEmu *createPt3File() { return new Pt3File; }

  blargg_err_t mLoad(const uint8_t *data, long size) override {
    mModule = reinterpret_cast<const PT3Module *>(data);
    if (!mModule->CheckIntegrity(size))
      return gme_wrong_file_type;
    mSetTrackNum(1);
    return nullptr;
  }

  blargg_err_t mGetTrackInfo(track_info_t *out, int track) const override {
    out->length = mModule->CountSongLengthMs();
    return nullptr;
  }
};

// Setup

blargg_err_t Pt3Emu::mLoad(const uint8_t *data, long size) {
  mModule = reinterpret_cast<const PT3Module *>(data);
  if (!mModule->CheckIntegrity(size))
    return gme_wrong_file_type;
  mSetTrackNum(1);
  mSetChannelsNumber(AyApu::OSCS_NUM);
  mApu.SetVolume(mGetGain());
  return mSetupBuffer(CLOCK_RATE);
}

void Pt3Emu::mUpdateEq(BlipEq const &eq) { mApu.SetTrebleEq(eq); }

void Pt3Emu::mSetChannel(int i, BlipBuffer *center, BlipBuffer *, BlipBuffer *) { mApu.SetOscOutput(i, center); }

// Emulation

void Pt3Emu::mSetTempo(double temp) {
  mFramePeriod = static_cast<blip_clk_time_t>(mGetClockRate() / FRAME_RATE / temp);
}

blargg_err_t Pt3Emu::mStartTrack(int track) {
  RETURN_ERR(ClassicEmu::mStartTrack(track));
  mInit();
  SetTempo(mGetTempo());
  return nullptr;
}

void Pt3Emu::mInit() {
  mApu.Reset();
  mEmuTime = 0;
  mDelayCounter = 1;
  mPositionIt = mModule->GetPositionBegin();
  memset(&mChannels, 0, sizeof(mChannels));
  auto pattern = mModule->GetPatternIndex(mPositionIt->pattern);
  for (uint8_t idx = 0; idx != mChannels.size(); ++idx) {
    Channel &c = mChannels[idx];
    c.SetPatternData(mModule->GetPattern(pattern, idx));
    c.SetOrnament(mModule, 0);
  }
}

void Pt3Emu::mPlayPattern() {
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
        channel.SetSample(mModule, code % 16);
      } else if (code <= 0x7F) {
        // Select ornament (0-15).
        channel.EnvelopeDisable();
        channel.SetOrnament(mModule, code % 16);
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
        channel.SetOrnament(mModule, 0);
      } else if (code <= 0x8E) {
        // Select envelope effect (3-14).
        channel.EnvelopeEnable();
        channel.SetOrnament(mModule, 0);
        mApu.Write(mEmuTime, AyApu::R11, channel.PatternCode());
        mApu.Write(mEmuTime, AyApu::R12, 0);
        mApu.Write(mEmuTime, AyApu::R13, code % 16);
      } else if (code == 0x00) {
        // End pattern marker. Advance to next song position and update all channels.
        mAdvancePosition();
      } else {
        // Number of empty locations after the subsequent code (0-63).
        channel.SetSkipCount(code - 0xA1);
      }
    }
  }
}

inline void Pt3Emu::mAdvancePosition() {
  if (*++mPositionIt == 0xFF)
    mPositionIt = mModule->GetPositionLoop();
  auto pattern = mModule->GetPattern(mPositionIt);
  for (uint8_t idx = 0; idx != mChannels.size(); ++idx)
    mChannels[idx].SetPatternData(mModule->GetPatternData(pattern, idx));
}

void Pt3Emu::mPlaySamples() {
  uint8_t mixer = 0;
  for (uint8_t idx = 0; idx != mChannels.size(); ++idx, mixer >>= 1) {
    Channel &channel = mChannels[idx];

    if (!channel.IsEnabled()) {
      mixer |= 64 | 8;
      continue;
    }

    auto sample = channel.GetSampleData();

    if (!sample->NoiseMask())
      mApu.Write(mEmuTime, AyApu::R6, sample->Noise());

    mixer |= 64 * sample->NoiseMask() | 8 * sample->ToneMask();

    const uint8_t note = channel.GetOrnamentNote() + mPositionTransposition();
    const uint16_t period = (PT3Module::GetTonePeriod(note) + sample->Transposition()) % 4096;

    mApu.Write(mEmuTime, AyApu::R0 + idx * 2, period % 256);
    mApu.Write(mEmuTime, AyApu::R1 + idx * 2, period / 256);
    mApu.Write(mEmuTime, AyApu::R8 + idx, sample->Volume() + 16 * channel.IsEnvelopeEnabled());

    channel.AdvanceSample();
  }
  mApu.Write(mEmuTime, AyApu::R7, mixer);
}

blargg_err_t Pt3Emu::mRunClocks(blip_clk_time_t &duration) {
  for (; mEmuTime <= duration; mEmuTime += mFramePeriod) {
    if (--mDelayCounter == 0) {
      mDelayCounter = mModule->GetDelay();
      mPlayPattern();
    }
    mPlaySamples();
  }
  mEmuTime -= duration;
  mApu.EndFrame(duration);
  return nullptr;
}

}  // namespace pt3
}  // namespace ay
}  // namespace emu
}  // namespace gme

static const gme_type_t_ gme_pt3_type_ = {
    "ZX Spectrum", 1, 0, &gme::emu::ay::pt3::Pt3Emu::createPt3Emu, &gme::emu::ay::pt3::Pt3File::createPt3File, "PT3", 1,
};
extern gme_type_t const gme_pt3_type = &gme_pt3_type_;
