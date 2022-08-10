// Nintendo Game Boy PAPU sound chip emulator

// Gb_Snd_Emu 0.1.5
#pragma once

#include "GbOscs.h"

namespace gme {
namespace emu {
namespace gb {

class GbApu {
 public:
  GbApu();
  // Set overall volume of all oscillators, where 1.0 is full volume
  void setVolume(double vol) {
    m_volumeUnit = 0.60 / OSC_NUM / 15 /*steps*/ / 2 /*?*/ / 8 /*master vol range*/ * vol;
    m_updateVolume();
  }

  // Set treble equalization
  void setTrebleEq(const BlipEq &);

  // Outputs can be assigned to a single buffer for mono output, or to three
  // buffers for stereo output (using StereoBuffer to do the mixing).

  // Assign all oscillator outputs to specified buffer(s). If buffer
  // is NULL, silences all oscillators.
  void setOutput(BlipBuffer *b) { setOutput(b, b, b); }
  void setOutput(BlipBuffer *center, BlipBuffer *left, BlipBuffer *right);

  // Assign single oscillator output to buffer(s). Valid indicies are 0 to 3,
  // which refer to Square 1, Square 2, Wave, and Noise. If buffer is NULL,
  // silences oscillator.
  enum { OSC_NUM = 4 };
  void setOscOutput(int i, BlipBuffer *b) { setOscOutput(i, b, b, b); }
  void setOscOutput(int index, BlipBuffer *center, BlipBuffer *left, BlipBuffer *right);

  // Reset oscillators and internal state
  void reset();

  // Reads and writes at addr must satisfy START_ADDR <= addr <= END_ADDR
  static const unsigned START_ADDR = 0xFF10;
  static const unsigned END_ADDR = 0xFF3F;
  static constexpr unsigned REGS_NUM = END_ADDR - START_ADDR + 1;

  // Write 'data' to address at specified time
  void writeRegister(blip_time_t, unsigned addr, uint8_t data);

  // Read from address at specified time
  uint8_t readRegister(blip_time_t, unsigned addr);

  // Run all oscillators up to specified time, end current time frame, then
  // start a new frame at time 0.
  void endFrame(blip_time_t);

  void setTempo(double);

  enum Register : uint16_t {
    NR10 = 0xFF10,
    NR11 = 0xFF11,
    NR12 = 0xFF12,
    NR13 = 0xFF13,
    NR14 = 0xFF14,
    NR21 = 0xFF16,
    NR22 = 0xFF17,
    NR23 = 0xFF18,
    NR24 = 0xFF19,
    NR30 = 0xFF1A,
    NR31 = 0xFF1B,
    NR32 = 0xFF1C,
    NR33 = 0xFF1D,
    NR34 = 0xFF1E,
    NR41 = 0xFF20,
    NR42 = 0xFF21,
    NR43 = 0xFF22,
    NR44 = 0xFF23,
    NR50 = 0xFF24,
    NR51 = 0xFF25,
    NR52 = 0xFF26,
  };

 private:
  // noncopyable
  GbApu(const GbApu &) = delete;
  GbApu &operator=(const GbApu &) = delete;

  std::array<GbOsc *, OSC_NUM> m_oscs;
  blip_time_t m_nextFrameTime;
  blip_time_t m_lastTime;
  blip_time_t m_framePeriod;
  double m_volumeUnit;
  int m_frameCounter;

  GbSquare m_square1;
  GbSquare m_square2;
  GbWave m_wave;
  GbNoise m_noise;
  std::array<uint8_t, REGS_NUM> m_regs;
  GbSquare::Synth m_squareSynth;  // used by squares
  GbWave::Synth m_otherSynth;     // used by wave and noise

  void m_updateVolume();
  void m_runUntil(blip_time_t);
  void m_writeOsc(unsigned index, unsigned reg, uint8_t data);
  void m_setRegister(unsigned address, uint8_t data) { this->m_regs[address - GbApu::START_ADDR] = data; }
  uint8_t m_getRegister(unsigned address) const { return this->m_regs[address - GbApu::START_ADDR]; }
  void m_writeNR50(blip_time_t time);
  void m_writeNR5152(blip_time_t time);
};

}  // namespace gb
}  // namespace emu
}  // namespace gme
