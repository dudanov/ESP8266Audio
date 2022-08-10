// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "SpcFilter.h"

#include <string.h>

/* Copyright (C) 2007 Shay Green. This module is free software; you
can redistribute it and/or modify it under the terms of the GNU Lesser
General Public License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version. This
module is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
details. You should have received a copy of the GNU Lesser General Public
License along with this module; if not, write to the Free Software Foundation,
Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA */

#include "blargg_source.h"

namespace gme {
namespace emu {
namespace snes {

void SpcFilter::Clear() { memset(this->m_ch, 0, sizeof(this->m_ch)); }

SpcFilter::SpcFilter() {
  this->m_enabled = true;
  this->m_gain = GAIN_UNIT;
  this->m_bass = BASS_NORM;
  this->Clear();
}

void SpcFilter::Run(sample_t *io, int count) {
  require((count & 1) == 0);  // must be even

  if (this->m_enabled) {
    int const bass = this->m_bass;
    chan_t *c = &m_ch[2];
    do {
      // cache in registers
      int sum = (--c)->sum;
      int pp1 = c->pp1;
      int p1 = c->p1;

      for (int i = 0; i < count; i += 2) {
        // Low-pass filter (two point FIR with coeffs 0.25, 0.75)
        int f = io[i] + p1;
        p1 = io[i] * 3;

        // High-pass filter ("leaky integrator")
        int delta = f - pp1;
        pp1 = f;
        int s = sum >> (GAIN_BITS + 2);
        sum += (delta * this->m_gain) - (sum >> bass);

        // Clamp to 16 bits
        if ((short) s != s)
          s = (s >> 31) ^ 0x7FFF;

        io[i] = (short) s;
      }

      c->p1 = p1;
      c->pp1 = pp1;
      c->sum = sum;
      ++io;
    } while (c != m_ch);
  } else if (this->m_gain != GAIN_UNIT) {
    short *const end = io + count;
    while (io < end) {
      int s = (*io * this->m_gain) >> GAIN_BITS;
      if ((short) s != s)
        s = (s >> 31) ^ 0x7FFF;
      *io++ = (short) s;
    }
  }
}

}  // namespace snes
}  // namespace emu
}  // namespace gme
