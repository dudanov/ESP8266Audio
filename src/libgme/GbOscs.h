// Private oscillators used by GbApu

// Gb_Snd_Emu 0.1.5
#pragma once
#include "BlipBuffer.h"
#include "blargg_common.h"

namespace gme {
namespace emu {
namespace gb {

class GbOsc {
 public:
  static const uint8_t TRIGGER = 0x80;
  static const uint8_t LEN_ENABLED_MASK = 0x40;

  void reset();

  void doLengthClock();

  int getFrequency() const { return (this->m_regs[4] << 8 | this->m_regs[3]) & 0x7FF; }

  void setOutputs(BlipBuffer *center, BlipBuffer *left, BlipBuffer *right) {
    this->m_outputs[CHANNEL_NULL] = nullptr;
    this->m_outputs[CHANNEL_RIGHT] = right;
    this->m_outputs[CHANNEL_LEFT] = left;
    this->m_outputs[CHANNEL_CENTER] = center;
    this->m_output = center;
  }

  virtual void run(blip_time_t, blip_time_t) = 0;
  virtual bool writeRegister(int, int) = 0;

  enum Channel {
    CHANNEL_NULL,
    CHANNEL_RIGHT,
    CHANNEL_LEFT,
    CHANNEL_CENTER,
  };

 protected:
  friend class GbApu;
  std::array<BlipBuffer *, 4> m_outputs;  // NULL, right, left, center
  BlipBuffer *m_output{nullptr};
  uint8_t *m_regs;  // osc's 5 registers
  int m_delay;
  int m_lastAmp;
  int m_volume;
  int m_length;
  bool m_enabled;
};

class GbEnv : public GbOsc {
 public:
  void reset() {
    this->m_envDelay = 0;
    GbOsc::reset();
  }
  void doEnvelopeClock();
  bool writeRegister(int, int) override;

 protected:
  int m_envDelay;
};

class GbSquare : public GbEnv {
 public:
  typedef BlipSynth<BLIP_GOOD_QUALITY, 1> Synth;
  Synth const *synth;

  void reset();
  void doSweepClock();
  void run(blip_time_t, blip_time_t) override;
  void onTrigger() {
    this->m_sweepFrequency = this->getFrequency();
    if (this->m_getShift() && this->m_getSweepPeriod()) {
      this->m_sweepDelay = 1;  // cause sweep to recalculate now
      this->doSweepClock();
    }
  }

 protected:
  int m_sweepFrequency;
  int m_phase;
  uint8_t m_sweepDelay;
  uint8_t m_getSweepPeriod() const { return this->m_regs[0] >> 4 & 0b111; }
  uint8_t m_getShift() const { return this->m_regs[0] & 0b111; }
};

class GbNoise : public GbEnv {
 public:
  typedef BlipSynth<BLIP_MED_QUALITY, 1> Synth;
  Synth const *synth;
  void run(blip_time_t, blip_time_t) override;
  void reset() {
    this->m_reset();
    GbEnv::reset();
  }
  bool writeRegister(int reg, int data) override {
    if (GbEnv::writeRegister(reg, data))
      this->m_lfsr = 0x7FFF;
    return false;
  }

 protected:
  uint16_t m_lfsr;
  void m_reset() { this->m_lfsr = 1; }
};

class GbWave : public GbOsc {
 public:
  GbWave() { this->reset(); }
  typedef BlipSynth<BLIP_MED_QUALITY, 1> Synth;
  Synth const *synth;

  bool writeRegister(int, int) override;
  void run(blip_time_t, blip_time_t) override;
  void writeSamples(unsigned address, uint8_t data) {
    unsigned idx = (address % 16) * 2;
    this->m_waveTable[idx] = data / 16 * 2;
    this->m_waveTable[idx + 1] = data % 16 * 2;
  }
  void reset() {
    this->m_reset();
    GbOsc::reset();
  }

 protected:
  std::array<uint8_t, 32> m_waveTable;
  const uint8_t *m_sampleIt;

  uint8_t m_getSample() const { return *this->m_sampleIt; }
  uint8_t m_advNextSample() {
    if (++this->m_sampleIt == this->m_waveTable.end())
      this->m_reset();
    return this->m_getSample();
  }
  void m_reset() { this->m_sampleIt = this->m_waveTable.begin(); }
};

}  // namespace gb
}  // namespace emu
}  // namespace gme
