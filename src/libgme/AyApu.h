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

  // Write to register at specified time
  void Write(blip_time_t time, int addr, int data) {
    mRunUntil(time);
    mWriteData(addr, data);
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
  static uint8_t mGetAmp(size_t idx);
  void mRunUntil(blip_time_t);
  void mWriteData(int addr, int data);

  struct Square {
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
    Envelope();
    blip_time_t mDelay;
    const uint8_t *mWave;
    int mPos;
    uint8_t mModes[8][48];  // values already passed through volume table
  };

  static const uint8_t REG_COUNT = 16;

  // EN: 16 internal control registers
  // RU: 16 внутренних регистров управления
  uint8_t mRegs[REG_COUNT];
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
