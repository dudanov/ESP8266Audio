// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "AyEmu.h"

#include "blargg_endian.h"
#include <string.h>

#include <algorithm>  // min, max

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

#include "blargg_source.h"

namespace gme {
namespace emu {
namespace ay {

static const uint32_t CLK_SPECTRUM = 3546900;
static const uint32_t CLK_CPC = 2000000;
static const uint16_t RAM_START = 0x4000;
static const uint8_t OSCS_NUM = AyApu::OSCS_NUM + 1;

using std::max;
using std::min;

AyEmu::AyEmu() {
  static const char *const CHANNELS_NAMES[OSCS_NUM] = {"Wave 1", "Wave 2", "Wave 3", "Beeper"};
  static int const CHANNELS_TYPES[OSCS_NUM] = {WAVE_TYPE | 0, WAVE_TYPE | 1, WAVE_TYPE | 2, MIXED_TYPE | 0};

  beeper_output = 0;
  m_setType(gme_ay_type);

  mSetChannelsNames(CHANNELS_NAMES);

  mSetChannelsTypes(CHANNELS_TYPES);
  mSetSilenceLookahead(6);
}

AyEmu::~AyEmu() {}

// Track info

static uint8_t const *get_data(AyEmu::file_t const &file, uint8_t const *ptr, int min_size) {
  long pos = ptr - (uint8_t const *) file.header;
  long file_size = file.end - (uint8_t const *) file.header;
  assert((unsigned long) pos <= (unsigned long) file_size - 2);
  int offset = (int16_t) get_be16(ptr);
  if (!offset || blargg_ulong(pos + offset) > blargg_ulong(file_size - min_size))
    return 0;
  return ptr + offset;
}

static blargg_err_t parse_header(uint8_t const *in, long size, AyEmu::file_t *out) {
  typedef AyEmu::header_t header_t;
  out->header = (header_t const *) in;
  out->end = in + size;

  if (size < AyEmu::header_size)
    return gme_wrong_file_type;

  header_t const &h = *(header_t const *) in;
  if (memcmp(h.tag, "ZXAYEMUL", 8))
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
  copy_ay_fields(file, out, track);
  return 0;
}
struct AyFile : GmeInfo {
  AyEmu::file_t file;

  AyFile() { m_setType(gme_ay_type); }
  static MusicEmu *createAyFile() { return BLARGG_NEW AyFile; }

  blargg_err_t mLoad(uint8_t const *begin, long size) override {
    RETURN_ERR(parse_header(begin, size, &file));
    m_setTrackNum(file.header->max_track + 1);
    return 0;
  }

