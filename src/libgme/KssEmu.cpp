// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "KssEmu.h"

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
namespace kss {

long const CLOCK_RATE = 3579545;
int const OSCS_NUM = ay::AyApu::OSCS_NUM + SccApu::OSCS_NUM;

using std::max;
using std::min;

KssEmu::KssEmu() {
  sn = 0;
  m_setType(gme_kss_type);
  mSetSilenceLookahead(6);
  static const char *const names[OSCS_NUM] = {"Square 1", "Square 2", "Square 3", "Wave 1",
                                              "Wave 2",   "Wave 3",   "Wave 4",   "Wave 5"};
  mSetChannelsNames(names);

  static int const types[OSCS_NUM] = {WAVE_TYPE | 0, WAVE_TYPE | 1, WAVE_TYPE | 2, WAVE_TYPE | 3,
                                      WAVE_TYPE | 4, WAVE_TYPE | 5, WAVE_TYPE | 6, WAVE_TYPE | 7};
  mSetChannelsTypes(types);

  memset(unmapped_read, 0xFF, sizeof unmapped_read);
}

KssEmu::~KssEmu() { mUnload(); }

void KssEmu::mUnload() {
  delete sn;
  sn = 0;
  ClassicEmu::mUnload();
}

// Track info

static void copy_kss_fields(KssEmu::header_t const &h, track_info_t *out) {
  const char *system = "MSX";
  if (h.device_flags & 0x02) {
    system = "Sega Master System";
    if (h.device_flags & 0x04)
      system = "Game Gear";
  }
  GmeFile::copyField(out->system, system);
}

blargg_err_t KssEmu::mGetTrackInfo(track_info_t *out, int) const {
  copy_kss_fields(header_, out);
  return 0;
}

static blargg_err_t check_kss_header(void const *header) {
  if (memcmp(header, "KSCC", 4) && memcmp(header, "KSSX", 4))
    return gme_wrong_file_type;
  return 0;
}

struct KssFile : GmeInfo {
  KssEmu::header_t header_;

  KssFile() { m_setType(gme_kss_type); }
  static MusicEmu *createKssFile() { return BLARGG_NEW KssFile; }

  blargg_err_t mLoad(DataReader &in) override {
    blargg_err_t err = in.read(&header_, KssEmu::HEADER_SIZE);
    if (err)
      return (err == in.eof_error ? gme_wrong_file_type : err);
    return check_kss_header(&header_);
  }

