// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "GbsEmu.h"

#include "blargg_endian.h"
#include <cstring>

/* Copyright (C) 2003-2006 Shay Green. This module is free software; you
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
namespace gb {

GbsEmu::equalizer_t const GbsEmu::handheld_eq = MusicEmu::make_equalizer(-47.0, 2000);
GbsEmu::equalizer_t const GbsEmu::headphones_eq = MusicEmu::make_equalizer(0.0, 300);

GbsEmu::GbsEmu() {
  m_setType(gme_gbs_type);

  static const char *const names[GbApu::OSC_NUM] = {"Square 1", "Square 2", "Wave", "Noise"};
  m_setChannelsNames(names);

  static int const types[GbApu::OSC_NUM] = {WAVE_TYPE | 1, WAVE_TYPE | 2, WAVE_TYPE | 0, MIXED_TYPE | 0};
  m_setChannelsTypes(types);

  m_setSilenceLookahead(6);
  m_setMaxInitSilence(21);
  setGain(1.2);

  set_equalizer(make_equalizer(-1.0, 120));
}

GbsEmu::~GbsEmu() {}

void GbsEmu::mUnload() {
  m_rom.clear();
  MusicEmu::mUnload();
}

// Track info

static void copy_gbs_fields(GbsEmu::Header const &h, track_info_t *out) {
  GME_COPY_FIELD(h, out, game);
  GME_COPY_FIELD(h, out, author);
  GME_COPY_FIELD(h, out, copyright);
}

blargg_err_t GbsEmu::mGetTrackInfo(track_info_t *out, int) const {
  copy_gbs_fields(m_header, out);
  return 0;
}

static blargg_err_t check_gbs_header(void const *header) {
  if (memcmp(header, "GBS", 3))
    return gme_wrong_file_type;
  return 0;
}

struct GbsFile : GmeInfo {
  GbsEmu::Header h;

  GbsFile() { m_setType(gme_gbs_type); }
  static MusicEmu *createGbsFile() { return BLARGG_NEW GbsFile; }

  blargg_err_t mLoad(DataReader &in) {
    blargg_err_t err = in.read(&h, GbsEmu::HEADER_SIZE);
    if (err)
      return (err == in.eof_error ? gme_wrong_file_type : err);

    m_setTrackNum(h.track_count);
    return check_gbs_header(&h);
  }

  blargg_err_t mGetTrackInfo(track_info_t *out, int) const {
    copy_gbs_fields(h, out);
    return 0;
  }
};

// Setup

blargg_err_t GbsEmu::mLoad(DataReader &in) {
  assert(offsetof(Header, copyright[32]) == HEADER_SIZE);
  RETURN_ERR(m_rom.load(in, HEADER_SIZE, &m_header, 0));

  m_setTrackNum(m_header.track_count);
  RETURN_ERR(check_gbs_header(&m_header));

  if (m_header.vers != 1)
    m_setWarning("Unknown file version");

  if (m_header.timer_mode & 0x78)
    m_setWarning("Invalid timer mode");

  unsigned load_addr = get_le16(m_header.load_addr);
  if ((m_header.load_addr[1] | m_header.init_addr[1] | m_header.play_addr[1]) > 0x7F || load_addr < 0x400)
    m_setWarning("Invalid load/init/play address");

  m_setChannelsNumber(GbApu::OSC_NUM);

  m_apu.setVolume(m_getGain());

  return m_setupBuffer(4194304);
}

void GbsEmu::m_updateEq(BlipEq const &eq) { m_apu.setTrebleEq(eq); }

void GbsEmu::m_setChannel(int i, BlipBuffer *c, BlipBuffer *l, BlipBuffer *r) { m_apu.setOscOutput(i, c, l, r); }

// Emulation

// see gb_cpu_io.h for read/write functions

void GbsEmu::m_setBank(int n) {
  // Only valid for MBC1 cartridges, but hopefully shouldn't hurt
  n &= 0x1f;
  if (n == 0) {
    n = 1;
  }

  blargg_long addr = n * (blargg_long) BANK_SIZE;
  if (addr > m_rom.size()) {
    return;
  }
  cpu::map_code(BANK_SIZE, BANK_SIZE, m_rom.atAddr(m_rom.maskAddr(addr)));
}

void GbsEmu::m_updateTimer() {
  if (m_header.timer_mode & 0x04) {
    static uint8_t const rates[4] = {10, 4, 6, 8};
    int shift = rates[m_ram[HI_PAGE + 7] & 3] - (m_header.timer_mode >> 7);
    m_playPeriod = (256L - m_ram[HI_PAGE + 6]) << shift;
  } else {
    m_playPeriod = 70224;  // 59.73 Hz
  }
  if (m_getTempo() != 1.0)
    m_playPeriod = blip_time_t(m_playPeriod / m_getTempo());
}

static uint8_t const sound_data[GbApu::REGS_NUM] = {0x80, 0xBF, 0x00, 0x00, 0xBF,  // square 1
                                                    0x00, 0x3F, 0x00, 0x00, 0xBF,  // square 2
                                                    0x7F, 0xFF, 0x9F, 0x00, 0xBF,  // wave
                                                    0x00, 0xFF, 0x00, 0x00, 0xBF,  // noise
                                                    0x77, 0xF3, 0xF1,              // vin/volume, status, power mode
                                                    0,    0,    0,    0,    0,    0,    0,    0,    0,  // unused
                                                    0xAC, 0xDD, 0xDA, 0x48, 0x36, 0x02, 0xCF, 0x16,     // waveform data
                                                    0x2C, 0x04, 0xE5, 0x2C, 0xAC, 0xDD, 0xDA, 0x48};

void GbsEmu::m_cpuJsr(gb_addr_t addr) {
  check(cpu::r.sp == get_le16(m_header.stack_ptr));
  cpu::r.pc = addr;
  cpu_write(--cpu::r.sp, IDLE_ADDR >> 8);
  cpu_write(--cpu::r.sp, IDLE_ADDR & 0xFF);
}

void GbsEmu::m_setTempo(double t) {
  m_apu.setTempo(t);
  m_updateTimer();
}

blargg_err_t GbsEmu::m_startTrack(int track) {
  RETURN_ERR(ClassicEmu::m_startTrack(track));

  std::fill_n(m_ram.begin(), 0x4000, 0x00);
  std::fill_n(m_ram.begin() + 0x4000, 0x1F00, 0xFF);
  std::fill(m_ram.begin() + 0x5F00, m_ram.end(), 0x00);
  m_ram[HI_PAGE] = 0;  // joypad reads back as 0

  m_apu.reset();
  for (int i = 0; i < (int) sizeof sound_data; i++)
    m_apu.writeRegister(0, i + GbApu::START_ADDR, sound_data[i]);

  unsigned load_addr = get_le16(m_header.load_addr);
  m_rom.setAddr(load_addr);
  cpu::rst_base = load_addr;

  cpu::reset(m_rom.unmapped());

  cpu::map_code(RAM_ADDR, 0x10000 - RAM_ADDR, m_ram.data());
  cpu::map_code(0, BANK_SIZE, m_rom.atAddr(0));
  m_setBank(m_rom.size() > BANK_SIZE);

  m_ram[HI_PAGE + 6] = m_header.timer_modulo;
  m_ram[HI_PAGE + 7] = m_header.timer_mode;
  m_updateTimer();
  m_nextPlay = m_playPeriod;

  cpu::r.a = track;
  cpu::r.pc = IDLE_ADDR;
  cpu::r.sp = get_le16(m_header.stack_ptr);
  m_cpuTime = 0;
  m_cpuJsr(get_le16(m_header.init_addr));

  return 0;
}

blargg_err_t GbsEmu::m_runClocks(blip_time_t &duration, int) {
  m_cpuTime = 0;
  while (m_cpuTime < duration) {
    long count = duration - m_cpuTime;
    m_cpuTime = duration;
    bool result = cpu::run(count);
    m_cpuTime -= cpu::remain();

    if (result) {
      if (cpu::r.pc == IDLE_ADDR) {
        if (m_nextPlay > duration) {
          m_cpuTime = duration;
          break;
        }

        if (m_cpuTime < m_nextPlay)
          m_cpuTime = m_nextPlay;
        m_nextPlay += m_playPeriod;
        m_cpuJsr(get_le16(m_header.play_addr));
        GME_FRAME_HOOK(this);
        // TODO: handle timer rates different than 60 Hz
      } else if (cpu::r.pc > 0xFFFF) {
        debug_printf("PC wrapped around\n");
        cpu::r.pc &= 0xFFFF;
      } else {
        m_setWarning("Emulation error (illegal/unsupported instruction)");
        debug_printf("Bad opcode $%.2x at $%.4x\n", (int) *cpu::get_code(cpu::r.pc), (int) cpu::r.pc);
        cpu::r.pc = (cpu::r.pc + 1) & 0xFFFF;
        m_cpuTime += 6;
      }
    }
  }

  duration = m_cpuTime;
  m_nextPlay -= m_cpuTime;
  if (m_nextPlay < 0)  // could go negative if routine is taking too long to return
    m_nextPlay = 0;
  m_apu.endFrame(m_cpuTime);

  return 0;
}

}  // namespace gb
}  // namespace emu
}  // namespace gme

static gme_type_t_ const gme_gbs_type_ = {
    "Game Boy", 0, &gme::emu::gb::GbsEmu::createGbsEmu, &gme::emu::gb::GbsFile::createGbsFile, "GBS", 1};
extern gme_type_t const gme_gbs_type = &gme_gbs_type_;
