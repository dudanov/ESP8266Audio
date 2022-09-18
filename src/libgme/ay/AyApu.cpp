// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "AyApu.h"
#include "../blargg_source.h"

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
static const unsigned NOISE_OFF = 0b1000;
static const unsigned TONE_OFF = 0b0001;

// Full table of the upper 8 envelope waveforms. Values already passed through volume table.
// With channels tied together and 1K resistor to ground (as datasheet recommends),
// output nearly matches logarithmic curve as claimed. Approx. 1.5 dB per step.
const uint8_t AyApu::Envelope::MODES[8][48] PROGMEM = {
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

inline uint8_t AyApu::Envelope::GetAmp(uint8_t volume, bool half) { return pgm_read_byte(&MODES[5][volume]) >> half; }

inline uint8_t AyApu::Envelope::GetAmp(bool half) const { return pgm_read_byte(mIt) >> half; }

inline void AyApu::Envelope::SetMode(uint8_t mode) {
  mIt = (mode & 0b1000) ? MODES[mode & 0b0111] : (mode & 0b0100) ? MODES[7] : MODES[1];
  mEnd = mIt + 48;
  mDelay = 0;  // will get set to envelope period in mRunUntil()
}

inline AyApu::Envelope &AyApu::Envelope::Advance() {
  if (++mIt == mEnd)
    mIt -= 32;
  return *this;
}

AyApu::AyApu() {
  SetOutput(nullptr);
  SetVolume(1.0);
  Reset();
}

void AyApu::Reset() {
  mLastClkTime = 0;
  mNoise.mDelay = 0;
  mNoise.mLfsr = 0b1;
  for (auto &osc : mSquare) {
    osc.mPeriod = CLOCK_PSC;
    osc.mDelay = 0;
    osc.mLastAmp = 0;
    osc.mPhase = 0;
  }
  mRegs.fill(0x00);
  mWriteRegister(AY_MIXER, 0xFF);
  mWriteRegister(AY_ENV_SHAPE, 0x00);
}

void AyApu::mWriteRegister(unsigned addr, uint8_t data) {
  assert(addr < RNUM);

  mRegs[addr] = data;

  // handle period changes accurately
  if (addr <= AY_CHNL_C_COARSE)
    return mPeriodUpdate(addr / 2);

  // TODO: same as above for envelope timer, and it also has a divide by two after it

  // envelope mode
  if (addr == AY_ENV_SHAPE)
    return mEnvelope.SetMode(data);
}

inline void AyApu::mPeriodUpdate(unsigned channel) {
  blip_clk_time_t period = get_le16(&mRegs[AY_CHNL_A_FINE + channel * 2]) % 4096 * CLOCK_PSC;
  if (period == 0)
    period = CLOCK_PSC;
  // adjust time of next timer expiration based on change in period
  Square &osc = mSquare[channel];
  if ((osc.mDelay += period - osc.mPeriod) >= period)
    osc.mDelay = 0;
  osc.mPeriod = period;
}

void AyApu::mRunUntil(const blip_clk_time_t end_clk_time) {
  require(end_clk_time >= mLastClkTime);

  // noise period and initial values
  constexpr blip_clk_time_t NOISE_PSC = CLOCK_PSC * 2;  // verified
  blip_clk_time_t noise_period = mRegs[AY_NOISE_PERIOD] % 32 * NOISE_PSC;
  if (noise_period == 0)
    noise_period = NOISE_PSC;
  const blargg_ulong old_noise_lfsr = mNoise.mLfsr;
  const blip_clk_time_t old_noise_delay = mNoise.mDelay;

  // envelope period
  constexpr blip_clk_time_t ENVELOPE_PSC = CLOCK_PSC * 2;  // verified
  blip_clk_time_t env_period = get_le16(&mRegs[AY_ENV_FINE]) * ENVELOPE_PSC;
  if (env_period == 0)
    env_period = ENVELOPE_PSC;  // same as period 1 on my AY chip
  if (mEnvelope.mDelay == 0)
    mEnvelope.mDelay = env_period;

  // run each osc separately
  for (int idx = 0; idx < OSCS_NUM; idx++) {
    Square &osc = mSquare[idx];
    uint8_t mode = mRegs[AY_MIXER] >> idx;

    // output
    BlipBuffer *const out = osc.mOutput;
    if (out == nullptr)
      continue;
    out->SetModified();

    // period
    bool half_vol = false;
    const blip_clk_time_t inaudible_period = (out->GetClockRate() + INAUDIBLE_FREQ) / (INAUDIBLE_FREQ * 2);
    if (osc.mPeriod <= inaudible_period && !(mode & TONE_OFF)) {
      half_vol = true;  // Actually around 60%, but 50% is close enough
      mode |= TONE_OFF;
    }

    // envelope
    blip_clk_time_t start_time = mLastClkTime;
    blip_clk_time_t end_time = end_clk_time;
    const uint8_t amp_ctrl = mRegs[AY_CHNL_A_VOL + idx];
    int volume = Envelope::GetAmp(amp_ctrl & 0b1111, half_vol);
    // int osc_env_pos = mEnvelope.mPos;
    if (amp_ctrl & 0x10) {
      volume = mEnvelope.GetAmp(half_vol);
      // use envelope only if it's a repeating wave or a ramp that hasn't finished
      if (!(mRegs[AY_ENV_SHAPE] & 1) || mEnvelope.InRampPhase()) {
        end_time = start_time + mEnvelope.mDelay;
        if (end_time >= end_clk_time)
          end_time = end_clk_time;

        // if ( !(mRegs [12] | mRegs [11]) )
        //  debug_printf( "Used envelope period 0\n" );
      } else if (volume == 0) {
        mode = NOISE_OFF | TONE_OFF;
      }
    } else if (volume == 0) {
      mode = NOISE_OFF | TONE_OFF;
    }

    // tone time
    const blip_clk_time_t period = osc.mPeriod;
    blip_clk_time_t time = start_time + osc.mDelay;
    // maintain tone's phase when off
    if (mode & TONE_OFF) {
      blip_clk_time_t count = (end_clk_time - time + period - 1) / period;
      time += count * period;
      osc.mPhase ^= count & 1;
    }

    // noise time
    blip_clk_time_t ntime = end_clk_time;
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
          mSynth.Offset(out, start_time, delta);
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
          blip_clk_time_t end = end_time;
          if (end_time > time)
            end = time;
          if (phase & delta_non_zero) {
            // must advance *past* time to avoid hang
            while (ntime <= end) {
              int changed = noise_lfsr + 1;
              noise_lfsr = (-(noise_lfsr & 1) & 0x12000) ^ (noise_lfsr >> 1);
              if (changed & 2) {
                delta = -delta;
                mSynth.Offset(out, ntime, delta);
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
              mSynth.Offset(out, time, delta);
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

      if (end_time >= end_clk_time)
        break;  // breaks first time when envelope is disabled

      // next envelope step
      volume = mEnvelope.Advance().GetAmp(half_vol);

      start_time = end_time;
      end_time += env_period;
      if (end_time > end_clk_time)
        end_time = end_clk_time;
    }
    osc.mDelay = time - end_clk_time;

    if (!(mode & NOISE_OFF)) {
      mNoise.mDelay = ntime - end_clk_time;
      mNoise.mLfsr = noise_lfsr;
    }
  }

  // TODO: optimized saw wave envelope?

  // maintain envelope phase
  long remain = end_clk_time - mLastClkTime - mEnvelope.mDelay;
  if (remain >= 0) {
    blargg_long count = (remain + env_period) / env_period;
    // mEnvelope.mPos += count;
    // if (mEnvelope.mPos >= 0)
    //   mEnvelope.mPos = (mEnvelope.mPos & 31) - 32;
    remain -= count * env_period;
    assert(-remain <= env_period);
  }
  mEnvelope.mDelay = -remain;
  assert(mEnvelope.mDelay > 0);
  // assert(mEnvelope.mPos < 0);

  mLastClkTime = end_clk_time;
}

}  // namespace ay
}  // namespace emu
}  // namespace gme
