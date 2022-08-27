// Atari POKEY sound chip emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once

#include "blargg_common.h"
#include "BlipBuffer.h"

namespace gme {
namespace emu {
namespace sap {

class SapApuImpl;

class SapApu {
 public:
  enum { OSCS_NUM = 4 };
  void osc_output(int index, BlipBuffer *);

  void reset(SapApuImpl *);

  enum { start_addr = 0xD200 };
  enum { end_addr = 0xD209 };
  void write_data(blip_time_t, unsigned addr, int data);

  void end_frame(blip_time_t);

 public:
  SapApu();

 private:
  struct osc_t {
    unsigned char mRegs[2];
    unsigned char phase;
    unsigned char invert;
    int last_amp;
    blip_time_t mDelay;
    blip_time_t period;  // always recalculated before use; here for convenience
    BlipBuffer *output;
  };
  osc_t oscs[OSCS_NUM];
  SapApuImpl *impl;
  blip_time_t last_time;
  int poly5_pos;
  int poly4_pos;
  int polym_pos;
  int control;

  void calc_periods();
  void mRunUntil(blip_time_t);

  enum { poly4_len = (1L << 4) - 1 };
  enum { poly9_len = (1L << 9) - 1 };
  enum { poly17_len = (1L << 17) - 1 };
  friend class SapApuImpl;
};

// Common tables and Blip_Synth that can be shared among multiple SapApu objects
class SapApuImpl {
 public:
  BlipSynth<BLIP_GOOD_QUALITY, 1> synth;

  SapApuImpl();
  void volume(double d) { synth.SetVolume(1.0 / SapApu::OSCS_NUM / 30 * d); }

 private:
  uint8_t poly4[SapApu::poly4_len / 8 + 1];
  uint8_t poly9[SapApu::poly9_len / 8 + 1];
  uint8_t poly17[SapApu::poly17_len / 8 + 1];
  friend class SapApu;
};

inline void SapApu::osc_output(int i, BlipBuffer *b) {
  assert((unsigned) i < OSCS_NUM);
  oscs[i].output = b;
}

}  // namespace sap
}  // namespace emu
}  // namespace gme
