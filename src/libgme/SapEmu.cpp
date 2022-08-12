// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "SapEmu.h"

#include "blargg_endian.h"
#include <algorithm>
#include <string.h>

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
namespace sap {

long const base_scanline_period = 114;

using std::max;
using std::min;

SapEmu::SapEmu() {
  m_setType(gme_sap_type);

  static const char *const names[SapApu::OSCS_NUM * 2] = {
      "Wave 1", "Wave 2", "Wave 3", "Wave 4", "Wave 5", "Wave 6", "Wave 7", "Wave 8",
  };
  m_setChannelsNames(names);

  static int const types[SapApu::OSCS_NUM * 2] = {
      WAVE_TYPE | 1, WAVE_TYPE | 2, WAVE_TYPE | 3, WAVE_TYPE | 0,
      WAVE_TYPE | 5, WAVE_TYPE | 6, WAVE_TYPE | 7, WAVE_TYPE | 4,
  };
  mSetChannelsTypes(types);
  m_setSilenceLookahead(6);
}

SapEmu::~SapEmu() {}

// Track info

// Returns 16 or greater if not hex
inline int from_hex_char(int h) {
  h -= 0x30;
  if ((unsigned) h > 9)
    h = ((h - 0x11) & 0xDF) + 10;
  return h;
}

static long from_hex(uint8_t const *in) {
  unsigned result = 0;
  for (int n = 4; n--;) {
    int h = from_hex_char(*in++);
    if (h > 15)
      return -1;
    result = result * 0x10 + h;
  }
  return result;
}

static int from_dec(uint8_t const *in, uint8_t const *end) {
  if (in >= end)
    return -1;

  int n = 0;
  while (in < end) {
    int dig = *in++ - '0';
    if ((unsigned) dig > 9)
      return -1;
    n = n * 10 + dig;
  }
  return n;
}

static void parse_string(uint8_t const *in, uint8_t const *end, int len, char *out) {
  uint8_t const *start = in;
  if (*in++ == '\"') {
    start++;
    while (in < end && *in != '\"')
      in++;
  } else {
    in = end;
  }
  len = min(len - 1, int(in - start));
  out[len] = 0;
  memcpy(out, start, len);
}

static blargg_err_t parse_info(uint8_t const *in, long size, SapEmu::info_t *out) {
  out->track_count = 1;
  out->author[0] = 0;
  out->name[0] = 0;
  out->copyright[0] = 0;

  if (size < 16 || memcmp(in, "SAP\x0D\x0A", 5))
    return gme_wrong_file_type;

  uint8_t const *file_end = in + size - 5;
  in += 5;
  while (in < file_end && (in[0] != 0xFF || in[1] != 0xFF)) {
    uint8_t const *line_end = in;
    while (line_end < file_end && *line_end != 0x0D)
      line_end++;

    char const *tag = (char const *) in;
    while (in<line_end && * in> ' ')
      in++;
    int tag_len = (char const *) in - tag;

    while (in < line_end && *in <= ' ')
      in++;

    if (tag_len <= 0) {
      // skip line
    } else if (!strncmp("INIT", tag, tag_len)) {
      out->init_addr = from_hex(in);
      if ((unsigned long) out->init_addr > 0xFFFF)
        return "Invalid init address";
    } else if (!strncmp("PLAYER", tag, tag_len)) {
      out->play_addr = from_hex(in);
      if ((unsigned long) out->play_addr > 0xFFFF)
        return "Invalid play address";
    } else if (!strncmp("MUSIC", tag, tag_len)) {
      out->music_addr = from_hex(in);
      if ((unsigned long) out->music_addr > 0xFFFF)
        return "Invalid music address";
    } else if (!strncmp("SONGS", tag, tag_len)) {
      out->track_count = from_dec(in, line_end);
      if (out->track_count <= 0)
        return "Invalid track count";
    } else if (!strncmp("TYPE", tag, tag_len)) {
      switch (out->type = *in) {
        case 'C':
        case 'B':
          break;

        case 'D':
          return "Digimusic not supported";

        default:
          return "Unsupported player type";
      }
    } else if (!strncmp("STEREO", tag, tag_len)) {
      out->stereo = true;
    } else if (!strncmp("FASTPLAY", tag, tag_len)) {
      out->fastplay = from_dec(in, line_end);
      if (out->fastplay <= 0)
        return "Invalid fastplay value";
    } else if (!strncmp("AUTHOR", tag, tag_len)) {
      parse_string(in, line_end, sizeof out->author, out->author);
    } else if (!strncmp("NAME", tag, tag_len)) {
      parse_string(in, line_end, sizeof out->name, out->name);
    } else if (!strncmp("DATE", tag, tag_len)) {
      parse_string(in, line_end, sizeof out->copyright, out->copyright);
    }

    in = line_end + 2;
  }

  if (in[0] != 0xFF || in[1] != 0xFF)
    return "ROM data missing";
  out->rom_data = in + 2;

  return 0;
}

static void copy_sap_fields(SapEmu::info_t const &in, track_info_t *out) {
  GmeFile::copyField(out->game, in.name);
  GmeFile::copyField(out->author, in.author);
  GmeFile::copyField(out->copyright, in.copyright);
}

blargg_err_t SapEmu::mGetTrackInfo(track_info_t *out, int) const {
  copy_sap_fields(info, out);
  return 0;
}

struct SapFile : GmeInfo {
  SapEmu::info_t info;

