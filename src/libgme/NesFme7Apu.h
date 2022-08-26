// Sunsoft FME-7 sound emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once
#include "BlipBuffer.h"
#include "blargg_common.h"
#include "NesApu.h"

namespace gme {
namespace emu {
namespace nes {

struct fme7_apu_state_t {
  static const size_t REGS_NUM = 14;
  uint8_t mRegs[REGS_NUM];
  uint8_t phases[3];  // 0 or 1
  uint8_t latch;
  uint16_t delays[3];  // a, b, c
};

class NesFme7Apu : private fme7_apu_state_t {
 public:
  // See NesApu.h for reference
  void reset();
  void volume(double);
  void treble_eq(BlipEq const &);
  void output(BlipBuffer *);
  enum { OSCS_NUM = 3 };
  void osc_output(int index, BlipBuffer *);
  void end_frame(blip_time_t);
  void save_state(fme7_apu_state_t *) const;
  void load_state(fme7_apu_state_t const &);

  // Mask and addresses of registers
  static const nes_addr_t ADDR_MASK = 0xE000;
  static const nes_addr_t DATA_ADDR = 0xE000;
  static const nes_addr_t LATCH_ADDR = 0xC000;

  // (addr & ADDR_MASK) == LATCH_ADDR
  void write_latch(int);

  // (addr & ADDR_MASK) == DATA_ADDR
  void write_data(blip_time_t, int data);

 public:
  NesFme7Apu();
 private:
  // noncopyable
  NesFme7Apu(const NesFme7Apu &);
  NesFme7Apu &operator=(const NesFme7Apu &);

  static const uint8_t amp_table[16];

  struct {
    BlipBuffer *output;
    int last_amp;
  } oscs[OSCS_NUM];
  blip_time_t last_time;

  enum { amp_range = 192 };  // can be any value; this gives best error/quality tradeoff
  BlipSynth<BLIP_GOOD_QUALITY, 1> synth;

  void run_until(blip_time_t);
};

inline void NesFme7Apu::volume(double v) {
  synth.SetVolume(0.38 / amp_range * v);  // to do: fine-tune
}

inline void NesFme7Apu::treble_eq(BlipEq const &eq) { synth.SetTrebleEq(eq); }

inline void NesFme7Apu::osc_output(int i, BlipBuffer *buf) {
  assert((unsigned) i < OSCS_NUM);
  oscs[i].output = buf;
}

inline void NesFme7Apu::output(BlipBuffer *buf) {
  for (int i = 0; i < OSCS_NUM; i++)
    osc_output(i, buf);
}

inline NesFme7Apu::NesFme7Apu() {
  output(NULL);
  volume(1.0);
  reset();
}

inline void NesFme7Apu::write_latch(int data) { latch = data; }

inline void NesFme7Apu::write_data(blip_time_t time, int data) {
  if ((unsigned) latch >= REGS_NUM) {
#ifdef debug_printf
    debug_printf("FME7 write to %02X (past end of sound registers)\n", (int) latch);
#endif
    return;
  }

  run_until(time);
  mRegs[latch] = data;
}

inline void NesFme7Apu::end_frame(blip_time_t time) {
  if (time > last_time)
    run_until(time);

  assert(last_time >= time);
  last_time -= time;
}

inline void NesFme7Apu::save_state(fme7_apu_state_t *out) const { *out = *this; }

inline void NesFme7Apu::load_state(fme7_apu_state_t const &in) {
  reset();
  fme7_apu_state_t *state = this;
  *state = in;
}

}  // namespace nes
}  // namespace emu
}  // namespace gme
