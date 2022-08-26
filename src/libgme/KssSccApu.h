// Konami SCC sound chip emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once

#include "BlipBuffer.h"
#include "blargg_common.h"
#include <string.h>

namespace gme {
namespace emu {
namespace kss {

class SccApu {
 public:
  // Set buffer to generate all sound into, or disable sound if NULL
  void SetOutput(BlipBuffer *buf) {
    for (int i = 0; i < OSCS_NUM; i++)
      m_oscs[i].output = buf;
  }

  // Reset sound chip
  void reset() {
    m_lastTime = 0;
    for (int i = 0; i < OSCS_NUM; i++)
      memset(&m_oscs[i], 0, offsetof(osc_t, output));
    memset(m_regs, 0, sizeof(m_regs));
  }

  // Write to register at specified time
  enum { REG_COUNT = 0x90 };
  void write(blip_time_t time, int addr, int data) {
    assert((unsigned) addr < REG_COUNT);
    run_until(time);
    m_regs[addr] = data;
  }

  // Run sound to specified time, end current time frame, then start a new
  // time frame at time 0. Time frames have no effect on emulation and each
  // can be whatever length is convenient.
  void end_frame(blip_time_t end_time) {
    if (end_time > m_lastTime)
      run_until(end_time);
    m_lastTime -= end_time;
    assert(m_lastTime >= 0);
  }

  // Additional features

  // Set sound output of specific oscillator to buffer, where index is
  // 0 to 4. If buffer is NULL, the specified oscillator is muted.
  enum { OSCS_NUM = 5 };
  void osc_output(int index, BlipBuffer *b) {
    assert((unsigned) index < OSCS_NUM);
    m_oscs[index].output = b;
  }

  // Set overall volume (default is 1.0)
  void setVolume(double v) { m_synth.SetVolume(0.43 / OSCS_NUM / AMP_RANGE * v); }

  // Set treble equalization (see documentation)
  void treble_eq(BlipEq const &eq) { m_synth.SetTrebleEq(eq); }

 public:
  SccApu() { SetOutput(0); }

 private:
  enum { AMP_RANGE = 0x8000 };
  struct osc_t {
    int mDelay;
    int phase;
    int mLastAmp;
    BlipBuffer *output;
  };
  osc_t m_oscs[OSCS_NUM];
  blip_time_t m_lastTime;
  unsigned char m_regs[REG_COUNT];
  BlipSynth<BLIP_MED_QUALITY, 1> m_synth;

  void run_until(blip_time_t);
};

}  // namespace kss
}  // namespace emu
}  // namespace gme
