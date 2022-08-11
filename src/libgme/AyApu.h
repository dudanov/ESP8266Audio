// AY-3-8910 sound chip emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once
#include <array>
#include "BlipBuffer.h"
#include "blargg_common.h"

namespace gme {
namespace emu {
namespace ay {

class AyApu {
 public:
  AyApu();
  static const uint8_t OSCS_NUM = 3;
  static const uint8_t AMP_RANGE = 255;
  // Set buffer to generate all sound into, or disable sound if NULL
  void SetOutput(BlipBuffer *buf) {
    this->SetOscOutput(0, buf);
    this->SetOscOutput(1, buf);
    this->SetOscOutput(2, buf);
  }
  // Reset sound chip
  void Reset();

  // Write to register at specified time
  void Write(blip_time_t time, int addr, int data) {
    this->mRunUntil(time);
    this->mWriteData(addr, data);
  }
  // Run sound to specified time, end current time frame, then start a new
  // time frame at time 0. Time frames have no effect on emulation and each
  // can be whatever length is convenient.
  void endFrame(blip_time_t time) {
    if (time > this->m_lastTime)
      this->mRunUntil(time);

    assert(m_lastTime >= time);
    this->m_lastTime -= time;
  }
  // Additional features

  // Set sound output of specific oscillator to buffer, where index is
  // 0, 1, or 2. If buffer is NULL, the specified oscillator is muted.
  void SetOscOutput(uint8_t idx, BlipBuffer *buf) {
    assert(idx < OSCS_NUM);
    this->m_square[idx].output = buf;
  }
  // Set overall volume (default is 1.0)
  void SetVolume(double v) { this->m_synth.setVolume(0.7 / OSCS_NUM / AMP_RANGE * v); }

  // Set treble equalization (see documentation)
  void setTrebleEq(BlipEq const &eq) { this->m_synth.setTrebleEq(eq); }

 private:
  void mRunUntil(blip_time_t);
  void mWriteData(int addr, int data);

  struct Square {
    BlipBuffer *output;
    blip_time_t period;
    blip_time_t delay;
    short lastAmp;
    short phase;
  };

  struct Noise {
    blip_time_t delay;
    blargg_ulong lfsr;
  };

  struct Envelope {
    Envelope();
    blip_time_t delay;
    const uint8_t *wave;
    int pos;
    uint8_t modes[8][48];  // values already passed through volume table
  };

  static const uint8_t REG_COUNT = 16;

  uint8_t m_regs[REG_COUNT];
  blip_time_t m_lastTime;
  std::array<Square, OSCS_NUM> m_square;
  Noise m_noise;
  Envelope m_envelope;

 public:
  BlipSynth<BLIP_GOOD_QUALITY, 1> m_synth;
};

}  // namespace ay
}  // namespace emu
}  // namespace gme
