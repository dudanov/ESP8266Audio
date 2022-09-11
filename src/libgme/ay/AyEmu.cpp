// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "AyEmu.h"
#include "../blargg_endian.h"
#include "../blargg_source.h"

#include <cstring>
#include <pgmspace.h>

/* Copyright (C) 2006 Shay Green. This module is free software; you
can redistribute it and/or modify it under the terms of the GNU Lesser
General Public License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version. This
module is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
details. You should have received a copy of the GNU Lesser General Public
License along with this module; if not, write to the Free Software Foundation,
Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA */

namespace gme {
namespace emu {
namespace ay {

static const uint16_t RAM_START = 0x4000;
static const uint8_t OSCS_NUM = AyApu::OSCS_NUM + 1;

AyEmu::AyEmu() {
  static const char *const CHANNELS_NAMES[OSCS_NUM] = {"Wave 1", "Wave 2", "Wave 3", "Beeper"};
  static int const CHANNELS_TYPES[OSCS_NUM] = {WAVE_TYPE | 0, WAVE_TYPE | 1, WAVE_TYPE | 2, MIXED_TYPE | 0};

  mBeeperOutput = 0;
  mSetType(gme_ay_type);

  mSetChannelsNames(CHANNELS_NAMES);

  mSetChannelsTypes(CHANNELS_TYPES);
  mSetSilenceLookahead(6);
}

AyEmu::~AyEmu() {}

// Track info

static const uint8_t *get_data(const AyEmu::file_t &file, const uint8_t *ptr, int min_size) {
  long pos = ptr - (const uint8_t *) file.header;
  long file_size = file.end - (uint8_t const *) file.header;
  assert((unsigned long) pos <= (unsigned long) file_size - 2);
  int offset = (int16_t) get_be16(ptr);
  if (!offset || blargg_ulong(pos + offset) > blargg_ulong(file_size - min_size))
    return 0;
  return ptr + offset;
}

static blargg_err_t parse_header(const uint8_t *in, long size, AyEmu::file_t *out) {
  typedef AyEmu::header_t header_t;
  out->header = (header_t const *) in;
  out->end = in + size;

  if (size < AyEmu::HEADER_SIZE)
    return gme_wrong_file_type;

  const header_t &h = *(const header_t *) in;
  if (memcmp_P(h.tag, PSTR("ZXAYEMUL"), 8))
    return gme_wrong_file_type;

  out->tracks = get_data(*out, h.track_info, (h.max_track + 1) * 4);
  if (!out->tracks)
    return "Missing track data";

  return 0;
}

static void copy_ay_fields(AyEmu::file_t const &file, track_info_t *out, int track) {
  GmeFile::copyField(out->song, (char const *) get_data(file, file.tracks + track * 4, 1));
  uint8_t const *track_info = get_data(file, file.tracks + track * 4 + 2, 6);
  if (track_info)
    out->length = get_be16(track_info + 4) * (1000L / 50);  // frames to msec

  GmeFile::copyField(out->author, (char const *) get_data(file, file.header->author, 1));
  GmeFile::copyField(out->comment, (char const *) get_data(file, file.header->comment, 1));
}

blargg_err_t AyEmu::mGetTrackInfo(track_info_t *out, int track) const {
  copy_ay_fields(mFile, out, track);
  return 0;
}

struct AyFile : GmeInfo {
  AyEmu::file_t file;

  AyFile() { mSetType(gme_ay_type); }
  static MusicEmu *createAyFile() { return BLARGG_NEW AyFile; }

  blargg_err_t mLoad(uint8_t const *begin, long size) override {
    RETURN_ERR(parse_header(begin, size, &file));
    mSetTrackNum(file.header->max_track + 1);
    return 0;
  }

