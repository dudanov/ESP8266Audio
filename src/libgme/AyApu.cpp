// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "AyApu.h"
#include <pgmspace.h>

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

// Full table of the upper 8 envelope waveforms. Values already passed through volume table.
static const uint8_t MODES[8][48] PROGMEM = {
    {0xFF, 0xB4, 0x80, 0x5A, 0x40, 0x2D, 0x20, 0x17, 0x10, 0x0B, 0x08, 0x06, 0x04, 0x03, 0x02, 0x00,
     0xFF, 0xB4, 0x80, 0x5A, 0x40, 0x2D, 0x20, 0x17, 0x10, 0x0B, 0x08, 0x06, 0x04, 0x03, 0x02, 0x00,
     0xFF, 0xB4, 0x80, 0x5A, 0x40, 0x2D, 0x20, 0x17, 0x10, 0x0B, 0x08, 0x06, 0x04, 0x03, 0x02, 0x00},
    {0xFF, 0xB4, 0x80, 0x5A, 0x40, 0x2D, 0x20, 0x17, 0x10, 0x0B, 0x08, 0x06, 0x04, 0x03, 0x02, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xFF, 0xB4, 0x80, 0x5A, 0x40, 0x2D, 0x20, 0x17, 0x10, 0x0B, 0x08, 0x06, 0x04, 0x03, 0x02, 0x00,
     0x00, 0x02, 0x03, 0x04, 0x06, 0x08, 0x0B, 0x10, 0x17, 0x20, 0x2D, 0x40, 0x5A, 0x80, 0xB4, 0xFF,
     0xFF, 0xB4, 0x80, 0x5A, 0x40, 0x2D, 0x20, 0x17, 0x10, 0x0B, 0x08, 0x06, 0x04, 0x03, 0x02, 0x00},
    {0xFF, 0xB4, 0x80, 0x5A, 0x40, 0x2D, 0x20, 0x17, 0x10, 0x0B, 0x08, 0x06, 0x04, 0x03, 0x02, 0x00,
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0x00, 0x02, 0x03, 0x04, 0x06, 0x08, 0x0B, 0x10, 0x17, 0x20, 0x2D, 0x40, 0x5A, 0x80, 0xB4, 0xFF,
     0x00, 0x02, 0x03, 0x04, 0x06, 0x08, 0x0B, 0x10, 0x17, 0x20, 0x2D, 0x40, 0x5A, 0x80, 0xB4, 0xFF,
     0x00, 0x02, 0x03, 0x04, 0x06, 0x08, 0x0B, 0x10, 0x17, 0x20, 0x2D, 0x40, 0x5A, 0x80, 0xB4, 0xFF},
    {0x00, 0x02, 0x03, 0x04, 0x06, 0x08, 0x0B, 0x10, 0x17, 0x20, 0x2D, 0x40, 0x5A, 0x80, 0xB4, 0xFF,
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0x00, 0x02, 0x03, 0x04, 0x06, 0x08, 0x0B, 0x10, 0x17, 0x20, 0x2D, 0x40, 0x5A, 0x80, 0xB4, 0xFF,
     0xFF, 0xB4, 0x80, 0x5A, 0x40, 0x2D, 0x20, 0x17, 0x10, 0x0B, 0x08, 0x06, 0x04, 0x03, 0x02, 0x00,
     0x00, 0x02, 0x03, 0x04, 0x06, 0x08, 0x0B, 0x10, 0x17, 0x20, 0x2D, 0x40, 0x5A, 0x80, 0xB4, 0xFF},
    {0x00, 0x02, 0x03, 0x04, 0x06, 0x08, 0x0B, 0x10, 0x17, 0x20, 0x2D, 0x40, 0x5A, 0x80, 0xB4, 0xFF,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

// With channels tied together and 1K resistor to ground (as datasheet recommends),
// output nearly matches logarithmic curve as claimed. Approx. 1.5 dB per step.
inline uint8_t AyApu::sGetAmp(size_t volume) { return pgm_read_byte(&MODES[5][volume]); }

AyApu::AyApu() {
  SetOutput(nullptr);
  SetVolume(1.0);
  Reset();
}

void AyApu::Reset() {
  mLastTime = 0;
  mNoise.mDelay = 0;
  mNoise.mLfsr = 0b1;
  for (auto &osc : mSquare) {
    osc.mPeriod = CLOCK_PSC;
    osc.mDelay = 0;
    osc.mLastAmp = 0;
    osc.mPhase = 0;
  }
  mRegs.fill(0x00);
  mWriteRegister(R7, 0xFF);
  mWriteRegister(R13, 0x00);
}

void AyApu::mWriteRegister(unsigned addr, uint8_t data) {
  assert(addr < RNUM);

  mRegs[R0 + addr] = data;

  // handle period changes accurately
  if (addr <= R5)
    return mPeriodUpdate(addr / 2);

  // TODO: same as above for envelope timer, and it also has a divide by two after it

  // envelope mode
  if (addr == R13)
    return mEnvelope.SetMode(data & 0b1111);
}

inline void AyApu::mPeriodUpdate(unsigned channel) {
  blip_time_t period = get_le16(&mRegs[R0 + channel * 2]) % 4096 * CLOCK_PSC;
  if (!period)
    period = CLOCK_PSC;

  // adjust time of next timer expiration based on change in period
  Square &osc = mSquare[channel];
  if ((osc.mDelay += period - osc.mPeriod) < 0)
    osc.mDelay = 0;
  osc.mPeriod = period;
}

inline void AyApu::Envelope::SetMode(uint8_t mode) {
  if (!(mode & CONTINUE))  // convert modes 0-7 to proper equivalents
    mode = (mode & ATTACK) ? 15 : 9;
  mWave = MODES[mode - 7];
  mPos = -48;
  mDelay = 0;  // will get set to envelope period in mRunUntil()
}

static const unsigned NOISE_OFF = 0b1000;
static const unsigned TONE_OFF = 0b0001;

void AyApu::mRunUntil(blip_time_t final_end_time) {
  require(final_end_time >= mLastTime);

  // noise period and initial values
  const blip_time_t NOISE_PSC = CLOCK_PSC * 2;  // verified
  blip_time_t noise_period = (mRegs[R6] & 0b11111) * NOISE_PSC;
  if (!noise_period)
    noise_period = NOISE_PSC;
  blip_time_t const old_noise_delay = mNoise.mDelay;
  blargg_ulong const old_noise_lfsr = mNoise.mLfsr;

  // envelope period
  const blip_time_t ENVELOPE_PSC = CLOCK_PSC * 2;  // verified
  blip_time_t env_period = get_le16(&mRegs[R11]) * ENVELOPE_PSC;
  if (!env_period)
    env_period = ENVELOPE_PSC;  // same as period 1 on my AY chip
  if (!mEnvelope.mDelay)
    mEnvelope.mDelay = env_period;

  // run each osc separately
  for (int idx = 0; idx < OSCS_NUM; idx++) {
    Square &osc = mSquare[idx];
    uint8_t mode = mRegs[R7] >> idx;

    // output
    BlipBuffer *const osc_output = osc.mOutput;
    if (osc_output == nullptr)
      continue;
    osc_output->setModified();

    // period
    bool half_vol = false;
    blip_time_t inaudible_period = (blargg_ulong)(osc_output->GetClockRate() + INAUDIBLE_FREQ) / (INAUDIBLE_FREQ * 2);
    if (osc.mPeriod <= inaudible_period && !(mode & TONE_OFF)) {
      half_vol = true;  // Actually around 60%, but 50% is close enough
      mode |= TONE_OFF;
    }

    // envelope
    blip_time_t start_time = mLastTime;
    blip_time_t end_time = final_end_time;
    const uint8_t amp_ctrl = mRegs[R8 + idx];
    int volume = AyApu::sGetAmp(amp_ctrl & 0b1111) >> half_vol;
    int osc_env_pos = mEnvelope.mPos;
    if (amp_ctrl & 0x10) {
      volume = pgm_read_byte(&mEnvelope.mWave[osc_env_pos]) >> half_vol;
      // use envelope only if it's a repeating wave or a ramp that hasn't
      // finished
      if (!(mRegs[R13] & 1) || osc_env_pos < -32) {
        end_time = start_time + mEnvelope.mDelay;
        if (end_time >= final_end_time)
          end_time = final_end_time;

        // if ( !(mRegs [12] | mRegs [11]) )
        //  debug_printf( "Used envelope period 0\n" );
      } else if (!volume) {
        mode = NOISE_OFF | TONE_OFF;
      }
    } else if (!volume) {
      mode = NOISE_OFF | TONE_OFF;
    }

    // tone time
    const blip_time_t period = osc.mPeriod;
    blip_time_t time = start_time + osc.mDelay;
    // maintain tone's phase when off
    if (mode & TONE_OFF) {
      blargg_long count = (final_end_time - time + period - 1) / period;
      time += count * period;
      osc.mPhase ^= count & 1;
    }

    // noise time
    blip_time_t ntime = final_end_time;
    blargg_ulong noise_lfsr = 1;
    if (!(mode & NOISE_OFF)) {
      ntime = start_time + old_noise_delay;
      noise_lfsr = old_noise_lfsr;
      // if ( (mRegs [6] & 0x1F) == 0 )
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
      if ((mode | osc.mPhase) & 1 & (mode >> 3 | noise_lfsr))
        amp = volume;
      {
        int delta = amp - osc.mLastAmp;
        if (delta) {
          osc.mLastAmp = amp;
          mSynth.offset(start_time, delta, osc_output);
        }
      }

      // Run wave and noise interleved with each catching up to the other.
      // If one or both are disabled, their "current time" will be past
      // end time, so there will be no significant performance hit.
      if (ntime < end_time || time < end_time) {
        // Since amplitude was updated above, delta will always be +/-
        // volume, so we can avoid using mLastAmp every time to
        // calculate the delta.
        int delta = amp * 2 - volume;
        int delta_non_zero = delta != 0;
        int phase = osc.mPhase | (mode & TONE_OFF);
        assert(TONE_OFF == 0x01);
        do {
          // run noise
          blip_time_t end = end_time;
          if (end_time > time)
            end = time;
          if (phase & delta_non_zero) {
            // must advance *past* time to avoid hang
            while (ntime <= end) {
              int changed = noise_lfsr + 1;
              noise_lfsr = (-(noise_lfsr & 1) & 0x12000) ^ (noise_lfsr >> 1);
              if (changed & 2) {
                delta = -delta;
                mSynth.offset(ntime, delta, osc_output);
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
              mSynth.offset(time, delta, osc_output);
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

        osc.mLastAmp = (delta + volume) >> 1;
        if (!(mode & TONE_OFF))
          osc.mPhase = phase;
      }

      if (end_time >= final_end_time)
        break;  // breaks first time when envelope is disabled

      // next envelope step
      if (++osc_env_pos >= 0)
        osc_env_pos -= 32;
      volume = pgm_read_byte(&mEnvelope.mWave[osc_env_pos]) >> half_vol;

      start_time = end_time;
      end_time += env_period;
      if (end_time > final_end_time)
        end_time = final_end_time;
    }
    osc.mDelay = time - final_end_time;

    if (!(mode & NOISE_OFF)) {
      mNoise.mDelay = ntime - final_end_time;
      mNoise.mLfsr = noise_lfsr;
    }
  }

  // TODO: optimized saw wave envelope?

  // maintain envelope phase
  blip_time_t remain = final_end_time - mLastTime - mEnvelope.mDelay;
  if (remain >= 0) {
    blargg_long count = (remain + env_period) / env_period;
    mEnvelope.mPos += count;
    if (mEnvelope.mPos >= 0)
      mEnvelope.mPos = (mEnvelope.mPos & 31) - 32;
    remain -= count * env_period;
    assert(-remain <= env_period);
  }
  mEnvelope.mDelay = -remain;
  assert(mEnvelope.mDelay > 0);
  assert(mEnvelope.mPos < 0);

  mLastTime = final_end_time;
}

}  // namespace ay
}  // namespace emu
}  // namespace gme