  blargg_err_t mGetTrackInfo(track_info_t *out, int track) const override {
    copy_ay_fields(file, out, track);
    return 0;
  }
};

// Setup

blargg_err_t AyEmu::mLoad(uint8_t const *in, long size) {
  assert(offsetof(header_t, track_info[2]) == header_size);

  RETURN_ERR(parse_header(in, size, &file));
  m_setTrackNum(file.header->max_track + 1);

  if (file.header->vers > 2)
    m_setWarning("Unknown file version");

  mSetChannelsNumber(OSCS_NUM);
  apu.SetVolume(mGetGain());

  return mSetupBuffer(CLK_SPECTRUM);
}

void AyEmu::mUpdateEq(BlipEq const &eq) { apu.setTrebleEq(eq); }

void AyEmu::mSetChannel(int i, BlipBuffer *center, BlipBuffer *, BlipBuffer *) {
  if (i >= AyApu::OSCS_NUM)
    beeper_output = center;
  else
    apu.SetOscOutput(i, center);
}

// Emulation

void AyEmu::mSetTempo(double t) { play_period = blip_time_t(mGetClockRate() / 50 / t); }

blargg_err_t AyEmu::mStartTrack(int track) {
  RETURN_ERR(ClassicEmu::mStartTrack(track));

  memset(mem.ram + 0x0000, 0xC9, 0x100);  // fill RST vectors with RET
  memset(mem.ram + 0x0100, 0xFF, 0x4000 - 0x100);
  memset(mem.ram + RAM_START, 0x00, sizeof mem.ram - RAM_START);
  memset(mem.padding1, 0xFF, sizeof mem.padding1);
  memset(mem.ram + 0x10000, 0xFF, sizeof mem.ram - 0x10000);

  // locate data blocks
  uint8_t const *const data = get_data(file, file.tracks + track * 4 + 2, 14);
  if (!data)
    return "File data missing";

  uint8_t const *const more_data = get_data(file, data + 10, 6);
  if (!more_data)
    return "File data missing";

  uint8_t const *blocks = get_data(file, data + 12, 8);
  if (!blocks)
    return "File data missing";

  // initial addresses
  cpu::Reset(mem.ram);
  r.sp = get_be16(more_data);
  r.b.a = r.b.b = r.b.d = r.b.h = data[8];
  r.b.flags = r.b.c = r.b.e = r.b.l = data[9];
  r.alt.w = r.w;
  r.ix = r.iy = r.w.hl;

  unsigned addr = get_be16(blocks);
  if (!addr)
    return "File data missing";

  unsigned init = get_be16(more_data + 2);
  if (!init)
    init = addr;

  // copy blocks into memory
  do {
    blocks += 2;
    unsigned len = get_be16(blocks);
    blocks += 2;
    if (addr + len > 0x10000) {
      m_setWarning("Bad data block size");
      len = 0x10000 - addr;
    }
    check(len);
    uint8_t const *in = get_data(file, blocks, 0);
    blocks += 2;
    if (len > blargg_ulong(file.end - in)) {
      m_setWarning("Missing file data");
      len = file.end - in;
    }
    // debug_printf( "addr: $%04X, len: $%04X\n", addr, len );
    if (addr < RAM_START && addr >= 0x400)  // several tracks use low data
      debug_printf("Block addr in ROM\n");
    memcpy(mem.ram + addr, in, len);

    if (file.end - blocks < 8) {
      m_setWarning("Missing file data");
      break;
    }
  } while ((addr = get_be16(blocks)) != 0);

  // copy and configure driver
  static uint8_t const passive[] = {
      0xF3,           // DI
      0xCD, 0,    0,  // CALL init
      0xED, 0x5E,     // LOOP: IM 2
      0xFB,           // EI
      0x76,           // HALT
      0x18, 0xFA      // JR LOOP
  };
  static uint8_t const active[] = {
      0xF3,           // DI
      0xCD, 0,    0,  // CALL init
      0xED, 0x56,     // LOOP: IM 1
      0xFB,           // EI
      0x76,           // HALT
      0xCD, 0,    0,  // CALL play
      0x18, 0xF7      // JR LOOP
  };
  memcpy(mem.ram, passive, sizeof passive);
  unsigned play_addr = get_be16(more_data + 4);
  // debug_printf( "Play: $%04X\n", play_addr );
  if (play_addr) {
    memcpy(mem.ram, active, sizeof active);
    mem.ram[9] = play_addr;
    mem.ram[10] = play_addr >> 8;
  }
  mem.ram[2] = init;
  mem.ram[3] = init >> 8;

  mem.ram[0x38] = 0xFB;  // Put EI at interrupt vector (followed by RET)

  memcpy(mem.ram + 0x10000, mem.ram, 0x80);  // some code wraps around (ugh)

  beeper_delta = int(apu.AMP_RANGE * 0.65);
  last_beeper = 0;
  apu.Reset();
  next_play = play_period;

  // start at spectrum speed
  mChangeClockRate(CLK_SPECTRUM);
  SetTempo(mGetTempo());

  spectrum_mode = false;
  cpc_mode = false;
  cpc_latch = 0;

  return 0;
}

// Emulation

void AyEmu::cpu_out_misc(cpu_time_t time, unsigned addr, int data) {
  if (!cpc_mode) {
    switch (addr & 0xFEFF) {
      case 0xFEFD:
        spectrum_mode = true;
        apu_addr = data & 0x0F;
        return;

      case 0xBEFD:
        spectrum_mode = true;
        apu.Write(time, apu_addr, data);
        return;
    }
  }

  if (!spectrum_mode) {
    switch (addr >> 8) {
      case 0xF6:
        switch (data & 0xC0) {
          case 0xC0:
            apu_addr = cpc_latch & 0x0F;
            goto enable_cpc;

          case 0x80:
            apu.Write(time, apu_addr, cpc_latch);
            goto enable_cpc;
        }
        break;

      case 0xF4:
        cpc_latch = data;
        goto enable_cpc;
    }
  }

  debug_printf("Unmapped OUT: $%04X <- $%02X\n", addr, data);
  return;

enable_cpc:
  if (!cpc_mode) {
    cpc_mode = true;
    mChangeClockRate(CLK_CPC);
    SetTempo(mGetTempo());
  }
}

void ay_cpu_out(AyCpu *cpu, cpu_time_t time, unsigned addr, int data) {
  AyEmu &emu = STATIC_CAST(AyEmu &, *cpu);

  if ((addr & 0xFF) == 0xFE && !emu.cpc_mode) {
    int delta = emu.beeper_delta;
    data &= 0x10;
    if (emu.last_beeper != data) {
      emu.last_beeper = data;
      emu.beeper_delta = -delta;
      emu.spectrum_mode = true;
      if (emu.beeper_output)
        emu.apu.m_synth.offset(time, delta, emu.beeper_output);
    }
  } else {
    emu.cpu_out_misc(time, addr, data);
  }
}

int ay_cpu_in(AyCpu *, unsigned addr) {
  // keyboard read and other things
  if ((addr & 0xFF) == 0xFE)
    return 0xFF;  // other values break some beeper tunes

  debug_printf("Unmapped IN : $%04X\n", addr);
  return 0xFF;
}

blargg_err_t AyEmu::mRunClocks(blip_time_t &duration, int) {
  SetTime(0);
  if (!(spectrum_mode | cpc_mode))
    duration /= 2;  // until mode is set, leave room for halved clock rate

  while (Time() < duration) {
    cpu::Run(min(duration, (blip_time_t) next_play));

    if (Time() >= next_play) {
      next_play += play_period;

      if (r.iff1) {
        if (mem.ram[r.pc] == 0x76)
          r.pc++;

        r.iff1 = r.iff2 = 0;

        mem.ram[--r.sp] = uint8_t(r.pc >> 8);
        mem.ram[--r.sp] = uint8_t(r.pc);
        r.pc = 0x38;
        cpu::AdjustTime(12);
        if (r.im == 2) {
          cpu::AdjustTime(6);
          unsigned addr = r.i * 0x100u + 0xFF;
          r.pc = mem.ram[(addr + 1) & 0xFFFF] * 0x100u + mem.ram[addr];
        }
      }
    }
  }
  duration = Time();
  next_play -= duration;
  check(next_play >= 0);
  AdjustTime(-duration);

  apu.endFrame(duration);

  return 0;
}

}  // namespace ay
}  // namespace emu
}  // namespace gme

static gme_type_t_ const gme_ay_type_ = {
    "ZX Spectrum", 0, &gme::emu::ay::AyEmu::createAyEmu, &gme::emu::ay::AyFile::createAyFile, "AY", 1};
extern gme_type_t const gme_ay_type = &gme_ay_type_;