  blargg_err_t mGetTrackInfo(track_info_t *out, int) const override {
    copy_kss_fields(header_, out);
    return 0;
  }
};

// Setup

void KssEmu::update_gain() {
  double g = mGetGain() * 1.4;
  if (scc_accessed)
    g *= 1.5;
  ay.SetVolume(g);
  scc.setVolume(g);
  if (sn)
    sn->setVolume(g);
}

blargg_err_t KssEmu::mLoad(DataReader &in) {
  memset(&header_, 0, sizeof header_);
  assert(offsetof(header_t, device_flags) == HEADER_SIZE - 1);
  assert(offsetof(ext_header_t, msx_audio_vol) == EXT_HEADER_SIZE - 1);
  RETURN_ERR(m_rom.load(in, HEADER_SIZE, STATIC_CAST(header_t *, &header_), 0));

  RETURN_ERR(check_kss_header(header_.tag));

  if (header_.tag[3] == 'C') {
    if (header_.extra_header) {
      header_.extra_header = 0;
      m_setWarning("Unknown data in header");
    }
    if (header_.device_flags & ~0x0F) {
      header_.device_flags &= 0x0F;
      m_setWarning("Unknown data in header");
    }
  } else {
    ext_header_t &ext = header_;
    memcpy(&ext, m_rom.begin(), min((int) EXT_HEADER_SIZE, (int) header_.extra_header));
    if (header_.extra_header > 0x10)
      m_setWarning("Unknown data in header");
  }

  if (header_.device_flags & 0x09)
    m_setWarning("FM sound not supported");

  scc_enabled = 0xC000;
  if (header_.device_flags & 0x04)
    scc_enabled = 0;

  if (header_.device_flags & 0x02 && !sn)
    CHECK_ALLOC(sn = BLARGG_NEW(sms::SmsApu));

  mSetChannelsNumber(OSCS_NUM);

  return mSetupBuffer(CLOCK_RATE);
}

void KssEmu::mUpdateEq(BlipEq const &eq) {
  ay.setTrebleEq(eq);
  scc.treble_eq(eq);
  if (sn)
    sn->setTrebleEq(eq);
}

void KssEmu::mSetChannel(int i, BlipBuffer *center, BlipBuffer *left, BlipBuffer *right) {
  int i2 = i - ay.OSCS_NUM;
  if (i2 >= 0)
    scc.osc_output(i2, center);
  else
    ay.SetOscOutput(i, center);
  if (sn && i < sn->OSCS_NUM)
    sn->setOscOutput(i, center, left, right);
}

// Emulation

void KssEmu::mSetTempo(double t) {
  blip_time_t period = (header_.device_flags & 0x40 ? CLOCK_RATE / 50 : CLOCK_RATE / 60);
  play_period = blip_time_t(period / t);
}

blargg_err_t KssEmu::mStartTrack(int track) {
  RETURN_ERR(ClassicEmu::mStartTrack(track));

  memset(ram, 0xC9, 0x4000);
  memset(ram + 0x4000, 0, sizeof ram - 0x4000);

  // copy driver code to lo RAM
  static uint8_t const bios[] = {
      0xD3, 0xA0, 0xF5, 0x7B, 0xD3, 0xA1, 0xF1, 0xC9,  // $0001: WRTPSG
      0xD3, 0xA0, 0xDB, 0xA2, 0xC9                     // $0009: RDPSG
  };
  static uint8_t const vectors[] = {
      0xC3, 0x01, 0x00,  // $0093: WRTPSG vector
      0xC3, 0x09, 0x00,  // $0096: RDPSG vector
  };
  memcpy(ram + 0x01, bios, sizeof bios);
  memcpy(ram + 0x93, vectors, sizeof vectors);

  // copy non-banked data into RAM
  unsigned load_addr = get_le16(header_.load_addr);
  long orig_load_size = get_le16(header_.load_size);
  long load_size = min(orig_load_size, m_rom.fileSize());
  load_size = min(load_size, long(mem_size - load_addr));
  if (load_size != orig_load_size)
    m_setWarning("Excessive data size");
  memcpy(ram + load_addr, m_rom.begin() + header_.extra_header, load_size);

  m_rom.setAddr(-load_size - header_.extra_header);

  // check available bank data
  blargg_long const bank_size = this->bank_size();
  int max_banks = (m_rom.fileSize() - load_size + bank_size - 1) / bank_size;
  bank_count = header_.bank_mode & 0x7F;
  if (bank_count > max_banks) {
    bank_count = max_banks;
    m_setWarning("Bank data missing");
  }
  // debug_printf( "load_size : $%X\n", load_size );
  // debug_printf( "bank_size : $%X\n", bank_size );
  // debug_printf( "bank_count: %d (%d claimed)\n", bank_count,
  // header_.bank_mode & 0x7F );

  ram[idle_addr] = 0xFF;
  cpu::reset(unmapped_write, unmapped_read);
  cpu::map_mem(0, mem_size, ram, ram);

  ay.Reset();
  scc.reset();
  if (sn)
    sn->reset();
  r.sp = 0xF380;
  ram[--r.sp] = idle_addr >> 8;
  ram[--r.sp] = idle_addr & 0xFF;
  r.b.a = track;
  r.pc = get_le16(header_.init_addr);
  next_play = play_period;
  scc_accessed = false;
  gain_updated = false;
  update_gain();
  ay_latch = 0;

  return 0;
}

void KssEmu::set_bank(int logical, int physical) {
  unsigned const bank_size = this->bank_size();

  unsigned addr = 0x8000;
  if (logical && bank_size == 8 * 1024)
    addr = 0xA000;

  physical -= header_.first_bank;
  if ((unsigned) physical >= (unsigned) bank_count) {
    uint8_t *data = ram + addr;
    cpu::map_mem(addr, bank_size, data, data);
  } else {
    long phys = physical * (blargg_long) bank_size;
    for (unsigned offset = 0; offset < bank_size; offset += PAGE_SIZE)
      cpu::map_mem(addr + offset, PAGE_SIZE, unmapped_write, m_rom.atAddr(phys + offset));
  }
}

void KssEmu::cpu_write(unsigned addr, int data) {
  data &= 0xFF;
  switch (addr) {
    case 0x9000:
      set_bank(0, data);
      return;

    case 0xB000:
      set_bank(1, data);
      return;
  }

  int scc_addr = (addr & 0xDFFF) ^ 0x9800;
  if (scc_addr < scc.REG_COUNT) {
    scc_accessed = true;
    scc.write(time(), scc_addr, data);
    return;
  }

  debug_printf("LD ($%04X),$%02X\n", addr, data);
}

void kss_cpu_write(KssCpu *cpu, unsigned addr, int data) {
  *cpu->write(addr) = data;
  if ((addr & STATIC_CAST(KssEmu &, *cpu).scc_enabled) == 0x8000)
    STATIC_CAST(KssEmu &, *cpu).cpu_write(addr, data);
}

void kss_cpu_out(KssCpu *cpu, cpu_time_t time, unsigned addr, int data) {
  data &= 0xFF;
  KssEmu &emu = STATIC_CAST(KssEmu &, *cpu);
  switch (addr & 0xFF) {
    case 0xA0:
      emu.ay_latch = data & 0x0F;
      return;

    case 0xA1:
      GME_APU_HOOK(&emu, emu.ay_latch, data);
      emu.ay.Write(time, emu.ay_latch, data);
      return;

    case 0x06:
      if (emu.sn && (emu.header_.device_flags & 0x04)) {
        emu.sn->writeGGStereo(time, data);
        return;
      }
      break;

    case 0x7E:
    case 0x7F:
      if (emu.sn) {
        GME_APU_HOOK(&emu, 16, data);
        emu.sn->writeData(time, data);
        return;
      }
      break;

    case 0xFE:
      emu.set_bank(0, data);
      return;

#ifndef NDEBUG
    case 0xF1:  // FM data
      if (data)
        break;  // trap non-zero data
    case 0xF0:  // FM addr
    case 0xA8:  // PPI
      return;
#endif
  }

  debug_printf("OUT $%04X,$%02X\n", addr, data);
}

int kss_cpu_in(KssCpu *, cpu_time_t, unsigned addr) {
  // KssEmu& emu = STATIC_CAST(KssEmu&,*cpu);
  // switch ( addr & 0xFF )
  //{
  //}

  debug_printf("IN $%04X\n", addr);
  return 0;
}

// Emulation

blargg_err_t KssEmu::mRunClocks(blip_time_t &duration, int) {
  while (time() < duration) {
    blip_time_t end = min(duration, next_play);
    cpu::run(min(duration, next_play));
    if (r.pc == idle_addr)
      set_time(end);

    if (time() >= next_play) {
      next_play += play_period;
      if (r.pc == idle_addr) {
        if (!gain_updated) {
          gain_updated = true;
          if (scc_accessed)
            update_gain();
        }

        ram[--r.sp] = idle_addr >> 8;
        ram[--r.sp] = idle_addr & 0xFF;
        r.pc = get_le16(header_.play_addr);
        GME_FRAME_HOOK(this);
      }
    }
  }

  duration = time();
  next_play -= duration;
  check(next_play >= 0);
  adjust_time(-duration);
  ay.endFrame(duration);
  scc.end_frame(duration);
  if (sn)
    sn->endFrame(duration);

  return 0;
}

}  // namespace kss
}  // namespace emu
}  // namespace gme

static gme_type_t_ const gme_kss_type_ = {
    "MSX", 256, &gme::emu::kss::KssEmu::createKssEmu, &gme::emu::kss::KssFile::createKssFile, "KSS", 0x03};
extern gme_type_t const gme_kss_type = &gme_kss_type_;
