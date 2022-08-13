// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "NsfEmu.h"

#include "blargg_endian.h"
#include <algorithm>
#include <cstring>
#include <stdio.h>

#if !NSF_EMU_APU_ONLY
#include "NesFme7Apu.h"
#include "NesNamcoApu.h"
#include "NesVrc6Apu.h"
#endif

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
namespace nes {

static const int VRC6_FLAG = 0x01;
static const int NAMCO_FLAG = 0x10;
static const int FME7_FLAG = 0x20;
static const long CLK_DIV = 12;

NsfEmu::equalizer_t const NsfEmu::nes_eq = MusicEmu::MakeEqualizer(-1.0, 80);
NsfEmu::equalizer_t const NsfEmu::famicom_eq = MusicEmu::MakeEqualizer(-15.0, 80);

int NsfEmu::pcmRead(void *emu, nes_addr_t addr) { return *((NsfEmu *) emu)->cpu::getCode(addr); }

NsfEmu::NsfEmu() {
  this->vrc6 = nullptr;
  this->namco = nullptr;
  this->fme7 = nullptr;

  this->m_setType(gme_nsf_type);
  this->mSetSilenceLookahead(6);
  this->m_apu.SetDmcReader(pcmRead, this);
  MusicEmu::SetEqualizer(nes_eq);
  SetGain(1.4);
  std::fill(this->m_unMappedCode.begin(), this->m_unMappedCode.end(), NesCpu::BAD_OPCODE);
}

NsfEmu::~NsfEmu() { this->mUnload(); }

void NsfEmu::mUnload() {
#if !NSF_EMU_APU_ONLY
  {
    delete vrc6;
    vrc6 = 0;

    delete namco;
    namco = 0;

    delete fme7;
    fme7 = 0;
  }
#endif

  this->m_rom.clear();
  MusicEmu::mUnload();
}

// Track info

static void copy_nsf_fields(NsfEmu::Header const &h, track_info_t *out) {
  GME_COPY_FIELD(h, out, game);
  GME_COPY_FIELD(h, out, author);
  GME_COPY_FIELD(h, out, copyright);
  if (h.chip_flags)
    GmeFile::copyField(out->system, "Famicom");
}

blargg_err_t NsfEmu::mGetTrackInfo(track_info_t *out, int) const {
  copy_nsf_fields(this->m_header, out);
  return 0;
}

static blargg_err_t check_nsf_header(void const *header) {
  if (memcmp(header, "NESM\x1A", 5))
    return gme_wrong_file_type;
  return 0;
}

struct NsfFile : GmeInfo {
  NsfEmu::Header hdr;
  NsfFile() { this->m_setType(gme_nsf_type); }
  static MusicEmu *createNsfFile() { return BLARGG_NEW NsfFile; }
  blargg_err_t mLoad(DataReader &in) override {
    blargg_err_t err = in.read(&hdr, NsfEmu::HEADER_SIZE);
    if (err)
      return (err == in.eof_error ? gme_wrong_file_type : err);

    if (hdr.chip_flags & ~(NAMCO_FLAG | VRC6_FLAG | FME7_FLAG))
      this->m_setWarning("Uses unsupported audio expansion hardware");

    this->m_setTrackNum(hdr.track_count);
    return check_nsf_header(&hdr);
  }

