// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "KssSccApu.h"

/* Copyright (C) 2006 Shay Green. This module is free software; you
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
namespace kss {

// Tones above this frequency are treated as disabled tone at half volume.
// Power of two is more efficient (avoids division).
unsigned const INAUDIBLE_FREQ = 16384;

int const wave_size = 0x20;

void SccApu::run_until(blip_time_t end_time) {
  for (int index = 0; index < OSCS_NUM; index++) {
    osc_t &osc = m_oscs[index];

    BlipBuffer *const output = osc.output;
    if (!output)
      continue;
    output->SetModified();

    blip_time_t period = (m_regs[0x80 + index * 2 + 1] & 0x0F) * 0x100 + m_regs[0x80 + index * 2] + 1;
    int volume = 0;
    if (m_regs[0x8F] & (1 << index)) {
      blip_time_t inaudible_period =
          (blargg_ulong)(output->GetClockRate() + INAUDIBLE_FREQ * 32) / (INAUDIBLE_FREQ * 16);
      if (period > inaudible_period)
        volume = (m_regs[0x8A + index] & 0x0F) * (AMP_RANGE / 256 / 15);
    }

    int8_t const *wave = (int8_t *) m_regs + index * wave_size;
    if (index == OSCS_NUM - 1)
      wave -= wave_size;  // last two oscs share wave
    {
      int amp = wave[osc.phase] * volume;
      int delta = amp - osc.mLastAmp;
      if (delta) {
        osc.mLastAmp = amp;
        m_synth.Offset(output, m_lastTime, delta);
      }
    }

    blip_time_t time = m_lastTime + osc.mDelay;
    if (time < end_time) {
      if (!volume) {
        // maintain phase
        blargg_long count = (end_time - time + period - 1) / period;
        osc.phase = (osc.phase + count) & (wave_size - 1);
        time += count * period;
      } else {
        int phase = osc.phase;
        int last_wave = wave[phase];
        phase = (phase + 1) & (wave_size - 1);  // pre-advance for optimal inner loop

        do {
          int amp = wave[phase];
          phase = (phase + 1) & (wave_size - 1);
          int delta = amp - last_wave;
          if (delta) {
            last_wave = amp;
            m_synth.Offset(output, time, delta * volume);
          }
          time += period;
        } while (time < end_time);

        osc.phase = phase = (phase - 1) & (wave_size - 1);  // undo pre-advance
        osc.mLastAmp = wave[phase] * volume;
      }
    }
    osc.mDelay = time - end_time;
  }
  m_lastTime = end_time;
}

}  // namespace kss
}  // namespace emu
}  // namespace gme
