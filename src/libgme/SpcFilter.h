// Simple low-pass and high-pass filter to better match sound output of a SNES

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once

#include "blargg_common.h"

namespace gme {
namespace emu {
namespace snes {

struct SpcFilter {
 public:
  // Filters count samples of stereo sound in place. Count must be a multiple of 2.
  typedef int16_t sample_t;
  void Run(sample_t *io, int count);

  // Optional features

  // Clears filter to silence
  void Clear();

  // Sets gain (volume), where GAIN_UNIT is normal. Gains greater than
  // GAIN_UNIT are fine, since output is clamped to 16-bit sample range.
  enum { GAIN_UNIT = 0x100 };
  void SetGain(int gain) { this->m_gain = gain; }

  // Enables/disables filtering (when disabled, gain is still applied)
  void SetEnable(bool enable) { this->m_enabled = enable; }

  // Sets amount of bass (logarithmic scale)
  enum { BASS_NONE = 0 };
  enum { BASS_NORM = 8 };  // normal amount
  enum { BASS_MAX = 31 };
  void SetBass(int bass) { this->m_bass = bass; }

 public:
  SpcFilter();
  BLARGG_DISABLE_NOTHROW
 private:
  enum { GAIN_BITS = 8 };
  int m_gain;
  int m_bass;
  bool m_enabled;
  struct chan_t {
    int p1, pp1, sum;
  };
  chan_t m_ch[2];
};

}  // namespace snes
}  // namespace emu
}  // namespace gme
