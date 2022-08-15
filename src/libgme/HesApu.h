// Turbo Grafx 16 (PC Engine) PSG sound chip emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once
#include "BlipBuffer.h"
#include "blargg_common.h"

namespace gme {
namespace emu {
namespace hes {

struct HesOsc {
  unsigned char wave[32];
  short volume[2];
  int last_amp[2];
  int mDelay;
  int period;
  unsigned char noise;
  unsigned char phase;
  unsigned char balance;
  unsigned char dac;
  blip_time_t last_time;

  BlipBuffer *outputs[2];
  BlipBuffer *chans[3];
  unsigned noise_lfsr;
  unsigned char control;

  enum { amp_range = 0x8000 };
  typedef BlipSynth<BLIP_MED_QUALITY, 1> synth_t;

  void run_until(synth_t &synth, blip_time_t);
};

class HesApu {
 public:
  void treble_eq(BlipEq const &eq) { synth.setTrebleEq(eq); }
  void volume(double v) { synth.setVolume(1.8 / OSCS_NUM / HesOsc::amp_range * v); }

  enum { OSCS_NUM = 6 };
  void osc_output(int index, BlipBuffer *center, BlipBuffer *left, BlipBuffer *right);

  void reset();

  enum { START_ADDR = 0x0800 };
  enum { END_ADDR = 0x0809 };
  void write_data(blip_time_t, int addr, int data);

  void end_frame(blip_time_t);

 public:
  HesApu();

 private:
  HesOsc oscs[OSCS_NUM];
  int latch;
  int balance;
  HesOsc::synth_t synth;

  void balance_changed(HesOsc &);
  void recalc_chans();
};

}  // namespace hes
}  // namespace emu
}  // namespace gme
