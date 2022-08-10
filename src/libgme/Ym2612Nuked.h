// YM2612 FM sound chip emulator interface

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once

namespace gme {
namespace emu {
namespace vgm {

typedef void Ym2612_Nuked_Impl;

class Ym2612NukedEmu {
 public:
  Ym2612NukedEmu();
  ~Ym2612NukedEmu();

  // Set output sample rate and chip clock rates, in Hz. Returns non-zero
  // if error.
  const char *set_rate(double sample_rate, double clock_rate);

  // Reset to power-up state
  void reset();

  // Mute voice n if bit n (1 << n) of mask is set
  enum { CHANNELS_NUM = 6 };
  void mute_voices(int mask);

  // Write addr to register 0 then data to register 1
  void write0(int addr, int data);

  // Write addr to register 2 then data to register 3
  void write1(int addr, int data);

  // Run and add pair_count samples into current output buffer contents
  typedef short sample_t;
  enum { OUT_CHANNELS_NUM = 2 };  // stereo
  void run(int pair_count, sample_t *out);

 private:
  Ym2612_Nuked_Impl *m_impl;
  double m_prevSampleRate;
  double m_prevClockRate;
};

}  // namespace vgm
}  // namespace emu
}  // namespace gme
