// Nes_Snd_Emu 0.1.8. http://www.slack.net/~ant/

#include "NesVrc6Apu.h"

/* Copyright (C) 2003-2006 Shay Green. This module is free software; you
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
namespace nes {

NesVrc6Apu::NesVrc6Apu() {
  output(NULL);
  volume(1.0);
  reset();
}

void NesVrc6Apu::reset() {
  last_time = 0;
  for (int i = 0; i < OSCS_NUM; i++) {
    Vrc6Osc &osc = oscs[i];
    for (int j = 0; j < REGS_NUM; j++)
      osc.mRegs[j] = 0;
    osc.mDelay = 0;
    osc.last_amp = 0;
    osc.phase = 1;
    osc.amp = 0;
  }
}

void NesVrc6Apu::output(BlipBuffer *buf) {
  for (int i = 0; i < OSCS_NUM; i++)
    osc_output(i, buf);
}

void NesVrc6Apu::run_until(blip_time_t time) {
  require(time >= last_time);
  run_square(oscs[0], time);
  run_square(oscs[1], time);
  run_saw(time);
  last_time = time;
}

void NesVrc6Apu::write_osc(blip_time_t time, int osc_index, int reg, int data) {
  require((unsigned) osc_index < OSCS_NUM);
  require((unsigned) reg < REGS_NUM);

  run_until(time);
  oscs[osc_index].mRegs[reg] = data;
}

void NesVrc6Apu::end_frame(blip_time_t time) {
  if (time > last_time)
    run_until(time);

  assert(last_time >= time);
  last_time -= time;
}

void NesVrc6Apu::save_state(vrc6_apu_state_t *out) const {
  assert(sizeof(vrc6_apu_state_t) == 20);
  out->saw_amp = oscs[2].amp;
  for (int i = 0; i < OSCS_NUM; i++) {
    Vrc6Osc const &osc = oscs[i];
    for (int r = 0; r < REGS_NUM; r++)
      out->mRegs[i][r] = osc.mRegs[r];

    out->delays[i] = osc.mDelay;
    out->phases[i] = osc.phase;
  }
}

void NesVrc6Apu::load_state(vrc6_apu_state_t const &in) {
  reset();
  oscs[2].amp = in.saw_amp;
  for (int i = 0; i < OSCS_NUM; i++) {
    Vrc6Osc &osc = oscs[i];
    for (int r = 0; r < REGS_NUM; r++)
      osc.mRegs[r] = in.mRegs[i][r];

    osc.mDelay = in.delays[i];
    osc.phase = in.phases[i];
  }
  if (!oscs[2].phase)
    oscs[2].phase = 1;
}

void NesVrc6Apu::run_square(Vrc6Osc &osc, blip_time_t end_time) {
  BlipBuffer *output = osc.output;
  if (!output)
    return;
  output->SetModified();

  int volume = osc.mRegs[0] & 15;
  if (!(osc.mRegs[2] & 0x80))
    volume = 0;

  int gate = osc.mRegs[0] & 0x80;
  int duty = ((osc.mRegs[0] >> 4) & 7) + 1;
  int delta = ((gate || osc.phase < duty) ? volume : 0) - osc.last_amp;
  blip_time_t time = last_time;
  if (delta) {
    osc.last_amp += delta;
    square_synth.Offset(output, time, delta);
  }

  time += osc.mDelay;
  osc.mDelay = 0;
  int period = osc.period();
  if (volume && !gate && period > 4) {
    if (time < end_time) {
      int phase = osc.phase;

      do {
        phase++;
        if (phase == 16) {
          phase = 0;
          osc.last_amp = volume;
          square_synth.Offset(output, time, volume);
        }
        if (phase == duty) {
          osc.last_amp = 0;
          square_synth.Offset(output, time, -volume);
        }
        time += period;
      } while (time < end_time);

      osc.phase = phase;
    }
    osc.mDelay = time - end_time;
  }
}

void NesVrc6Apu::run_saw(blip_time_t end_time) {
  Vrc6Osc &osc = oscs[2];
  BlipBuffer *output = osc.output;
  if (!output)
    return;
  output->SetModified();

  int amp = osc.amp;
  int amp_step = osc.mRegs[0] & 0x3F;
  blip_time_t time = last_time;
  int last_amp = osc.last_amp;
  if (!(osc.mRegs[2] & 0x80) || !(amp_step | amp)) {
    osc.mDelay = 0;
    int delta = (amp >> 3) - last_amp;
    last_amp = amp >> 3;
    saw_synth.Offset(output, time, delta);
  } else {
    time += osc.mDelay;
    if (time < end_time) {
      int period = osc.period() * 2;
      int phase = osc.phase;

      do {
        if (--phase == 0) {
          phase = 7;
          amp = 0;
        }

        int delta = (amp >> 3) - last_amp;
        if (delta) {
          last_amp = amp >> 3;
          saw_synth.Offset(output, time, delta);
        }

        time += period;
        amp = (amp + amp_step) & 0xFF;
      } while (time < end_time);

      osc.phase = phase;
      osc.amp = amp;
    }

    osc.mDelay = time - end_time;
  }

  osc.last_amp = last_amp;
}

}  // namespace nes
}  // namespace emu
}  // namespace gme
