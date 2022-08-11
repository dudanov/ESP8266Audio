// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "AyApu.h"

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

// Emulation inaccuracies:
// * Noise isn't run when not in use
// * Changes to envelope and noise periods are delayed until next reload
// * Super-sonic tone should attenuate output to about 60%, not 50%

// Tones above this frequency are treated as disabled tone at half volume.
// Power of two is more efficient (avoids division).

namespace gme {
namespace emu {
namespace ay {

static const unsigned INAUDIBLE_FREQ = 16384;
static const int PERIOD_FACTOR = 16;

static const uint8_t AMP_TABLE[16] = {
#define ENTRY(v) static_cast<uint8_t>(AyApu::AMP_RANGE * (v) + 0.5)
    // With channels tied together and 1K resistor to ground (as datasheet
    // recommends), output nearly matches logarithmic curve as claimed.
    // Approx. 1.5 dB per step.
    ENTRY(0.000000), ENTRY(0.007813), ENTRY(0.011049), ENTRY(0.015625), ENTRY(0.022097), ENTRY(0.031250),
    ENTRY(0.044194), ENTRY(0.062500), ENTRY(0.088388), ENTRY(0.125000), ENTRY(0.176777), ENTRY(0.250000),
    ENTRY(0.353553), ENTRY(0.500000), ENTRY(0.707107), ENTRY(1.000000),
#undef ENTRY
};

static const uint8_t MODES[8] = {
#define MODE(a0, a1, b0, b1, c0, c1) (a0 << 0 | a1 << 1 | b0 << 2 | b1 << 3 | c0 << 4 | c1 << 5)
    MODE(1, 0, 1, 0, 1, 0), MODE(1, 0, 0, 0, 0, 0), MODE(1, 0, 0, 1, 1, 0), MODE(1, 0, 1, 1, 1, 1),
    MODE(0, 1, 0, 1, 0, 1), MODE(0, 1, 1, 1, 1, 1), MODE(0, 1, 1, 0, 0, 1), MODE(0, 1, 0, 0, 0, 0),
#undef MODE
};

AyApu::Envelope::Envelope() {
  // build full table of the upper 8 envelope waveforms
  for (auto m = 8; m--;) {
    auto it = this->modes[m];
    auto flags = MODES[m];
    for (auto x = 0; x < 3; x++) {
      int amp = (flags & 0b01);
      int e = (flags & 0b10) >> 1;
      int step = e - amp;
      if (amp)
        amp = 15;
      for (auto end = it + 16; it != end; amp += step)
        *it++ = AMP_TABLE[amp];
      flags >>= 2;
    }
  }
}

AyApu::AyApu() {
  this->setOutput(nullptr);
  this->setVolume(1.0);
  this->reset();
}

void AyApu::reset() {
  this->m_lastTime = 0;
  this->m_noise.delay = 0;
  this->m_noise.lfsr = 1;
  for (auto &osc : this->m_square) {
    osc.period = PERIOD_FACTOR;
    osc.delay = 0;
    osc.lastAmp = 0;
    osc.phase = 0;
  }
  for (int i = sizeof(this->m_regs); --i >= 0;)
    this->m_regs[i] = 0;
  this->m_regs[7] = 0xFF;
  this->m_writeData(13, 0);
}

void AyApu::m_writeData(int addr, int data) {
  assert((unsigned) addr < REG_COUNT);

  if ((unsigned) addr >= 14) {
#ifdef debug_printf
    debug_printf("Wrote to I/O port %02X\n", (int) addr);
#endif
  }

  // envelope mode
  if (addr == 13) {
    if (!(data & 8))  // convert modes 0-7 to proper equivalents
      data = (data & 4) ? 15 : 9;
    this->m_envelope.wave = this->m_envelope.modes[data - 7];
    this->m_envelope.pos = -48;
    this->m_envelope.delay = 0;  // will get set to envelope period in m_runUntil()
  }
  this->m_regs[addr] = data;

  // handle period changes accurately
  int i = addr >> 1;
  if (i < OSCS_NUM) {
    blip_time_t period =
        (this->m_regs[i * 2 + 1] & 0x0F) * (0x100L * PERIOD_FACTOR) + this->m_regs[i * 2] * PERIOD_FACTOR;
    if (!period)
      period = PERIOD_FACTOR;

    // adjust time of next timer expiration based on change in period
    Square &osc = this->m_square[i];
    if ((osc.delay += period - osc.period) < 0)
      osc.delay = 0;
    osc.period = period;
  }

  // TODO: same as above for envelope timer, and it also has a divide by two
  // after it
}

int const NOISE_OFF = 0x08;
int const TONE_OFF = 0x01;

void AyApu::m_runUntil(blip_time_t final_end_time) {
  require(final_end_time >= this->m_lastTime);

  // noise period and initial values
  blip_time_t const noise_period_factor = PERIOD_FACTOR * 2;  // verified
  blip_time_t noise_period = (this->m_regs[6] & 0x1F) * noise_period_factor;
  if (!noise_period)
    noise_period = noise_period_factor;
  blip_time_t const old_noise_delay = this->m_noise.delay;
  blargg_ulong const old_noise_lfsr = this->m_noise.lfsr;

  // envelope period
  blip_time_t const env_period_factor = PERIOD_FACTOR * 2;  // verified
  blip_time_t env_period = (this->m_regs[12] * 0x100L + this->m_regs[11]) * env_period_factor;
  if (!env_period)
    env_period = env_period_factor;  // same as period 1 on my AY chip
  if (!m_envelope.delay)
    this->m_envelope.delay = env_period;

  // run each osc separately
  for (int idx = 0; idx < OSCS_NUM; idx++) {
    Square *const osc = &this->m_square[idx];
    int osc_mode = this->m_regs[7] >> idx;

    // output
    BlipBuffer *const osc_output = osc->output;
    if (!osc_output)
      continue;
    osc_output->setModified();

    // period
    int half_vol = 0;
    blip_time_t inaudible_period = (blargg_ulong) (osc_output->getClockRate() + INAUDIBLE_FREQ) / (INAUDIBLE_FREQ * 2);
    if (osc->period <= inaudible_period && !(osc_mode & TONE_OFF)) {
      half_vol = 1;  // Actually around 60%, but 50% is close enough
      osc_mode |= TONE_OFF;
    }

    // envelope
    blip_time_t start_time = this->m_lastTime;
    blip_time_t end_time = final_end_time;
    const int vol_mode = this->m_regs[idx + 8];
    int volume = AMP_TABLE[vol_mode & 0x0F] >> half_vol;
    int osc_env_pos = this->m_envelope.pos;
    if (vol_mode & 0x10) {
      volume = this->m_envelope.wave[osc_env_pos] >> half_vol;
      // use envelope only if it's a repeating wave or a ramp that hasn't
      // finished
      if (!(this->m_regs[13] & 1) || osc_env_pos < -32) {
        end_time = start_time + this->m_envelope.delay;
        if (end_time >= final_end_time)
          end_time = final_end_time;

        // if ( !(this->m_regs [12] | this->m_regs [11]) )
        //  debug_printf( "Used envelope period 0\n" );
      } else if (!volume) {
        osc_mode = NOISE_OFF | TONE_OFF;
      }
    } else if (!volume) {
      osc_mode = NOISE_OFF | TONE_OFF;
    }

    // tone time
    const blip_time_t period = osc->period;
    blip_time_t time = start_time + osc->delay;
    // maintain tone's phase when off
    if (osc_mode & TONE_OFF) {
      blargg_long count = (final_end_time - time + period - 1) / period;
      time += count * period;
      osc->phase ^= count & 1;
    }

    // noise time
    blip_time_t ntime = final_end_time;
    blargg_ulong noise_lfsr = 1;
    if (!(osc_mode & NOISE_OFF)) {
      ntime = start_time + old_noise_delay;
      noise_lfsr = old_noise_lfsr;
      // if ( (this->m_regs [6] & 0x1F) == 0 )
      //  debug_printf( "Used noise period 0\n" );
    }

    // The following efficiently handles several cases (least demanding
    // first):
    // * Tone, noise, and envelope disabled, where channel acts as 4-bit DAC
    // * Just tone or just noise, envelope disabled
    // * Envelope controlling tone and/or noise
    // * Tone and noise disabled, envelope enabled with high frequency
    // * Tone and noise together
    // * Tone and noise together with envelope

    // This loop only runs one iteration if envelope is disabled. If
    // envelope is being used as a waveform (tone and noise disabled), this
    // loop will still be reasonably efficient since the bulk of it will be
    // skipped.
    while (1) {
      // current amplitude
      int amp = 0;
      if ((osc_mode | osc->phase) & 1 & (osc_mode >> 3 | noise_lfsr))
        amp = volume;
      {
        int delta = amp - osc->lastAmp;
        if (delta) {
          osc->lastAmp = amp;
          this->m_synth.offset(start_time, delta, osc_output);
        }
      }

      // Run wave and noise interleved with each catching up to the other.
      // If one or both are disabled, their "current time" will be past
      // end time, so there will be no significant performance hit.
      if (ntime < end_time || time < end_time) {
        // Since amplitude was updated above, delta will always be +/-
        // volume, so we can avoid using lastAmp every time to
        // calculate the delta.
        int delta = amp * 2 - volume;
        int delta_non_zero = delta != 0;
        int phase = osc->phase | (osc_mode & TONE_OFF);
        assert(TONE_OFF == 0x01);
        do {
          // run noise
          blip_time_t end = end_time;
          if (end_time > time)
            end = time;
          if (phase & delta_non_zero) {
            while (ntime <= end)  // must advance *past* time to avoid hang
            {
              int changed = noise_lfsr + 1;
              noise_lfsr = (-(noise_lfsr & 1) & 0x12000) ^ (noise_lfsr >> 1);
              if (changed & 2) {
                delta = -delta;
                this->m_synth.offset(ntime, delta, osc_output);
              }
              ntime += noise_period;
            }
          } else {
            // 20 or more noise periods on average for some music
            blargg_long remain = end - ntime;
            blargg_long count = remain / noise_period;
            if (remain >= 0)
              ntime += noise_period + count * noise_period;
          }

          // run tone
          end = end_time;
          if (end_time > ntime)
            end = ntime;
          if (noise_lfsr & delta_non_zero) {
            while (time < end) {
              delta = -delta;
              this->m_synth.offset(time, delta, osc_output);
              time += period;
              // phase ^= 1;
            }
            // assert( phase == (delta > 0) );
            phase = unsigned(-delta) >> (CHAR_BIT * sizeof(unsigned) - 1);
            // (delta > 0)
          } else {
            // loop usually runs less than once
            // SUB_CASE_COUNTER( (time < end) * (end - time + period
            // - 1) / period );

            while (time < end) {
              time += period;
              phase ^= 1;
            }
          }
        } while (time < end_time || ntime < end_time);

        osc->lastAmp = (delta + volume) >> 1;
        if (!(osc_mode & TONE_OFF))
          osc->phase = phase;
      }

      if (end_time >= final_end_time)
        break;  // breaks first time when envelope is disabled

      // next envelope step
      if (++osc_env_pos >= 0)
        osc_env_pos -= 32;
      volume = this->m_envelope.wave[osc_env_pos] >> half_vol;

      start_time = end_time;
      end_time += env_period;
      if (end_time > final_end_time)
        end_time = final_end_time;
    }
    osc->delay = time - final_end_time;

    if (!(osc_mode & NOISE_OFF)) {
      this->m_noise.delay = ntime - final_end_time;
      this->m_noise.lfsr = noise_lfsr;
    }
  }

  // TODO: optimized saw wave envelope?

  // maintain envelope phase
  blip_time_t remain = final_end_time - this->m_lastTime - this->m_envelope.delay;
  if (remain >= 0) {
    blargg_long count = (remain + env_period) / env_period;
    this->m_envelope.pos += count;
    if (this->m_envelope.pos >= 0)
      this->m_envelope.pos = (this->m_envelope.pos & 31) - 32;
    remain -= count * env_period;
    assert(-remain <= env_period);
  }
  this->m_envelope.delay = -remain;
  assert(this->m_envelope.delay > 0);
  assert(this->m_envelope.pos < 0);

  this->m_lastTime = final_end_time;
}

}  // namespace ay
}  // namespace emu
}  // namespace gme
