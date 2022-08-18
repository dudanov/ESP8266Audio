// Private oscillators used by NesApu

// Nes_Snd_Emu 0.1.8
#pragma once

#include <array>
#include <functional>
#include "blargg_common.h"
#include "NesCpu.h"
#include "BlipBuffer.h"

namespace gme {
namespace emu {
namespace nes {

using DmcReaderFn = std::function<int(void *, nes_addr_t)>;
class NesApu;

struct NesOsc {
  BlipBuffer *mOutput;
  int mLengthCounter;  // length counter (0 if unused by oscillator)
  int mDelay;          // delay until next (potential) transition
  int mLastAmp;        // last amplitude oscillator was outputting
  std::array<uint8_t, 4> mRegs;
  std::array<bool, 4> mRegWritten;

  void SetOutput(BlipBuffer *output) { this->mOutput = output; }
  void doLengthClock(int halt_mask);
  int mGetPeriod() const { return 256 * (this->mRegs[3] & 0b111) + this->mRegs[2]; }

 protected:
  NesOsc() = delete;
  NesOsc(NesApu *apu) : mApu(apu) {}
  NesApu *mApu;
  void mReset() {
    this->mDelay = 0;
    this->mLastAmp = 0;
  }
  int mUpdateAmp(int amp) {
    int delta = amp - this->mLastAmp;
    this->mLastAmp = amp;
    return delta;
  }
#if 0
    void zero_apu_osc(nes_time_t time) {
        int last_amp = this->mLastAmp;
        this->mLastAmp = 0;
        if (this->mOutput != nullptr && last_amp)
            this->synth.offset(time, -last_amp, this->mOutput);
    }
#endif
};

struct NesEnvelope : NesOsc {
  NesEnvelope() = delete;
  NesEnvelope(NesApu *apu) : NesOsc(apu) {}
  int envelope;
  int env_delay;

  void doEnvelopeClock();
  int volume() const;
  void mReset() {
    this->envelope = 0;
    this->env_delay = 0;
    NesOsc::mReset();
  }
};

// NesSquare
struct NesSquare : NesEnvelope {
  enum { NEGATE_FLAG = 0x08 };
  enum { SHIFT_MASK = 0x07 };
  enum { PHASE_RANGE = 8 };
  int phase;
  int sweep_delay;

  typedef BlipSynth<BLIP_GOOD_QUALITY, 1> Synth;
  Synth const &synth;  // shared between squares

  NesSquare(const Synth *s) : NesEnvelope(nullptr), synth(*s) {}

  void doSweepClock(int adjust);
  void run(nes_time_t, nes_time_t);
  void mReset() {
    this->sweep_delay = 0;
    NesEnvelope::mReset();
  }
  nes_time_t maintain_phase(nes_time_t time, nes_time_t end_time, nes_time_t timer_period);
};

// NesTriangle
struct NesTriangle : NesOsc {
  NesTriangle(NesApu *apu) : NesOsc(apu) {}
  enum { PHASE_RANGE = 16 };
  int phase;
  int linear_counter;
  BlipSynth<BLIP_MED_QUALITY, 1> synth;

  int calc_amp() const;
  void run(nes_time_t, nes_time_t);
  void doLinearCounterClock();
  void mReset() {
    this->linear_counter = 0;
    this->phase = 1;
    NesOsc::mReset();
  }
  nes_time_t maintain_phase(nes_time_t time, nes_time_t end_time, nes_time_t timer_period);
};

// NesNoise
struct NesNoise : NesEnvelope {
  NesNoise(NesApu *apu) : NesEnvelope(apu) {}
  int noise;
  BlipSynth<BLIP_MED_QUALITY, 1> synth;

  void run(nes_time_t, nes_time_t);
  void mReset() {
    noise = 1 << 14;
    NesEnvelope::mReset();
  }

 private:
  uint16_t mGetPeriod() const;
};

// NesDmc
struct NesDmc : NesOsc {
  NesDmc(NesApu *apu) : NesOsc(apu) {}
  int address;  // address of next byte to read
  int period;
  int buf;
  int bits_remain;
  int bits;
  bool buf_full;
  bool silence;

  enum { LOOP_FLAG = 0x40 };

  uint8_t dac;

  nes_time_t m_nextIrq;
  bool irq_enabled;
  bool m_irqFlag;
  bool nonlinear;

  DmcReaderFn prg_reader;  // needs to be initialized to prg read function
  void *prg_reader_data;

  BlipSynth<BLIP_MED_QUALITY, 1> synth;

  void start();
  void writeRegister(int, int);
  void run(nes_time_t, nes_time_t);
  void recalc_irq();
  void fill_buffer();
  void reload_sample();
  void mReset();
  int count_reads(nes_time_t, nes_time_t *) const;
  nes_time_t next_read_time() const;

 private:
  static int sGetDelta(uint8_t dacNew, uint8_t dacOld);
  uint16_t mGetPeriod(uint8_t data) const;
  void mWriteR0(int data);
  void mWriteR1(int data);
};

}  // namespace nes
}  // namespace emu
}  // namespace gme
