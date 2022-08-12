// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "HesEmu.h"
#include "blargg_endian.h"
#include <algorithm>
#include <cstring>

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
namespace hes {

int const timer_mask = 0x04;
int const vdp_mask = 0x02;
int const i_flag_mask = 0x04;
int const unmapped = 0xFF;

long const period_60hz = 262 * 455L;  // scanlines * clocks per scanline

using std::max;
using std::min;

HesEmu::HesEmu() {
  timer.raw_load = 0;
  m_setType(gme_hes_type);

  static const char *const names[HesApu::OSCS_NUM] = {"Wave 1", "Wave 2", "Wave 3", "Wave 4", "Multi 1", "Multi 2"};
  m_setChannelsNames(names);

  static int const types[HesApu::OSCS_NUM] = {WAVE_TYPE | 0, WAVE_TYPE | 1,  WAVE_TYPE | 2,
                                              WAVE_TYPE | 3, MIXED_TYPE | 0, MIXED_TYPE | 1};
  mSetChannelsTypes(types);
  m_setSilenceLookahead(6);
  setGain(1.11);
}

HesEmu::~HesEmu() {}

void HesEmu::mUnload() {
  rom.clear();
  MusicEmu::mUnload();
}

// Track info

static uint8_t const *copy_field(uint8_t const *in, char *out) {
  if (in) {
    int len = 0x20;
    if (in[0x1F] && !in[0x2F])
      len = 0x30;  // fields are sometimes 16 bytes longer (ugh)

    // since text fields are where any data could be, detect non-text
    // and fields with data after zero byte terminator

    int i = 0;
    for (i = 0; i < len && in[i]; i++)
      if (((in[i] + 1) & 0xFF) < ' ' + 1)  // also treat 0xFF as non-text
        return 0;                          // non-ASCII found

    for (; i < len; i++)
      if (in[i])
        return 0;  // data after terminator

    GmeFile::copyField(out, (char const *) in, len);
    in += len;
  }
  return in;
}

static void copy_hes_fields(uint8_t const *in, track_info_t *out) {
  if (*in >= ' ') {
    in = copy_field(in, out->game);
    in = copy_field(in, out->author);
    in = copy_field(in, out->copyright);
  }
}

blargg_err_t HesEmu::mGetTrackInfo(track_info_t *out, int) const {
  copy_hes_fields(rom.begin() + 0x20, out);
  return 0;
}

static blargg_err_t check_hes_header(void const *header) {
  if (memcmp(header, "HESM", 4))
    return gme_wrong_file_type;
  return 0;
}

struct HesFile : GmeInfo {
  struct header_t {
    char header[HesEmu::HEADER_SIZE];
    char unused[0x20];
    uint8_t fields[0x30 * 3];
  } h;

  HesFile() { m_setType(gme_hes_type); }
  static MusicEmu *createHesFile() { return BLARGG_NEW HesFile; }

  blargg_err_t mLoad(DataReader &in) override {
    assert(offsetof(header_t, fields) == HesEmu::HEADER_SIZE + 0x20);
    blargg_err_t err = in.read(&h, sizeof h);
    if (err)
      return (err == in.eof_error ? gme_wrong_file_type : err);
    return check_hes_header(&h);
  }

