// NesEmu 0.1.8. http://www.slack.net/~ant/

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

#include "NesOscs.h"
#include "NesApu.h"
#include "blargg_source.h"
#include <pgmspace.h>

namespace gme {
namespace emu {
namespace nes {

// NesOsc

void NesOsc::doLengthClock(int halt_mask) {
  if (mLengthCounter && !(mRegs[0] & halt_mask))
    mLengthCounter--;
}

void NesEnvelope::doEnvelopeClock() {
  int period = mRegs[0] & 15;
  if (mRegWritten[3]) {
    mRegWritten[3] = false;
    this->env_delay = period;
    this->envelope = 15;
  } else if (--this->env_delay < 0) {
    this->env_delay = period;
    if (this->envelope | (mRegs[0] & 0x20))
      this->envelope = (this->envelope - 1) & 15;
  }
}

int NesEnvelope::volume() const {
  if (!mLengthCounter)
    return 0;
  if (mRegs[0] & 0x10)
    return mRegs[0] & 15;
  return this->envelope;
}

// NesSquare

void NesSquare::doSweepClock(int negative_adjust) {
  const int sweep = mRegs[1];

  if (--this->sweep_delay < 0) {
    mRegWritten[1] = true;

    int period = mGetPeriod();
    int shift = sweep & SHIFT_MASK;
    if (shift && (sweep & 0x80) && period >= 8) {
      int offset = period >> shift;

      if (sweep & NEGATE_FLAG)
        offset = negative_adjust - offset;

      if (period + offset <= 0x7FF) {
        period += offset;
        mRegs[2] = period % 256;
        mRegs[3] = (mRegs[3] & 0b11111000) + period / 256;
      }
    }
  }

  if (mRegWritten[1]) {
    mRegWritten[1] = false;
    this->sweep_delay = (sweep >> 4) & 7;
  }
}

// TODO: clean up
inline nes_time_t NesSquare::maintain_phase(nes_time_t time, nes_time_t end_time, nes_time_t timer_period) {
  nes_time_t remain = end_time - time;
  if (remain > 0) {
    int count = (remain + timer_period - 1) / timer_period;
    this->phase = (this->phase + count) & (PHASE_RANGE - 1);
    time += (blargg_long) count * timer_period;
  }
  return time;
}

void NesSquare::run(nes_time_t time, nes_time_t end_time) {
  const int period = mGetPeriod();
  const int timer_period = (period + 1) * 2;

  if (mOutput == nullptr) {
    mDelay = maintain_phase(time + mDelay, end_time, timer_period) - end_time;
    return;
  }

  mOutput->setModified();

  int offset = period >> (mRegs[1] & SHIFT_MASK);
  if (mRegs[1] & NEGATE_FLAG)
    offset = 0;

  const int volume = this->volume();
  if (volume == 0 || period < 8 || (period + offset) >= 0x800) {
    if (mLastAmp) {
      this->mSynth.offset(time, -mLastAmp, mOutput);
      mLastAmp = 0;
    }

    time += mDelay;
    time = maintain_phase(time, end_time, timer_period);
  } else {
    // handle duty select
    int duty_select = (mRegs[0] >> 6) & 3;
    int duty = 1 << duty_select;  // 1, 2, 4, 2
    int amp = 0;
    if (duty_select == 3) {
      duty = 2;  // negated 25%
      amp = volume;
    }
    if (this->phase < duty)
      amp ^= volume;

    {
      int delta = mUpdateAmp(amp);
      if (delta)
        this->mSynth.offset(time, delta, mOutput);
    }

    time += mDelay;
    if (time < end_time) {
      BlipBuffer *const output = mOutput;
      const Synth &synth = this->mSynth;
      int delta = amp * 2 - volume;
      int phase = this->phase;

      do {
        phase = (phase + 1) & (PHASE_RANGE - 1);
        if (phase == 0 || phase == duty) {
          delta = -delta;
          synth.offset(time, delta, output);
        }
        time += timer_period;
      } while (time < end_time);

      mLastAmp = (delta + volume) >> 1;
      this->phase = phase;
    }
  }

  mDelay = time - end_time;
}

// NesTriangle

void NesTriangle::doLinearCounterClock() {
  if (mRegWritten[3])
    this->linear_counter = mRegs[0] & 0x7F;
  else if (this->linear_counter)
    this->linear_counter--;
  if (!(mRegs[0] & 0x80))
    mRegWritten[3] = false;
}

inline int NesTriangle::calc_amp() const {
  int amp = NesTriangle::PHASE_RANGE - this->phase;
  if (amp < 0)
    amp = this->phase - NesTriangle::PHASE_RANGE + 1;
  return amp;
}

// TODO: clean up
inline nes_time_t NesTriangle::maintain_phase(nes_time_t time, nes_time_t end_time, nes_time_t timer_period) {
  nes_time_t remain = end_time - time;
  if (remain > 0) {
    int count = (remain + timer_period - 1) / timer_period;
    this->phase = ((unsigned) this->phase + 1 - count) & (PHASE_RANGE * 2 - 1);
    this->phase++;
    time += (blargg_long) count * timer_period;
  }
  return time;
}

void NesTriangle::run(nes_time_t time, nes_time_t end_time) {
  const int timer_period = mGetPeriod() + 1;
  if (mOutput == nullptr) {
    time += mDelay;
    mDelay = 0;
    if (mLengthCounter && this->linear_counter && timer_period >= 3)
      mDelay = maintain_phase(time, end_time, timer_period) - end_time;
    return;
  }

  mOutput->setModified();

  // to do: track phase when period < 3
  // to do: Output 7.5 on dac when period < 2? More accurate, but results in
  // more clicks.

  int delta = mUpdateAmp(this->calc_amp());
  if (delta)
    this->mSynth.offset(time, delta, mOutput);

  time += mDelay;
  if (mLengthCounter == 0 || this->linear_counter == 0 || timer_period < 3) {
    time = end_time;
  } else if (time < end_time) {
    BlipBuffer *const output = mOutput;

    int phase = this->phase;
    int volume = 1;
    if (phase > PHASE_RANGE) {
      phase -= PHASE_RANGE;
      volume = -volume;
    }

    do {
      if (--phase == 0) {
        phase = PHASE_RANGE;
        volume = -volume;
      } else {
        this->mSynth.offset(time, volume, output);
      }

      time += timer_period;
    } while (time < end_time);

    if (volume < 0)
      phase += PHASE_RANGE;
    this->phase = phase;
    mLastAmp = this->calc_amp();
  }
  mDelay = time - end_time;
}

// NesDmc

void NesDmc::mReset() {
  this->address = 0;
  this->mDac = 0;
  this->buf = 0;
  this->bits_remain = 1;
  this->bits = 0;
  this->buf_full = false;
  this->silence = true;
  mNextIRQ = NesApu::NO_IRQ;
  mIRQFlag = false;
  this->mIsIRQEnabled = false;

  NesOsc::mReset();
  this->period = 0x1AC;
}

void NesDmc::mRecalcIRQ() {
  nes_time_t irq = NesApu::NO_IRQ;
  if (this->mIsIRQEnabled && mLengthCounter)
    irq =
        mApu->mLastDmcTime + mDelay + ((mLengthCounter - 1) * 8 + this->bits_remain - 1) * nes_time_t(this->period) + 1;
  if (irq != mNextIRQ) {
    mNextIRQ = irq;
    mApu->mIRQChanged();
  }
}

nes_time_t NesDmc::mGetNextReadTime() const {
  if (mLengthCounter == 0)
    return NesApu::NO_IRQ;  // not reading
  return mApu->mLastDmcTime + mDelay + long(this->bits_remain - 1) * this->period;
}

int NesDmc::mGetCountReads(nes_time_t time, nes_time_t *last_read) const {
  if (last_read)
    *last_read = time;

  if (mLengthCounter == 0)
    return 0;  // not reading

  nes_time_t first_read = this->mGetNextReadTime();
  nes_time_t avail = time - first_read;
  if (avail <= 0)
    return 0;

  int count = (avail - 1) / (this->period * 8) + 1;
  if (!(mRegs[0] & LOOP_FLAG) && count > mLengthCounter)
    count = mLengthCounter;

  if (last_read) {
    *last_read = first_read + (count - 1) * (this->period * 8) + 1;
    check(*last_read <= time);
    check(count == this->mGetCountReads(*last_read, nullptr));
    check(count - 1 == this->mGetCountReads(*last_read - 1, nullptr));
  }

  return count;
}

inline void NesDmc::mReloadSample() {
  this->address = 0x4000 + mRegs[2] * 0x40;
  mLengthCounter = 16 * mRegs[3] + 1;
}

inline uint16_t NesDmc::mGetPeriod(uint8_t data) const {
  static const uint16_t PERIOD_TABLE[][16] PROGMEM = {
      {428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54},  // NTSC
      {398, 354, 316, 298, 276, 236, 210, 198, 176, 148, 132, 118, 98, 78, 66, 50},   // PAL
  };
  return pgm_read_word(&PERIOD_TABLE[mApu->IsPAL()][data & 15]);
}

inline void NesDmc::mWriteR0(int data) {
  this->period = mGetPeriod(data);
  this->mIsIRQEnabled = (data & 0xC0) == 0x80;  // enabled only if loop disabled
  mIRQFlag &= this->mIsIRQEnabled;
  this->mRecalcIRQ();
}

inline int NesDmc::mGetDelta(uint8_t old) {
  static const uint8_t DAC_TABLE[] PROGMEM = {
      0,  1,  2,  3,  4,  5,  6,  7,  7,  8,  9,  10, 11, 12, 13, 14, 15, 15, 16, 17, 18, 19, 20, 20, 21, 22,
      23, 24, 24, 25, 26, 27, 27, 28, 29, 30, 31, 31, 32, 33, 33, 34, 35, 36, 36, 37, 38, 38, 39, 40, 41, 41,
      42, 43, 43, 44, 45, 45, 46, 47, 47, 48, 48, 49, 50, 50, 51, 52, 52, 53, 53, 54, 55, 55, 56, 56, 57, 58,
      58, 59, 59, 60, 60, 61, 61, 62, 63, 63, 64, 64, 65, 65, 66, 66, 67, 67, 68, 68, 69, 70, 70, 71, 71, 72,
      72, 73, 73, 74, 74, 75, 75, 75, 76, 76, 77, 77, 78, 78, 79, 79, 80, 80, 81, 81, 82, 82, 82, 83,
  };
  return pgm_read_byte(&DAC_TABLE[this->mDac]) - pgm_read_byte(&DAC_TABLE[old]);
}

inline void NesDmc::mWriteR1(int data) {
  data &= 0x7F;
  uint8_t old = this->mDac;
  this->mDac = data;
  // adjust mLastAmp so that "pop" amplitude will be properly non-linear with respect to change in dac
  if (!this->mNonlinear)
    mLastAmp = data - mGetDelta(old);
}

void NesDmc::mWriteRegister(int addr, int data) {
  if (addr == 0)
    return mWriteR0(data);
  if (addr == 1)
    return mWriteR1(data);
}

void NesDmc::mStart() {
  this->mReloadSample();
  this->mFillBuffer();
  this->mRecalcIRQ();
}

void NesDmc::mFillBuffer() {
  if (!this->buf_full && mLengthCounter) {
    assert(this->mPrgReader != nullptr);  // mPrgReader must be set
    this->buf = this->mPrgReader(this->mPrgReaderData, 0x8000u + this->address);
    this->address = (this->address + 1) & 0x7FFF;
    this->buf_full = true;
    if (--mLengthCounter == 0) {
      if (mRegs[0] & LOOP_FLAG) {
        this->mReloadSample();
      } else {
        mApu->mOscEnables &= ~0x10;
        mIRQFlag = this->mIsIRQEnabled;
        mNextIRQ = NesApu::NO_IRQ;
        mApu->mIRQChanged();
      }
    }
  }
}

void NesDmc::mRun(nes_time_t time, nes_time_t end_time) {
  int delta = mUpdateAmp(this->mDac);
  if (mOutput == nullptr) {
    this->silence = true;
  } else {
    mOutput->setModified();
    if (delta)
      this->mSynth.offset(time, delta, mOutput);
  }

  time += mDelay;
  if (time < end_time) {
    if (this->silence && !this->buf_full) {
      int count = (end_time - time + this->period - 1) / this->period;
      this->bits_remain = (this->bits_remain - 1 + 8 - (count % 8)) % 8 + 1;
      time += count * this->period;
    } else {
      do {
        if (!this->silence) {
          int step = (this->bits & 1) * 4 - 2;
          this->bits >>= 1;
          if (unsigned(this->mDac + step) <= 0x7F) {
            this->mDac += step;
            this->mSynth.offset(time, step, mOutput);
          }
        }

        time += this->period;

        if (--this->bits_remain == 0) {
          this->bits_remain = 8;
          if (!this->buf_full) {
            this->silence = true;
          } else {
            this->bits = this->buf;
            this->buf_full = false;
            this->silence = mOutput == nullptr;
            this->mFillBuffer();
          }
        }
      } while (time < end_time);

      mLastAmp = this->mDac;
    }
  }
  mDelay = time - end_time;
}

// NesNoise

inline uint16_t NesNoise::mGetPeriod() const {
  static const uint16_t PERIOD_TABLE[][16] PROGMEM = {
      {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068},  // NTSC
      {4, 8, 14, 30, 60, 88, 118, 148, 188, 236, 354, 472, 708, 944, 1890, 3778},   // PAL
  };
  return pgm_read_word(&PERIOD_TABLE[mApu->IsPAL()][mRegs[2] & 15]);
}

void NesNoise::run(nes_time_t time, nes_time_t end_time) {
  const uint16_t period = mGetPeriod();
  if (mOutput == nullptr) {
    // TODO: clean up
    time += mDelay;
    mDelay = time + (end_time - time + period - 1) / period * period - end_time;
    return;
  }

  mOutput->setModified();

  const int volume = this->volume();
  int amp = (this->noise & 1) ? volume : 0;
  {
    int delta = mUpdateAmp(amp);
    if (delta)
      this->mSynth.offset(time, delta, mOutput);
  }

  time += mDelay;
  if (time < end_time) {
    const int mode_flag = 0x80;

    if (!volume) {
      // round to next multiple of period
      time += (end_time - time + period - 1) / period * period;

      // approximate noise cycling while muted, by shuffling up noise
      // register to do: precise muted noise cycling?
      if (!(mRegs[2] & mode_flag)) {
        int feedback = (this->noise << 13) ^ (this->noise << 14);
        this->noise = (feedback & 0x4000) | (this->noise >> 1);
      }
    } else {
      // using resampled time avoids conversion in synth.offset()
      blip_resampled_time_t rperiod = mOutput->resampledDuration(period);
      blip_resampled_time_t rtime = mOutput->resampledTime(time);

      int delta = amp * 2 - volume;
      const int tap = (mRegs[2] & mode_flag ? 8 : 13);

      do {
        int feedback = (this->noise << tap) ^ (this->noise << 14);
        time += period;

        if ((this->noise + 1) & 2) {
          // bits 0 and 1 of noise differ
          delta = -delta;
          this->mSynth.offsetResampled(rtime, delta, mOutput);
        }

        rtime += rperiod;
        this->noise = (feedback & 0x4000) | (this->noise >> 1);
      } while (time < end_time);

      mLastAmp = (delta + volume) >> 1;
    }
  }

  mDelay = time - end_time;
}

}  // namespace nes
}  // namespace emu
}  // namespace gme