  SapFile() { m_setType(gme_sap_type); }
  static MusicEmu *createSapFile() { return BLARGG_NEW SapFile; }

  blargg_err_t mLoad(uint8_t const *begin, long size) {
    RETURN_ERR(parse_info(begin, size, &info));
    m_setTrackNum(info.track_count);
    return 0;
  }

  blargg_err_t mGetTrackInfo(track_info_t *out, int) const {
    copy_sap_fields(info, out);
    return 0;
  }
};

// Setup

blargg_err_t SapEmu::mLoad(uint8_t const *in, long size) {
  file_end = in + size;

  info.warning = 0;
  info.type = 'B';
  info.stereo = false;
  info.init_addr = -1;
  info.play_addr = -1;
  info.music_addr = -1;
  info.fastplay = 312;
  RETURN_ERR(parse_info(in, size, &info));

  m_setWarning(info.warning);
  m_setTrackNum(info.track_count);
  m_setChannelsNumber(SapApu::OSCS_NUM << info.stereo);
  apu_impl.volume(m_getGain());

  return mSetupBuffer(1773447);
}

void SapEmu::mUpdateEq(BlipEq const &eq) { apu_impl.synth.setTrebleEq(eq); }

void SapEmu::mSetChannel(int i, BlipBuffer *center, BlipBuffer *left, BlipBuffer *right) {
  int i2 = i - SapApu::OSCS_NUM;
  if (i2 >= 0)
    apu2.osc_output(i2, right);
  else
    apu.osc_output(i, (info.stereo ? left : center));
}

// Emulation

void SapEmu::mSetTempo(double t) { scanline_period = sap_time_t(base_scanline_period / t); }

inline sap_time_t SapEmu::play_period() const { return info.fastplay * scanline_period; }

void SapEmu::cpu_jsr(sap_addr_t addr) {
  check(r.sp >= 0xFE);  // catch anything trying to leave data on stack
  r.pc = addr;
  int high_byte = (idle_addr - 1) >> 8;
  if (r.sp == 0xFE && mem.ram[0x1FF] == high_byte)
    r.sp = 0xFF;                        // pop extra byte off
  mem.ram[0x100 + r.sp--] = high_byte;  // some routines use RTI to return
  mem.ram[0x100 + r.sp--] = high_byte;
  mem.ram[0x100 + r.sp--] = (idle_addr - 1) & 0xFF;
}

void SapEmu::run_routine(sap_addr_t addr) {
  cpu_jsr(addr);
  cpu::run(312 * base_scanline_period * 60);
  check(r.pc == idle_addr);
}

inline void SapEmu::call_init(int track) {
  switch (info.type) {
    case 'B':
      r.a = track;
      run_routine(info.init_addr);
      break;

    case 'C':
      r.a = 0x70;
      r.x = info.music_addr & 0xFF;
      r.y = info.music_addr >> 8;
      run_routine(info.play_addr + 3);
      r.a = 0;
      r.x = track;
      run_routine(info.play_addr + 3);
      break;
  }
}

blargg_err_t SapEmu::mStartTrack(int track) {
  RETURN_ERR(ClassicEmu::mStartTrack(track));

  memset(&mem, 0, sizeof mem);

  uint8_t const *in = info.rom_data;
  while (file_end - in >= 5) {
    unsigned start = get_le16(in);
    unsigned end = get_le16(in + 2);
    // debug_printf( "Block $%04X-$%04X\n", start, end );
    in += 4;
    if (end < start) {
      m_setWarning("Invalid file data block");
      break;
    }
    long len = end - start + 1;
    if (len > file_end - in) {
      m_setWarning("Invalid file data block");
      break;
    }

    memcpy(mem.ram + start, in, len);
    in += len;
    if (file_end - in >= 2 && in[0] == 0xFF && in[1] == 0xFF)
      in += 2;
  }

  apu.reset(&apu_impl);
  apu2.reset(&apu_impl);
  cpu::reset(mem.ram);
  time_mask = 0;  // disables sound during init
  call_init(track);
  time_mask = -1;

  next_play = play_period();

  return 0;
}

// Emulation

// see sap_cpu_io.h for read/write functions

void SapEmu::cpu_write_(sap_addr_t addr, int data) {
  if ((addr ^ SapApu::start_addr) <= (SapApu::end_addr - SapApu::start_addr)) {
    GME_APU_HOOK(this, addr - SapApu::start_addr, data);
    apu.write_data(time() & time_mask, addr, data);
    return;
  }

  if ((addr ^ (SapApu::start_addr + 0x10)) <= (SapApu::end_addr - SapApu::start_addr) && info.stereo) {
    GME_APU_HOOK(this, addr - 0x10 - SapApu::start_addr + 10, data);
    apu2.write_data(time() & time_mask, addr ^ 0x10, data);
    return;
  }

  if ((addr & ~0x0010) != 0xD20F || data != 0x03)
    debug_printf("Unmapped write $%04X <- $%02X\n", addr, data);
}

inline void SapEmu::call_play() {
  switch (info.type) {
    case 'B':
      cpu_jsr(info.play_addr);
      break;

    case 'C':
      cpu_jsr(info.play_addr + 6);
      break;
  }
}

blargg_err_t SapEmu::mRunClocks(blip_time_t &duration, int) {
  set_time(0);
  while (time() < duration) {
    if (cpu::run(duration) || r.pc > idle_addr)
      return "Emulation error (illegal instruction)";

    if (r.pc == idle_addr) {
      if (next_play <= duration) {
        set_time(next_play);
        next_play += play_period();
        call_play();
        GME_FRAME_HOOK(this);
      } else {
        set_time(duration);
      }
    }
  }

  duration = time();
  next_play -= duration;
  check(next_play >= 0);
  if (next_play < 0)
    next_play = 0;
  apu.end_frame(duration);
  if (info.stereo)
    apu2.end_frame(duration);

  return 0;
}

}  // namespace sap
}  // namespace emu
}  // namespace gme

static gme_type_t_ const gme_sap_type_ = {
    "Atari XL", 0, &gme::emu::sap::SapEmu::createSapEmu, &gme::emu::sap::SapFile::createSapFile, "SAP", 1};
extern gme_type_t const gme_sap_type = &gme_sap_type_;