  blargg_err_t mGetTrackInfo(track_info_t *out, int) const override {
    copy_hes_fields(h.fields, out);
    return 0;
  }
};

// Setup

blargg_err_t HesEmu::mLoad(DataReader &in) {
  assert(offsetof(header_t, unused[4]) == HEADER_SIZE);
  RETURN_ERR(rom.load(in, HEADER_SIZE, &header_, unmapped));

  RETURN_ERR(check_hes_header(header_.tag));

  if (header_.vers != 0)
    m_setWarning("Unknown file version");

  if (memcmp(header_.data_tag, "DATA", 4))
    m_setWarning("Data header missing");

  if (memcmp(header_.unused, "\0\0\0\0", 4))
    m_setWarning("Unknown header data");

  // File spec supports multiple blocks, but I haven't found any, and
  // many files have bad sizes in the only block, so it's simpler to
  // just try to load the damn data as best as possible.

  long addr = get_le32(header_.addr);
  long size = get_le32(header_.size);
  long const rom_max = 0x100000;
  if (addr & ~(rom_max - 1)) {
    m_setWarning("Invalid address");
    addr &= rom_max - 1;
  }
  if ((unsigned long) (addr + size) > (unsigned long) rom_max)
    m_setWarning("Invalid size");

  if (size != rom.fileSize()) {
    if (size <= rom.fileSize() - 4 && !memcmp(rom.begin() + size, "DATA", 4))
      m_setWarning("Multiple DATA not supported");
    else if (size < rom.fileSize())
      m_setWarning("Extra file data");
    else
      m_setWarning("Missing file data");
  }

  rom.setAddr(addr);

  m_setChannelsNumber(apu.OSCS_NUM);

  apu.volume(m_getGain());

  return mSetupBuffer(7159091);
}

void HesEmu::mUpdateEq(BlipEq const &eq) { apu.treble_eq(eq); }

void HesEmu::mSetChannel(int i, BlipBuffer *center, BlipBuffer *left, BlipBuffer *right) {
  apu.osc_output(i, center, left, right);
}

// Emulation

void HesEmu::recalc_timer_load() { timer.load = timer.raw_load * timer_base + 1; }

void HesEmu::mSetTempo(double t) {
  play_period = hes_time_t(period_60hz / t);
  timer_base = int(1024 / t);
  recalc_timer_load();
}

blargg_err_t HesEmu::mStartTrack(int track) {
  RETURN_ERR(ClassicEmu::mStartTrack(track));

  memset(ram, 0, sizeof ram);  // some HES music relies on zero fill
  memset(sgx, 0, sizeof sgx);

  apu.reset();
  cpu::reset();

  for (unsigned i = 0; i < sizeof header_.banks; i++)
    set_mmr(i, header_.banks[i]);
  set_mmr(page_count, 0xFF);  // unmapped beyond end of address space

  irq.disables = timer_mask | vdp_mask;
  irq.timer = future_hes_time;
  irq.vdp = future_hes_time;

  timer.enabled = false;
  timer.raw_load = 0x80;
  timer.count = timer.load;
  timer.fired = false;
  timer.last_time = 0;

  vdp.latch = 0;
  vdp.control = 0;
  vdp.next_vbl = 0;

  ram[0x1FF] = (idle_addr - 1) >> 8;
  ram[0x1FE] = (idle_addr - 1) & 0xFF;
  r.sp = 0xFD;
  r.pc = get_le16(header_.init_addr);
  r.a = track;

  recalc_timer_load();
  last_frame_hook = 0;

  return 0;
}

// Hardware

void HesEmu::cpu_write_vdp(int addr, int data) {
  switch (addr) {
    case 0:
      vdp.latch = data & 0x1F;
      break;

    case 2:
      if (vdp.latch == 5) {
        if (data & 0x04)
          m_setWarning("Scanline interrupt unsupported");
        run_until(time());
        vdp.control = data;
        irq_changed();
      } else {
        debug_printf("VDP not supported: $%02X <- $%02X\n", vdp.latch, data);
      }
      break;

    case 3:
      debug_printf("VDP MSB not supported: $%02X <- $%02X\n", vdp.latch, data);
      break;
  }
}

void HesEmu::cpu_write_(hes_addr_t addr, int data) {
  if (unsigned(addr - apu.START_ADDR) <= apu.END_ADDR - apu.START_ADDR) {
    GME_APU_HOOK(this, addr - apu.START_ADDR, data);
    // avoid going way past end when a long block xfer is writing to I/O
    // space
    hes_time_t t = min(time(), end_time() + 8);
    apu.write_data(t, addr, data);
    return;
  }

  hes_time_t time = this->time();
  switch (addr) {
    case 0x0000:
    case 0x0002:
    case 0x0003:
      cpu_write_vdp(addr, data);
      return;

    case 0x0C00: {
      run_until(time);
      timer.raw_load = (data & 0x7F) + 1;
      recalc_timer_load();
      timer.count = timer.load;
      break;
    }

    case 0x0C01:
      data &= 1;
      if (timer.enabled == data)
        return;
      run_until(time);
      timer.enabled = data;
      if (data)
        timer.count = timer.load;
      break;

    case 0x1402:
      run_until(time);
      irq.disables = data;
      if ((data & 0xF8) && (data & 0xF8) != 0xF8)  // flag questionable values
        debug_printf("Int mask: $%02X\n", data);
      break;

    case 0x1403:
      run_until(time);
      if (timer.enabled)
        timer.count = timer.load;
      timer.fired = false;
      break;

#ifndef NDEBUG
    case 0x1000:  // I/O port
    case 0x0402:  // palette
    case 0x0403:
    case 0x0404:
    case 0x0405:
      return;

    default:
      debug_printf("unmapped write $%04X <- $%02X\n", addr, data);
      return;
#endif
  }

  irq_changed();
}

int HesEmu::cpu_read_(hes_addr_t addr) {
  hes_time_t time = this->time();
  addr &= PAGE_SIZE - 1;
  switch (addr) {
    case 0x0000:
      if (irq.vdp > time)
        return 0;
      irq.vdp = future_hes_time;
      run_until(time);
      irq_changed();
      return 0x20;

    case 0x0002:
    case 0x0003:
      debug_printf("VDP read not supported: %d\n", addr);
      return 0;

    case 0x0C01:
      // return timer.enabled; // TODO: remove?
    case 0x0C00:
      run_until(time);
      debug_printf("Timer count read\n");
      return (unsigned) (timer.count - 1) / timer_base;

    case 0x1402:
      return irq.disables;

    case 0x1403: {
      int status = 0;
      if (irq.timer <= time)
        status |= timer_mask;
      if (irq.vdp <= time)
        status |= vdp_mask;
      return status;
    }

#ifndef NDEBUG
    case 0x1000:  // I/O port
    case 0x180C:  // CD-ROM
    case 0x180D:
      break;

    default:
      debug_printf("unmapped read  $%04X\n", addr);
#endif
  }

  return unmapped;
}

// see hes_cpu_io.h for core read/write functions

// Emulation

void HesEmu::run_until(hes_time_t present) {
  while (vdp.next_vbl < present)
    vdp.next_vbl += play_period;

  hes_time_t elapsed = present - timer.last_time;
  if (elapsed > 0) {
    if (timer.enabled) {
      timer.count -= elapsed;
      if (timer.count <= 0)
        timer.count += timer.load;
    }
    timer.last_time = present;
  }
}

void HesEmu::irq_changed() {
  hes_time_t present = time();

  if (irq.timer > present) {
    irq.timer = future_hes_time;
    if (timer.enabled && !timer.fired)
      irq.timer = present + timer.count;
  }

  if (irq.vdp > present) {
    irq.vdp = future_hes_time;
    if (vdp.control & 0x08)
      irq.vdp = vdp.next_vbl;
  }

  hes_time_t time = future_hes_time;
  if (!(irq.disables & timer_mask))
    time = irq.timer;
  if (!(irq.disables & vdp_mask))
    time = min(time, irq.vdp);

  set_irq_time(time);
}

int HesEmu::cpu_done() {
  check(time() >= end_time() || (!(r.status & i_flag_mask) && time() >= irq_time()));

  if (!(r.status & i_flag_mask)) {
    hes_time_t present = time();

    if (irq.timer <= present && !(irq.disables & timer_mask)) {
      timer.fired = true;
      irq.timer = future_hes_time;
      irq_changed();  // overkill, but not worth writing custom code
#if GME_FRAME_HOOK_DEFINED
      {
        unsigned const threshold = period_60hz / 30;
        unsigned long elapsed = present - last_frame_hook;
        if (elapsed - period_60hz + threshold / 2 < threshold) {
          last_frame_hook = present;
          GME_FRAME_HOOK(this);
        }
      }
#endif
      return 0x0A;
    }

    if (irq.vdp <= present && !(irq.disables & vdp_mask)) {
// work around for bugs with music not acknowledging VDP
// run_until( present );
// irq.vdp = future_hes_time;
// irq_changed();
#if GME_FRAME_HOOK_DEFINED
      last_frame_hook = present;
      GME_FRAME_HOOK(this);
#endif
      return 0x08;
    }
  }
  return 0;
}

static void adjTime(blargg_long &time, hes_time_t delta) {
  if (time < future_hes_time) {
    time -= delta;
    if (time < 0)
      time = 0;
  }
}

blargg_err_t HesEmu::mRunClocks(blip_time_t &duration_, int) {
  blip_time_t const duration = duration_;  // cache

  if (cpu::run(duration))
    m_setWarning("Emulation error (illegal instruction)");

  check(time() >= duration);
  // check( time() - duration < 20 ); // Txx instruction could cause going way
  // over

  run_until(duration);

  // end time frame
  timer.last_time -= duration;
  vdp.next_vbl -= duration;
#if GME_FRAME_HOOK_DEFINED
  last_frame_hook -= duration;
#endif
  cpu::end_frame(duration);
  adjTime(irq.timer, duration);
  adjTime(irq.vdp, duration);
  apu.end_frame(duration);

  return 0;
}

}  // namespace hes
}  // namespace emu
}  // namespace gme

static gme_type_t_ const gme_hes_type_ = {
    "PC Engine", 256, &gme::emu::hes::HesEmu::createHesEmu, &gme::emu::hes::HesFile::createHesFile, "HES", 1};
extern gme_type_t const gme_hes_type = &gme_hes_type_;
