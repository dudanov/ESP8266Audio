// Namco 106 sound chip emulator

// Nes_Snd_Emu 0.1.8
#pragma once

#include "BlipBuffer.h"
#include "blargg_common.h"

namespace gme {
namespace emu {
namespace nes {

struct namco_state_t;

class NesNamcoApu {
 public:
  // See NesApu.h for reference.
  void volume(double v) { m_synth.setVolume(0.10 / OSCS_NUM * v); }
  void treble_eq(const BlipEq &eq) { m_synth.setTrebleEq(eq); }
  void output(BlipBuffer *);
  static const int OSCS_NUM = 8;
  void osc_output(int i, BlipBuffer *buf) {
    assert((unsigned) i < OSCS_NUM);
    oscs[i].output = buf;
  }
  void reset();
  void end_frame(blip_time_t);

  // Read/write data register is at 0x4800
  enum { data_reg_addr = 0x4800 };
  void write_data(blip_time_t time, int data) {
    run_until(time);
    access() = data;
  }
  int read_data() { return access(); }

  // Write-only address register is at 0xF800
  enum { addr_reg_addr = 0xF800 };
  void write_addr(int v) { m_regAddr = v; }

  // to do: implement save/restore
  void save_state(namco_state_t *out) const;
  void load_state(namco_state_t const &);

 public:
  NesNamcoApu();
 private:
  NesNamcoApu(const NesNamcoApu &) = delete;
  NesNamcoApu &operator=(const NesNamcoApu &) = delete;

  struct NamcoOsc {
    blargg_long delay;
    BlipBuffer *output;
    short last_amp;
    short wave_pos;
  };

  NamcoOsc oscs[OSCS_NUM];

  blip_time_t last_time;
  int m_regAddr;

  enum { REGS_NUM = 0x80 };
  std::array<uint8_t, REGS_NUM> m_regs;
  BlipSynth<BLIP_GOOD_QUALITY, 15> m_synth;

  uint8_t &access() {
    int addr = m_regAddr & 0x7F;
    if (m_regAddr & 0x80)
      m_regAddr = (addr + 1) | 0x80;
    return m_regs[addr];
  }
  void run_until(blip_time_t);
};
/*
struct namco_state_t
{
        uint8_t regs [0x80];
        uint8_t addr;
        uint8_t unused;
        uint8_t positions [8];
        uint32_t delays [8];
};
*/

}  // namespace nes
}  // namespace emu
}  // namespace gme
