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

GbsEmu::equalizer_t const GbsEmu::handheld_eq = MusicEmu::MakeEqualizer(-47.0, 2000);
GbsEmu::equalizer_t const GbsEmu::headphones_eq = MusicEmu::MakeEqualizer(0.0, 300);

GbsEmu::GbsEmu() {
  mSetType(gme_gbs_type);

  static const char *const names[GbApu::OSC_NUM] = {"Square 1", "Square 2", "Wave", "Noise"};
  mSetChannelsNames(names);

  static int const types[GbApu::OSC_NUM] = {WAVE_TYPE | 1, WAVE_TYPE | 2, WAVE_TYPE | 0, MIXED_TYPE | 0};
  mSetChannelsTypes(types);

  mSetSilenceLookahead(6);
  mSetMaxInitSilence(21);
  SetGain(1.2);

  SetEqualizer(MakeEqualizer(-1.0, 120));
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
  copy_gbs_fields(mHeader, out);
  return 0;
}

static blargg_err_t check_gbs_header(void const *header) {
  if (memcmp(header, "GBS", 3))
    return gme_wrong_file_type;
  return 0;
}

struct GbsFile : GmeInfo {
  GbsEmu::Header h;

  GbsFile() { mSetType(gme_gbs_type); }
  static MusicEmu *createGbsFile() { return BLARGG_NEW GbsFile; }

  blargg_err_t mLoad(DataReader &in) override {
    blargg_err_t err = in.read(&h, GbsEmu::HEADER_SIZE);
    if (err)
      return (err == in.eof_error ? gme_wrong_file_type : err);

    mSetTrackNum(h.track_count);
    return check_gbs_header(&h);
  }

  blargg_err_t mGetTrackInfo(track_info_t *out, int) const override {
    copy_gbs_fields(h, out);
    return 0;
  }
};

// Setup

blargg_err_t GbsEmu::mLoad(DataReader &in) {
  assert(offsetof(Header, copyright[32]) == HEADER_SIZE);
  RETURN_ERR(m_rom.load(in, HEADER_SIZE, &mHeader, 0));

  mSetTrackNum(mHeader.track_count);
  RETURN_ERR(check_gbs_header(&mHeader));

  if (mHeader.vers != 1)
    mSetWarning("Unknown file version");

  if (mHeader.timer_mode & 0x78)
    mSetWarning("Invalid timer mode");

  unsigned load_addr = get_le16(mHeader.load_addr);
  if ((mHeader.load_addr[1] | mHeader.init_addr[1] | mHeader.play_addr[1]) > 0x7F || load_addr < 0x400)
    mSetWarning("Invalid load/init/play address");

  mSetChannelsNumber(GbApu::OSC_NUM);

  mApu.setVolume(mGetGain());

  return mSetupBuffer(4194304);
}

void GbsEmu::mUpdateEq(BlipEq const &eq) { mApu.SetTrebleEq(eq); }

void GbsEmu::mSetChannel(int i, BlipBuffer *c, BlipBuffer *l, BlipBuffer *r) { mApu.setOscOutput(i, c, l, r); }

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
  if (mHeader.timer_mode & 0x04) {
    static uint8_t const rates[4] = {10, 4, 6, 8};
    int shift = rates[m_ram[HI_PAGE + 7] & 3] - (mHeader.timer_mode >> 7);
    mPlayPeriod = (256L - m_ram[HI_PAGE + 6]) << shift;
  } else {
    mPlayPeriod = 70224;  // 59.73 Hz
  }
  if (mGetTempo() != 1.0)
    mPlayPeriod = blip_time_t(mPlayPeriod / mGetTempo());
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
  check(cpu::r.sp == get_le16(mHeader.stack_ptr));
  cpu::r.pc = addr;
  cpu_write(--cpu::r.sp, IDLE_ADDR >> 8);
  cpu_write(--cpu::r.sp, IDLE_ADDR & 0xFF);
}

void GbsEmu::mSetTempo(double t) {
  mApu.SetTempo(t);
  m_updateTimer();
}

blargg_err_t GbsEmu::mStartTrack(int track) {
  RETURN_ERR(ClassicEmu::mStartTrack(track));

  std::fill_n(m_ram.begin(), 0x4000, 0x00);
  std::fill_n(m_ram.begin() + 0x4000, 0x1F00, 0xFF);
  std::fill(m_ram.begin() + 0x5F00, m_ram.end(), 0x00);
  m_ram[HI_PAGE] = 0;  // joypad reads back as 0

  mApu.reset();
  for (int i = 0; i < (int) sizeof sound_data; i++)
    mApu.writeRegister(0, i + GbApu::START_ADDR, sound_data[i]);

  unsigned load_addr = get_le16(mHeader.load_addr);
  m_rom.setAddr(load_addr);
  cpu::rst_base = load_addr;

  cpu::reset(m_rom.unmapped());

  cpu::map_code(RAM_ADDR, 0x10000 - RAM_ADDR, m_ram.data());
  cpu::map_code(0, BANK_SIZE, m_rom.atAddr(0));
  m_setBank(m_rom.size() > BANK_SIZE);

  m_ram[HI_PAGE + 6] = mHeader.timer_modulo;
  m_ram[HI_PAGE + 7] = mHeader.timer_mode;
  m_updateTimer();
  mNextPlay = mPlayPeriod;

  cpu::r.a = track;
  cpu::r.pc = IDLE_ADDR;
  cpu::r.sp = get_le16(mHeader.stack_ptr);
  mCpuTime = 0;
  m_cpuJsr(get_le16(mHeader.init_addr));

  return 0;
}

blargg_err_t GbsEmu::mRunClocks(blip_clk_time_t &duration) {
  mCpuTime = 0;
  while (mCpuTime < duration) {
    long count = duration - mCpuTime;
    mCpuTime = duration;
    bool result = cpu::run(count);
    mCpuTime -= cpu::remain();

    if (result) {
      if (cpu::r.pc == IDLE_ADDR) {
        if (mNextPlay > duration) {
          mCpuTime = duration;
          break;
        }

        if (mCpuTime < mNextPlay)
          mCpuTime = mNextPlay;
        mNextPlay += mPlayPeriod;
        m_cpuJsr(get_le16(mHeader.play_addr));
        GME_FRAME_HOOK(this);
        // TODO: handle timer rates different than 60 Hz
      } else if (cpu::r.pc > 0xFFFF) {
        debug_printf("PC wrapped around\n");
        cpu::r.pc &= 0xFFFF;
      } else {
        mSetWarning("Emulation error (illegal/unsupported instruction)");
        debug_printf("Bad opcode $%.2x at $%.4x\n", (int) *cpu::get_code(cpu::r.pc), (int) cpu::r.pc);
        cpu::r.pc = (cpu::r.pc + 1) & 0xFFFF;
        mCpuTime += 6;
      }
    }
  }

  duration = mCpuTime;
  mNextPlay -= mCpuTime;
  if (mNextPlay < 0)  // could go negative if routine is taking too long to return
    mNextPlay = 0;
  mApu.EndFrame(mCpuTime);

  return 0;
}

}  // namespace gb
}  // namespace emu
}  // namespace gme

static gme_type_t_ const gme_gbs_type_ = {
    "Game Boy", 0, 0, &gme::emu::gb::GbsEmu::createGbsEmu, &gme::emu::gb::GbsFile::createGbsFile, "GBS", 1};
extern gme_type_t const gme_gbs_type = &gme_gbs_type_;
