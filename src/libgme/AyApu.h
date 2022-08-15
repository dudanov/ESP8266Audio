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
  void SetOutput(BlipBuffer *out) {
    SetOscOutput(0, out);
    SetOscOutput(1, out);
    SetOscOutput(2, out);
  }
  // Reset sound chip
  void Reset();

  // EN: 16 internal control registers
  // RU: 16 внутренних регистров управления
  enum Reg { R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, RNUM };

  // Write to register at specified time
  void Write(blip_time_t time, Reg reg, uint8_t data) {
    mRunUntil(time);
    mWriteRegister(reg, data);
  }
  // Run sound to specified time, end current time frame, then start a new
  // time frame at time 0. Time frames have no effect on emulation and each
  // can be whatever length is convenient.
  void EndFrame(blip_time_t time) {
    if (time > mLastTime)
      mRunUntil(time);

    assert(mLastTime >= time);
    mLastTime -= time;
  }
  // Additional features

  // Set sound output of specific oscillator to buffer, where index is
  // 0, 1, or 2. If buffer is NULL, the specified oscillator is muted.
  void SetOscOutput(uint8_t idx, BlipBuffer *out) {
    assert(idx < OSCS_NUM);
    mSquare[idx].mOutput = out;
  }
  // Set overall volume (default is 1.0)
  void SetVolume(double v) { mSynth.setVolume(0.7 / OSCS_NUM / AMP_RANGE * v); }

  // Set treble equalization (see documentation)
  void setTrebleEq(BlipEq const &eq) { mSynth.setTrebleEq(eq); }

 private:
  std::array<uint8_t, RNUM> mRegs;
  void mWriteRegister(Reg reg, uint8_t data);

  static uint8_t mGetAmp(size_t idx);
  void mRunUntil(blip_time_t);

  struct Square {
    static const uint8_t CLK_PSC = 16;
    BlipBuffer *mOutput;
    blip_time_t mPeriod;
    blip_time_t mDelay;
    short mLastAmp;
    short mPhase;
  };

  struct Noise {
    blip_time_t mDelay;
    blargg_ulong mLfsr;
  };

  struct Envelope {
    enum {
      HOLD = 0b0001,
      ALTERNATE = 0b0010,
      ATTACK = 0b0100,
      CONTINUE = 0b1000,
    };
    Envelope();
    blip_time_t mDelay;
    const uint8_t *mWave;
    int mPos;
    uint8_t mModes[8][48];  // values already passed through volume table
  };

  uint16_t mGetPeriod(uint8_t idx) { return 256 * mRegs[idx * 2 + 1] + mRegs[idx * 2]; }
  blip_time_t mLastTime;
  // EN: 3 square generators
  // RU: 3 генератора прямоугольных сигналов
  std::array<Square, OSCS_NUM> mSquare;
  Noise mNoise;
  Envelope mEnvelope;

 public:
  BlipSynth<BLIP_GOOD_QUALITY, 1> mSynth;
};

}  // namespace ay
}  // namespace emu
}  // namespace gme
