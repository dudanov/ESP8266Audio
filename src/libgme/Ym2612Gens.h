// YM2612 FM sound chip emulator interface

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once

namespace gme {
namespace emu {
namespace vgm {

struct Ym2612_GENS_Impl;

class Ym2612GensEmu {
  Ym2612_GENS_Impl *m_impl;

 public:
  Ym2612GensEmu() : m_impl(0) {}
  ~Ym2612GensEmu();

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
};

}  // namespace vgm
}  // namespace emu
}  // namespace gme
