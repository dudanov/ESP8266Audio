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
  BlipBuffer *m_output;
  int lengthCounter;  // length counter (0 if unused by oscillator)
  int delay;          // delay until next (potential) transition
  int lastAmp;        // last amplitude oscillator was outputting
  std::array<uint8_t, 4> regs;
  std::array<bool, 4> regWritten;

  void setOutput(BlipBuffer *output) { this->m_output = output; }
  void doLengthClock(int halt_mask);
  int getPeriod() const { return 256 * (this->regs[3] & 0b111) + this->regs[2]; }

 protected:
  NesOsc() = delete;
  NesOsc(NesApu *apu) : m_apu(apu) {}
  NesApu *m_apu;
  void m_reset() {
    this->delay = 0;
    this->lastAmp = 0;
  }
  int m_updateAmp(int amp) {
    int delta = amp - this->lastAmp;
    this->lastAmp = amp;
    return delta;
  }
#if 0
    void zero_apu_osc(nes_time_t time) {
        int last_amp = this->lastAmp;
        this->lastAmp = 0;
        if (this->m_output != nullptr && last_amp)
            this->synth.offset(time, -last_amp, this->m_output);
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
  void reset() {
    this->envelope = 0;
    this->env_delay = 0;
    NesOsc::m_reset();
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
  void reset() {
    this->sweep_delay = 0;
    NesEnvelope::reset();
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
  void reset() {
    this->linear_counter = 0;
    this->phase = 1;
    NesOsc::m_reset();
  }
  nes_time_t maintain_phase(nes_time_t time, nes_time_t end_time, nes_time_t timer_period);
};

// NesNoise
struct NesNoise : NesEnvelope {
  NesNoise(NesApu *apu) : NesEnvelope(apu) {}
  int noise;
  BlipSynth<BLIP_MED_QUALITY, 1> synth;

  void run(nes_time_t, nes_time_t);
  void reset() {
    noise = 1 << 14;
    NesEnvelope::reset();
  }
};

// NesDmc
struct NesDmc : NesOsc {
  NesDmc(NesApu *apu) : NesOsc(apu) {}
  int address;  // address of next byte to read
  int period;
  // int lengthCounter; // bytes remaining to play (already defined in
  // NesOsc)
  int buf;
  int bits_remain;
  int bits;
  bool buf_full;
  bool silence;

  enum { LOOP_FLAG = 0x40 };

  int dac;

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
  void reset();
  int count_reads(nes_time_t, nes_time_t *) const;
  nes_time_t next_read_time() const;
};

}  // namespace nes
}  // namespace emu
}  // namespace gme
