#pragma once

#include "SmsOscs.h"

// Sega Master System SN76489 PSG sound chip emulator

namespace gme {
namespace emu {
namespace sms {

class SmsApu {
 public:
  // Set overall volume of all oscillators, where 1.0 is full volume
  void setVolume(double);

  // Set treble equalization
  void setTrebleEq(const BlipEq &);

  // Outputs can be assigned to a single buffer for mono output, or to three
  // buffers for stereo output (using StereoBuffer to do the mixing).

  // Assign all oscillator outputs to specified buffer(s). If buffer
  // is NULL, silences all oscillators.
  void SetOutput(BlipBuffer *mono) { this->SetOutput(mono, mono, mono); }
  void SetOutput(BlipBuffer *center, BlipBuffer *left, BlipBuffer *right);

  // Assign single oscillator output to buffer(s). Valid indicies are 0 to 3,
  // which refer to Square 1, Square 2, Square 3, and Noise. If buffer is
  // NULL, silences oscillator.
  enum { OSCS_NUM = 4 };
  void setOscOutput(int index, BlipBuffer *mono) { this->setOscOutput(index, mono, mono, mono); }
  void setOscOutput(int index, BlipBuffer *center, BlipBuffer *left, BlipBuffer *right);

  // Reset oscillators and internal state
  void reset(unsigned noise_feedback = 0, int noise_width = 0);

  // Write GameGear left/right assignment byte
  void writeGGStereo(blip_time_t, int);

  // Write to data port
  void writeData(blip_time_t, int);

  // Run all oscillators up to specified time, end current frame, then
  // start a new frame at time 0.
  void EndFrame(blip_time_t);

 public:
  SmsApu();
  ~SmsApu();

 private:
  // noncopyable
  SmsApu(const SmsApu &) = delete;
  SmsApu &operator=(const SmsApu &) = delete;

  std::array<SmsOsc *, OSCS_NUM> m_oscs;
  std::array<SmsSquare, 3> m_squares;
  SmsNoise m_noise;
  SmsSquare::Synth m_squareSynth;  // used by squares
  blip_time_t m_lastTime;
  int m_latch;
  unsigned m_noiseFeedback;
  unsigned m_loopedFeedback;

  void mRunUntil(blip_time_t);
};

// struct SmsApuState {
// uint8_t mRegs[8][2];
// uint8_t latch;
//};

}  // namespace sms
}  // namespace emu
}  // namespace gme