  blargg_err_t mGetTrackInfo(track_info_t *out, int) const override {
    copy_nsf_fields(hdr, out);
    return 0;
  }
};

// Setup

void NsfEmu::mSetTempo(double t) {
  uint16_t playback_rate = get_le16(this->m_header.ntsc_speed);
  uint16_t standard_rate = 0x411A;
  this->m_clockRate = 1789772.72727f;
  this->m_playPeriod = 262 * 341L * 4 - 2;  // two fewer PPU clocks every four frames

  if (this->m_palMode) {
    this->m_playPeriod = 33247 * CLK_DIV;
    this->m_clockRate = 1662607.125;
    standard_rate = 0x4E20;
    playback_rate = get_le16(this->m_header.pal_speed);
  }

  if (!playback_rate)
    playback_rate = standard_rate;

  if (playback_rate != standard_rate || t != 1.0)
    this->m_playPeriod = long(playback_rate * this->m_clockRate / (1000000.0 / CLK_DIV * t));

  this->m_apu.SetTempo(t);
}

blargg_err_t NsfEmu::mInitSound() {
  if (this->m_header.chip_flags & ~(NAMCO_FLAG | VRC6_FLAG | FME7_FLAG))
    this->m_setWarning("Uses unsupported audio expansion hardware");

  {
#define APU_NAMES "Square 1", "Square 2", "Triangle", "Noise", "DMC"

    int const count = NesApu::OSCS_NUM;
    static const char *const apuNames[count] = {APU_NAMES};
    this->mSetChannelsNumber(count);
    this->mSetChannelsNames(apuNames);
  }

  static int const types[] = {WAVE_TYPE | 1,  WAVE_TYPE | 2,  WAVE_TYPE | 0,  NOISE_TYPE | 0,
                              MIXED_TYPE | 1, WAVE_TYPE | 3,  WAVE_TYPE | 4,  WAVE_TYPE | 5,
                              WAVE_TYPE | 6,  WAVE_TYPE | 7,  WAVE_TYPE | 8,  WAVE_TYPE | 9,
                              WAVE_TYPE | 10, WAVE_TYPE | 11, WAVE_TYPE | 12, WAVE_TYPE | 13};
  this->mSetChannelsTypes(types);  // common to all sound chip configurations

  double adjusted_gain = this->mGetGain();

#if NSF_EMU_APU_ONLY
  if (this->m_header.chip_flags)
    this->m_setWarning("Uses unsupported audio expansion hardware");
#else
  if (this->m_header.chip_flags & (NAMCO_FLAG | VRC6_FLAG | FME7_FLAG))
    this->mSetChannelsNumber(NesApu::OSCS_NUM + 3);

  if (this->m_header.chip_flags & NAMCO_FLAG) {
    namco = BLARGG_NEW NesNamcoApu;
    CHECK_ALLOC(namco);
    adjusted_gain *= 0.75;

    static const int count = NesApu::OSCS_NUM + NesNamcoApu::OSCS_NUM;
    static const char *const names[count] = {APU_NAMES, "Wave 1", "Wave 2", "Wave 3", "Wave 4",
                                             "Wave 5",  "Wave 6", "Wave 7", "Wave 8"};
    this->mSetChannelsNumber(count);
    this->mSetChannelsNames(names);
  }

  if (this->m_header.chip_flags & VRC6_FLAG) {
    vrc6 = BLARGG_NEW NesVrc6Apu;
    CHECK_ALLOC(vrc6);
    adjusted_gain *= 0.75;

    static const int count = NesApu::OSCS_NUM + NesVrc6Apu::OSCS_NUM;
    static const char *const names[count] = {APU_NAMES, "Saw Wave", "Square 3", "Square 4"};
    this->mSetChannelsNumber(count);
    this->mSetChannelsNames(names);

    if (this->m_header.chip_flags & NAMCO_FLAG) {
      static const int count = NesApu::OSCS_NUM + NesVrc6Apu::OSCS_NUM + NesNamcoApu::OSCS_NUM;
      static const char *const names[count] = {APU_NAMES, "Saw Wave", "Square 3", "Square 4", "Wave 1", "Wave 2",
                                               "Wave 3",  "Wave 4",   "Wave 5",   "Wave 6",   "Wave 7", "Wave 8"};
      this->mSetChannelsNumber(count);
      this->mSetChannelsNames(names);
    }
  }

  if (this->m_header.chip_flags & FME7_FLAG) {
    fme7 = BLARGG_NEW NesFme7Apu;
    CHECK_ALLOC(fme7);
    adjusted_gain *= 0.75;

    int const count = NesApu::OSCS_NUM + NesFme7Apu::OSCS_NUM;
    static const char *const names[count] = {APU_NAMES, "Square 3", "Square 4", "Square 5"};
    this->mSetChannelsNumber(count);
    this->mSetChannelsNames(names);
  }

  if (namco)
    namco->volume(adjusted_gain);
  if (vrc6)
    vrc6->volume(adjusted_gain);
  if (fme7)
    fme7->volume(adjusted_gain);
#endif

  this->m_apu.SetVolume(adjusted_gain);

  return 0;
}

blargg_err_t NsfEmu::mLoad(DataReader &in) {
  assert(offsetof(Header, unused[4]) == HEADER_SIZE);
  RETURN_ERR(this->m_rom.load(in, HEADER_SIZE, &this->m_header, 0));

  this->m_setTrackNum(this->m_header.track_count);
  RETURN_ERR(check_nsf_header(&this->m_header));

  if (this->m_header.vers != 1)
    this->m_setWarning("Unknown file version");

  // sound and memory
  blargg_err_t err = this->mInitSound();
  if (err)
    return err;

  // set up data
  nes_addr_t load_addr = get_le16(this->m_header.load_addr);
  this->m_initAddress = get_le16(this->m_header.init_addr);
  this->m_playAddress = get_le16(this->m_header.play_addr);
  if (!load_addr)
    load_addr = ROM_BEGIN;
  if (!this->m_initAddress)
    this->m_initAddress = ROM_BEGIN;
  if (!this->m_playAddress)
    this->m_playAddress = ROM_BEGIN;
  if (load_addr < ROM_BEGIN || this->m_initAddress < ROM_BEGIN) {
    const char *w = warning();
    if (!w)
      w = "Corrupt file (invalid load/init/play address)";
    return w;
  }

  this->m_rom.setAddr(load_addr % BANK_SIZE);
  int total_banks = this->m_rom.size() / BANK_SIZE;

  // bank switching
  int first_bank = (load_addr - ROM_BEGIN) / BANK_SIZE;
  for (size_t i = 0; i < BANKS_NUM; i++) {
    unsigned bank = i - first_bank;
    if (bank >= (unsigned) total_banks)
      bank = 0;
    this->m_initBanks[i] = bank;

    if (this->m_header.banks[i]) {
      // bank-switched
      memcpy(this->m_initBanks.data(), this->m_header.banks, sizeof this->m_initBanks);
      break;
    }
  }

  this->m_palMode = (this->m_header.speed_flags & 3) == 1;

#if !NSF_EMU_EXTRA_FLAGS
  this->m_header.speed_flags = 0;
#endif

  SetTempo(this->mGetTempo());

  return this->mSetupBuffer((long) (this->m_clockRate + 0.5));
}

void NsfEmu::mUpdateEq(BlipEq const &eq) {
  this->m_apu.SetTrebleEq(eq);

#if !NSF_EMU_APU_ONLY
  {
    if (namco)
      namco->treble_eq(eq);
    if (vrc6)
      vrc6->treble_eq(eq);
    if (fme7)
      fme7->treble_eq(eq);
  }
#endif
}

void NsfEmu::mSetChannel(int i, BlipBuffer *buf, BlipBuffer *, BlipBuffer *) {
  if (i < NesApu::OSCS_NUM) {
    this->m_apu.SetOscOutput(i, buf);
    return;
  }
  i -= NesApu::OSCS_NUM;

#if !NSF_EMU_APU_ONLY
  {
    if (fme7 && i < NesFme7Apu::OSCS_NUM) {
      fme7->osc_output(i, buf);
      return;
    }

    if (vrc6) {
      if (i < NesVrc6Apu::OSCS_NUM) {
        // put saw first
        if (--i < 0)
          i = 2;
        vrc6->osc_output(i, buf);
        return;
      }
      i -= NesVrc6Apu::OSCS_NUM;
    }

    if (namco && i < NesNamcoApu::OSCS_NUM) {
      namco->osc_output(i, buf);
      return;
    }
  }
#endif
}

// Emulation

// see nes_cpu_io.h for read/write functions

void NsfEmu::mCpuWriteMisc(nes_addr_t addr, uint8_t data) {
#if !NSF_EMU_APU_ONLY
  {
    if (namco) {
      switch (addr) {
        case NesNamcoApu::data_reg_addr:
          namco->write_data(time(), data);
          return;

        case NesNamcoApu::addr_reg_addr:
          namco->write_addr(data);
          return;
      }
    }

    if (addr >= NesFme7Apu::LATCH_ADDR && fme7) {
      switch (addr & NesFme7Apu::ADDR_MASK) {
        case NesFme7Apu::LATCH_ADDR:
          fme7->write_latch(data);
          return;

        case NesFme7Apu::DATA_ADDR:
          fme7->write_data(time(), data);
          return;
      }
    }

    if (vrc6) {
      unsigned reg = addr & (NesVrc6Apu::ADDR_STEP - 1);
      unsigned osc = unsigned(addr - NesVrc6Apu::BASE_ADDR) / NesVrc6Apu::ADDR_STEP;
      if (osc < NesVrc6Apu::OSCS_NUM && reg < NesVrc6Apu::REGS_NUM) {
        vrc6->write_osc(time(), osc, reg, data);
        return;
      }
    }
  }
#endif

  // unmapped write

#ifndef NDEBUG
  {
    // some games write to $8000 and $8001 repeatedly
    if (addr == 0x8000 || addr == 0x8001)
      return;

    // probably namco sound mistakenly turned on in mck
    if (addr == 0x4800 || addr == 0xF800)
      return;

    // memory mapper?
    if (addr == 0xFFF8)
      return;

    debug_printf("write_unmapped( 0x%04X, 0x%02X )\n", (unsigned) addr, (unsigned) data);
  }
#endif
}

blargg_err_t NsfEmu::mStartTrack(int track) {
  RETURN_ERR(ClassicEmu::mStartTrack(track));

  std::fill(this->m_lowMem.begin(), this->m_lowMem.end(), 0);
  std::fill(this->m_sram.begin(), this->m_sram.end(), 0);

  cpu::reset(this->m_unMappedCode.data());  // also maps lowMem
  cpu::mapCode(SRAM_ADDR, this->m_sram.size(), this->m_sram.data());
  for (size_t i = 0; i < BANKS_NUM; ++i)
    this->mCpuWrite(BANK_SELECT_ADDR + i, this->m_initBanks[i]);

  this->m_apu.Reset(this->m_palMode, (this->m_header.speed_flags & 0x20) ? 0x3F : 0);
  this->m_apu.WriteRegister(0, 0x4015, 0x0F);
  this->m_apu.WriteRegister(0, 0x4017, (this->m_header.speed_flags & 0x10) ? 0x80 : 0);
#if !NSF_EMU_APU_ONLY
  {
    if (namco)
      namco->reset();
    if (vrc6)
      vrc6->reset();
    if (fme7)
      fme7->reset();
  }
#endif

  this->m_playReady = 4;
  this->m_playExtra = 0;
  this->m_nextPlay = this->m_playPeriod / CLK_DIV;

  this->m_savedState.pc = BADOP_ADDR;
  this->m_lowMem[0x1FF] = (BADOP_ADDR - 1) >> 8;
  this->m_lowMem[0x1FE] = (BADOP_ADDR - 1) & 0xFF;
  this->m_regs.sp = 0xFD;
  this->m_regs.pc = this->m_initAddress;
  this->m_regs.a = track;
  this->m_regs.x = this->m_palMode;

  return 0;
}

blargg_err_t NsfEmu::mRunClocks(blip_time_t &duration, int) {
  setTime(0);
  while (time() < duration) {
    nes_time_t end = std::min((blip_time_t) this->m_nextPlay, duration);
    end = std::min(end, time() + 32767);  // allows CPU to use 16-bit time delta
    if (cpu::run(end)) {
      if (this->m_regs.pc != BADOP_ADDR) {
        this->m_setWarning("Emulation error (illegal instruction)");
        this->m_regs.pc++;
      } else {
        this->m_playReady = 1;
        if (this->m_savedState.pc != BADOP_ADDR) {
          cpu::m_regs = this->m_savedState;
          this->m_savedState.pc = BADOP_ADDR;
        } else {
          setTime(end);
        }
      }
    }

    if (time() >= this->m_nextPlay) {
      nes_time_t period = (this->m_playPeriod + this->m_playExtra) / CLK_DIV;
      this->m_playExtra = this->m_playPeriod - period * CLK_DIV;
      this->m_nextPlay += period;
      if (this->m_playReady && !--this->m_playReady) {
        check(this->m_savedState.pc == BADOP_ADDR);
        if (this->m_regs.pc != BADOP_ADDR)
          this->m_savedState = cpu::m_regs;

        this->m_regs.pc = this->m_playAddress;
        this->m_lowMem[0x100 + this->m_regs.sp--] = (BADOP_ADDR - 1) >> 8;
        this->m_lowMem[0x100 + this->m_regs.sp--] = (BADOP_ADDR - 1) & 0xFF;
        GME_FRAME_HOOK(this);
      }
    }
  }

  if (cpu::getErrorsNum()) {
    cpu::clearErrors();
    this->m_setWarning("Emulation error (illegal instruction)");
  }

  duration = time();
  this->m_nextPlay -= duration;
  check(this->m_nextPlay >= 0);
  if (this->m_nextPlay < 0)
    this->m_nextPlay = 0;

  this->m_apu.EndFrame(duration);

#if !NSF_EMU_APU_ONLY
  {
    if (namco)
      namco->end_frame(duration);
    if (vrc6)
      vrc6->end_frame(duration);
    if (fme7)
      fme7->end_frame(duration);
  }
#endif

  return 0;
}

}  // namespace nes
}  // namespace emu
}  // namespace gme

static gme_type_t_ const gme_nsf_type_ = {
    "Nintendo NES", 0, &gme::emu::nes::NsfEmu::createNsfEmu, &gme::emu::nes::NsfFile::createNsfFile, "NSF", 1};
extern gme_type_t const gme_nsf_type = &gme_nsf_type_;
