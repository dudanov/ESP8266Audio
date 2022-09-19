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

/* PT3 PLAYER */

void Player::mUpdateTables() {
  static const int8_t TABLE_VOLUME[][16][16] PROGMEM = {
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

  static const uint16_t TABLE_PT[][96] PROGMEM = {
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

  static const uint16_t TABLE_ST[] PROGMEM = {
      // Table #1 of ProTracker 3.3x and above
      0xEF8, 0xE10, 0xD60, 0xC80, 0xBD8, 0xB28, 0xA88, 0x9F0, 0x960, 0x8E0, 0x858, 0x7E0, 0x77C, 0x708, 0x6B0, 0x640,
      0x5EC, 0x594, 0x544, 0x4F8, 0x4B0, 0x470, 0x42C, 0x3FD, 0x3BE, 0x384, 0x358, 0x320, 0x2F6, 0x2CA, 0x2A2, 0x27C,
      0x258, 0x238, 0x216, 0x1F8, 0x1DF, 0x1C2, 0x1AC, 0x190, 0x17B, 0x165, 0x151, 0x13E, 0x12C, 0x11C, 0x10A, 0x0FC,
      0x0EF, 0x0E1, 0x0D6, 0x0C8, 0x0BD, 0x0B2, 0x0A8, 0x09F, 0x096, 0x08E, 0x085, 0x07E, 0x077, 0x070, 0x06B, 0x064,
      0x05E, 0x059, 0x054, 0x04F, 0x04B, 0x047, 0x042, 0x03F, 0x03B, 0x038, 0x035, 0x032, 0x02F, 0x02C, 0x02A, 0x027,
      0x025, 0x023, 0x021, 0x01F, 0x01D, 0x01C, 0x01A, 0x019, 0x017, 0x016, 0x015, 0x013, 0x012, 0x011, 0x010, 0x00F,
  };

  static const uint16_t TABLE_ASM[][96] PROGMEM = {
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

  static const uint16_t TABLE_REAL[][96] PROGMEM = {
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

  const uint8_t version = mModule->GetSubVersion();
  mVolumeTable = TABLE_VOLUME[version >= 5][0];

  if (mModule->HasNoteTable(0)) {
    mNoteTable = TABLE_PT[0];
  } else if (mModule->HasNoteTable(1)) {
    mNoteTable = TABLE_ST;
    return;
  } else if (mModule->HasNoteTable(2)) {
    mNoteTable = TABLE_ASM[0];
  } else {
    mNoteTable = TABLE_REAL[0];
  }

  if (version >= 4)
    mNoteTable += 96;
}

inline uint16_t Player::mGetNotePeriod(int8_t tone) const {
  return pgm_read_word(mNoteTable + ((tone >= 95) ? 95 : ((tone <= 0) ? 0 : tone)));
}

inline void Player::mUpdateAmplitude(int8_t &amplitude, uint8_t volume) const {
  amplitude = pgm_read_byte(mVolumeTable + 16 * volume + amplitude);
}

/* PT3 MODULE */

static const char PT_SIGNATURE[] PROGMEM = {
    'P', 'r', 'o', 'T', 'r', 'a', 'c', 'k', 'e', 'r', ' ', '3', '.',
};

static const char VT_SIGNATURE[] PROGMEM = {
    'V', 'o', 'r', 't', 'e', 'x', ' ', 'T', 'r', 'a', 'c', 'k', 'e', 'r', ' ', 'I', 'I',
};

const PT3Module *PT3Module::GetModule(const uint8_t *data, size_t size) {
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
  // Vortex Tracker?
  if (mSubVersion == 'r')
    return 6;
  return mSubVersion - '0';
}

unsigned PT3Module::CountSongLengthMs() const { return CountSongLength() * 1000 / FRAME_RATE; }

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
  SetTempo(mGetTempo());
  return nullptr;
}

void Player::mInit() {
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

void Player::mSetEnvelope(Channel &channel, uint8_t shape) {
  mApu.Write(mEmuTime, AyApu::AY_ENV_SHAPE, shape);
  channel.EnvelopeEnable();
  channel.ResetOrnament();
  mEnvelopeBase = channel.PatternCodeBE16();
  mCurEnvSlide = 0;
  mCurEnvDelay = 0;
}

void Player::mGliss(Channel &chan) {
  chan.SimpleGliss = true;
  chan.CurrentOnOff = 0;
  chan.TonSlideCount = chan.TonSlideDelay = chan.PatternCode();
  chan.TonSlideStep = chan.PatternCodeLE16();
  if ((chan.TonSlideCount == 0) && (mModule->GetSubVersion() >= 7))
    chan.TonSlideCount++;
}

void Player::mPortamento(Channel &chan, uint8_t prevNote, int16_t prevSliding) {
  chan.SimpleGliss = false;
  chan.CurrentOnOff = 0;
  chan.TonSlideCount = chan.TonSlideDelay = chan.PatternCode();
  chan.SkipPatternCode(2);
  int16_t step = chan.PatternCodeLE16();
  if (step < 0)
    step = -step;
  chan.TonSlideStep = step;
  chan.TonDelta = mGetNotePeriod(chan.Note) - mGetNotePeriod(prevNote);
  chan.SlideToNote = chan.Note;
  chan.Note = prevNote;
  if (mModule->GetSubVersion() >= 6)
    chan.CurrentTonSliding = prevSliding;
  if ((chan.TonDelta - chan.CurrentTonSliding) < 0)
    chan.TonSlideStep = -chan.TonSlideStep;
}

void Player::mPlayPattern() {
  for (Channel &chan : mChannels) {
    uint8_t counter = 0, cmd1 = 0, cmd2 = 0, cmd3 = 0, cmd4 = 0, cmd5 = 0, cmd8 = 0, cmd9 = 0;
    uint8_t prevNote = chan.Note;
    int16_t prevSliding = chan.CurrentTonSliding;
    while (true) {
      const uint8_t val = chan.PatternCode();
      if (val >= 0xF0) {
        // Set ornament and sample. Envelope disable.
        chan.SetOrnament(mModule->GetOrnament(val - 0xF0));
        chan.SetSample(mModule->GetSample(chan.PatternCode() / 2));
        chan.EnvelopeDisable();
      } else if (val >= 0xD1) {
        // Set sample.
        chan.SetSample(mModule->GetSample(val - 0xD0));
      } else if (val == 0xD0) {
        // Empty location. End position.
        break;
      } else if (val >= 0xC1) {
        // Set volume.
        chan.Volume = val - 0xC0;
      } else if (val == 0xC0) {
        // Pause. End position.
        chan.Disable();
        chan.Reset();
        break;
      } else if (val >= 0xB2) {
        // Set envelope.
        mSetEnvelope(chan, val - 0xB1);
      } else if (val == 0xB1) {
        // Set number of empty locations after the subsequent code.
        chan.SetSkipNotes(chan.PatternCode());
      } else if (val == 0xB0) {
        // Disable envelope.
        chan.EnvelopeDisable();
        chan.ResetOrnament();
      } else if (val >= 0x50) {
        // Set note in semitones. End position.
        chan.SetNote(val - 0x50);
        chan.Reset();
        chan.Enable();
        break;
      } else if (val >= 0x40) {
        // Set ornament.
        chan.SetOrnament(mModule->GetOrnament(val - 0x40));
      } else if (val >= 0x20) {
        // Set noise offset (occurs only in channel B).
        mNoiseBase = val - 0x20;
      } else if (val >= 0x11) {
        // Set envelope and sample.
        mSetEnvelope(chan, val - 0x10);
        chan.SetSample(mModule->GetSample(chan.PatternCode() / 2));
      } else if (val == 0x10) {
        // Disable envelope, reset ornament and set sample.
        chan.EnvelopeDisable();
        chan.ResetOrnament();
        chan.SetSample(mModule->GetSample(chan.PatternCode() / 2));
      } else if (val == 0x09) {
        cmd9 = ++counter;
      } else if (val == 0x08) {
        cmd8 = ++counter;
      } else if (val == 0x05) {
        cmd5 = ++counter;
      } else if (val == 0x04) {
        cmd4 = ++counter;
      } else if (val == 0x03) {
        cmd3 = ++counter;
      } else if (val == 0x02) {
        cmd2 = ++counter;
      } else if (val == 0x01) {
        cmd1 = ++counter;
      } else if (val == 0x00) {
        mAdvancePosition();
      }
    }

    for (; counter > 0; --counter) {
      if (counter == cmd1) {
        mGliss(chan);
      } else if (counter == cmd2) {
        mPortamento(chan, prevNote, prevSliding);
      } else if (counter == cmd3) {
        chan.ResetSample(chan.PatternCode());
      } else if (counter == cmd4) {
        chan.ResetOrnament(chan.PatternCode());
      } else if (counter == cmd5) {
        chan.CurrentOnOff = chan.OnOffDelay = chan.PatternCode();
        chan.OffOnDelay = chan.PatternCode();
        chan.CurrentTonSliding = chan.TonSlideCount = 0;
      } else if (counter == cmd8) {
        mCurEnvDelay = mEnvDelay = chan.PatternCode();
        mEnvSlideAdd = chan.PatternCodeLE16();
      } else if (counter == cmd9) {
        mDelay = chan.PatternCode();
      }
    }
  }
}

inline bool SkipCounter::RunTick() {
  if (mSkipCounter > 0) {
    mSkipCounter--;
    return true;
  }
  mSkipCounter = mSkipCount;
  return false;
}

inline void Player::mAdvancePosition() {
  if (*++mPositionIt == 0xFF)
    mPositionIt = mModule->GetPositionLoop();
  auto pattern = mModule->GetPattern(mPositionIt);
  for (uint8_t idx = 0; idx != mChannels.size(); ++idx)
    mChannels[idx].SetPatternData(mModule->GetPatternData(pattern, idx));
}

void SampleData::NoiseSlide(uint8_t &value, uint8_t &store) const {
  uint8_t tmp = mData[0] / 2 % 32;
  value = tmp + store;
  if (mData[1] & 32)
    store = value;
}

void SampleData::EnvelopeSlide(int8_t &value, int8_t &store) const {
  int8_t tmp = mData[0] >> 1;
  tmp = (tmp & 16) ? (tmp | ~15) : (tmp & 15);
  tmp += store;
  value += tmp;
  if (mData[1] & 32)
    store = tmp;
}

void SampleData::VolumeSlide(int8_t &value, int8_t &store) const {
  if (mVolumeSlide()) {
    if (mVolumeSlideUp()) {
      if (store < 15)
        store++;
    } else if (store > -15) {
      store--;
    }
  }
  value = mVolume() + store;
  if (value > 15)
    value = 15;
  else if (value < 0)
    value = 0;
}

void Player::mPlaySamples() {
  int8_t AddToEnv = 0;
  uint8_t mixer = 0;
  for (uint8_t idx = 0; idx != mChannels.size(); ++idx, mixer >>= 1) {
    Channel &chan = mChannels[idx];
    // unsigned char j, b1, b0;
    if (chan.IsEnabled()) {
      const SampleData &sample = chan.GetSampleData();
      chan.Ton = sample.Transposition() + chan.TonAccumulator;
      if (sample.ToneStore())
        chan.TonAccumulator = chan.Ton;
      chan.Ton = (chan.Ton + mGetNotePeriod(chan.GetOrnamentNote()) + chan.CurrentTonSliding) % 4096;
      if (chan.TonSlideCount > 0) {
        chan.TonSlideCount--;
        if (chan.TonSlideCount == 0) {
          chan.CurrentTonSliding += chan.TonSlideStep;
          chan.TonSlideCount = chan.TonSlideDelay;
          if (!chan.SimpleGliss) {
            if (((chan.TonSlideStep < 0) && (chan.CurrentTonSliding <= chan.TonDelta)) ||
                ((chan.TonSlideStep >= 0) && (chan.CurrentTonSliding >= chan.TonDelta))) {
              chan.Note = chan.SlideToNote;
              chan.TonSlideCount = 0;
              chan.CurrentTonSliding = 0;
            }
          }
        }
      }
      sample.VolumeSlide(chan.Amplitude, chan.CurrentAmplitudeSliding);
      mUpdateAmplitude(chan.Amplitude, chan.Volume);
      if (chan.IsEnvelopeEnabled() && !sample.EnvelopeMask())
        chan.Amplitude |= 16;
      if (sample.NoiseMask())
        sample.EnvelopeSlide(AddToEnv, chan.CurrentEnvelopeSliding);
      else
        sample.NoiseSlide(mAddToNoise, chan.CurrentNoiseSliding);
      mixer |= 64 * sample.NoiseMask() | 8 * sample.ToneMask();
    } else {
      chan.Amplitude = 0;
    }
    if (chan.CurrentOnOff > 0) {
      chan.CurrentOnOff--;
      if (chan.CurrentOnOff == 0) {
        chan.mEnabled = !chan.mEnabled;
        if (chan.mEnabled)
          chan.CurrentOnOff = chan.OnOffDelay;
        else
          chan.CurrentOnOff = chan.OffOnDelay;
      }
    }
  }
  mApu.Write(mEmuTime, AyApu::AY_MIXER, mixer);
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