  blargg_err_t mGetTrackInfo(track_info_t *out, int track) const override {
    copy_ay_fields(file, out, track);
    return 0;
  }
};

// Setup

blargg_err_t AyEmu::mLoad(const uint8_t *in, long size) {
  assert(offsetof(header_t, track_info[2]) == HEADER_SIZE);

  RETURN_ERR(parse_header(in, size, &mFile));
  mSetTrackNum(mFile.header->max_track + 1);

  if (mFile.header->vers > 2)
    mSetWarning("Unknown file version");

  mSetChannelsNumber(OSCS_NUM);
  mApu.SetVolume(mGetGain());

  return mSetupBuffer(CLK_SPECTRUM);
}

void AyEmu::mUpdateEq(BlipEq const &eq) { mApu.SetTrebleEq(eq); }

void AyEmu::mSetChannel(int i, BlipBuffer *center, BlipBuffer *, BlipBuffer *) {
  if (i >= AyApu::OSCS_NUM)
    mBeeperOutput = center;
  else
    mApu.SetOscOutput(i, center);
}

// Emulation

void AyEmu::mSetTempo(double t) { mPlayPeriod = blip_time_t(mGetClockRate() / 50 / t); }

blargg_err_t AyEmu::mStartTrack(int track) {
  RETURN_ERR(ClassicEmu::mStartTrack(track));

  std::fill(&mMem.ram[0x00000], &mMem.ram[0x00100], 0xC9);  // fill RST vectors with RET
  std::fill(&mMem.ram[0x00100], &mMem.ram[0x04000], 0xFF);
  std::fill(&mMem.ram[0x04000], &mMem.ram[0x10000], 0x00);  // RAM area
  std::fill(&mMem.ram[0x10000], mMem.ram.end(), 0xFF);
  mMem.padding1.fill(0xFF);

  // locate data blocks
  uint8_t const *const data = get_data(mFile, mFile.tracks + track * 4 + 2, 14);
  if (!data)
    return "File data missing";

  uint8_t const *const more_data = get_data(mFile, data + 10, 6);
  if (!more_data)
    return "File data missing";

  uint8_t const *blocks = get_data(mFile, data + 12, 8);
  if (!blocks)
    return "File data missing";

  // initial addresses
  cpu::Reset(mMem.ram.data());
  r.sp = get_be16(more_data);
  r.b.a = r.b.b = r.b.d = r.b.h = data[8];
  r.b.flags = r.b.c = r.b.e = r.b.l = data[9];
  r.alt.w = r.w;
  r.ix = r.iy = r.w.hl;

  unsigned addr = get_be16(blocks);
  if (!addr)
    return "File data missing";

  uint16_t init = get_be16(more_data + 2);
  if (!init)
    init = addr;

  // copy blocks into memory
  do {
    blocks += 2;
    unsigned len = get_be16(blocks);
    blocks += 2;
    if (addr + len > 0x10000) {
      mSetWarning("Bad data block size");
      len = 0x10000 - addr;
    }
    check(len);
    uint8_t const *in = get_data(mFile, blocks, 0);
    blocks += 2;
    if (len > blargg_ulong(mFile.end - in)) {
      mSetWarning("Missing file data");
      len = mFile.end - in;
    }
    // debug_printf( "addr: $%04X, len: $%04X\n", addr, len );
    if (addr < RAM_START && addr >= 0x400)  // several tracks use low data
      debug_printf("Block addr in ROM\n");
    memcpy(&mMem.ram[addr], in, len);

    if (mFile.end - blocks < 8) {
      mSetWarning("Missing file data");
      break;
    }
  } while ((addr = get_be16(blocks)) != 0);

  // copy and configure driver
  uint16_t play_addr = get_be16(more_data + 4);
  if (play_addr) {
    static const uint8_t PLAYER_ACTIVE[] PROGMEM = {
        0xF3,              // DI
        0xCD, 0x00, 0x00,  // CALL init
        0xED, 0x56,        // LOOP: IM 1
        0xFB,              // EI
        0x76,              // HALT
        0xCD, 0x00, 0x00,  // CALL play
        0x18, 0xF7         // JR LOOP
    };
    memcpy_P(mMem.ram.begin(), PLAYER_ACTIVE, sizeof(PLAYER_ACTIVE));
    set_le16(&mMem.ram[9], play_addr);
  } else {
    static const uint8_t PLAYER_PASSIVE[] PROGMEM = {
        0xF3,              // DI
        0xCD, 0x00, 0x00,  // CALL init
        0xED, 0x5E,        // LOOP: IM 2
        0xFB,              // EI
        0x76,              // HALT
        0x18, 0xFA         // JR LOOP
    };
    memcpy_P(mMem.ram.begin(), PLAYER_PASSIVE, sizeof(PLAYER_PASSIVE));
  }
  set_le16(&mMem.ram[2], init);

  mMem.ram[0x38] = 0xFB;  // Put EI at interrupt vector (followed by RET)

  std::copy(&mMem.ram[0], &mMem.ram[0x80], &mMem.ram[0x10000]);  // some code wraps around (ugh)

  mBeeperDelta = int(mApu.AMP_RANGE * 0.65);
  mLastBeeper = 0;
  mApu.Reset();
  mNextPlay = mPlayPeriod;

  // start at spectrum speed
  mChangeClockRate(CLK_SPECTRUM);
  SetTempo(mGetTempo());

  mSpectrumMode = false;
  mCpcMode = false;
  mCpcLatch = 0;

  return 0;
}

// Emulation

void AyEmu::mCpuOutMisc(cpu_time_t time, unsigned addr, int data) {
  if (!mCpcMode) {
    switch (addr & 0xFEFF) {
      case 0xFEFD:
        mSpectrumMode = true;
        mApuReg = data & 0x0F;
        return;

      case 0xBEFD:
        mSpectrumMode = true;
        mApu.Write(time, mApuReg, data);
        return;
    }
  }

  if (!mSpectrumMode) {
    switch (addr >> 8) {
      case 0xF6:
        switch (data & 0xC0) {
          case 0xC0:
            mApuReg = mCpcLatch & 0x0F;
            goto enable_cpc;

          case 0x80:
            mApu.Write(time, mApuReg, mCpcLatch);
            goto enable_cpc;
        }
        break;

      case 0xF4:
        mCpcLatch = data;
        goto enable_cpc;
    }
  }

  debug_printf("Unmapped OUT: $%04X <- $%02X\n", addr, data);
  return;

enable_cpc:
  if (!mCpcMode) {
    mCpcMode = true;
    mChangeClockRate(CLK_AMSTRAD_CPC);
    SetTempo(mGetTempo());
  }
}

void ay_cpu_out(AyCpu *cpu, cpu_time_t time, unsigned addr, int data) {
  AyEmu &emu = STATIC_CAST(AyEmu &, *cpu);

  if ((addr & 0xFF) == 0xFE && !emu.mCpcMode) {
    int delta = emu.mBeeperDelta;
    data &= 0x10;
    if (emu.mLastBeeper != data) {
      emu.mLastBeeper = data;
      emu.mBeeperDelta = -delta;
      emu.mSpectrumMode = true;
      if (emu.mBeeperOutput)
        emu.mApu.mSynth.Offset(emu.mBeeperOutput, time, delta);
    }
  } else {
    emu.mCpuOutMisc(time, addr, data);
  }
}

int ay_cpu_in(AyCpu *, unsigned addr) {
  // keyboard read and other things
  if ((addr & 0xFF) == 0xFE)
    return 0xFF;  // other values break some beeper tunes

  debug_printf("Unmapped IN : $%04X\n", addr);
  return 0xFF;
}

blargg_err_t AyEmu::mRunClocks(blip_clk_time_t &duration) {
  SetTime(0);
  if (!(mSpectrumMode | mCpcMode))
    duration /= 2;  // until mode is set, leave room for halved clock rate

  while (Time() < duration) {
    cpu::Run(std::min(duration, (blip_clk_time_t) mNextPlay));

    if (Time() >= mNextPlay) {
      mNextPlay += mPlayPeriod;

      if (r.iff1) {
        if (mMem.ram[r.pc] == 0x76)
          r.pc++;

        r.iff1 = r.iff2 = 0;

        mMem.ram[--r.sp] = uint8_t(r.pc >> 8);
        mMem.ram[--r.sp] = uint8_t(r.pc);
        r.pc = 0x38;
        cpu::AdjustTime(12);
        if (r.im == 2) {
          cpu::AdjustTime(6);
          unsigned addr = r.i * 0x100u + 0xFF;
          r.pc = mMem.ram[(addr + 1) & 0xFFFF] * 0x100u + mMem.ram[addr];
        }
      }
    }
  }
  duration = Time();
  mNextPlay -= duration;
  check(mNextPlay >= 0);
  AdjustTime(-duration);

  mApu.EndFrame(duration);

  return 0;
}

}  // namespace ay
}  // namespace emu
}  // namespace gme

static gme_type_t_ const gme_ay_type_ = {
    "ZX Spectrum", 0, 0, &gme::emu::ay::AyEmu::createAyEmu, &gme::emu::ay::AyFile::createAyFile, "AY", 1};
extern gme_type_t const gme_ay_type = &gme_ay_type_;
