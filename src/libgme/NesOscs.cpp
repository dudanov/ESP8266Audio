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
  if (this->mLengthCounter && !(this->mRegs[0] & halt_mask))
    this->mLengthCounter--;
}

void NesEnvelope::doEnvelopeClock() {
  int period = this->mRegs[0] & 15;
  if (this->mRegWritten[3]) {
    this->mRegWritten[3] = false;
    this->env_delay = period;
    this->envelope = 15;
  } else if (--this->env_delay < 0) {
    this->env_delay = period;
    if (this->envelope | (this->mRegs[0] & 0x20))
      this->envelope = (this->envelope - 1) & 15;
  }
}

int NesEnvelope::volume() const {
  if (!this->mLengthCounter)
    return 0;
  if (this->mRegs[0] & 0x10)
    return this->mRegs[0] & 15;
  return this->envelope;
}

// NesSquare

void NesSquare::doSweepClock(int negative_adjust) {
  const int sweep = this->mRegs[1];

  if (--this->sweep_delay < 0) {
    this->mRegWritten[1] = true;

    int period = this->mGetPeriod();
    int shift = sweep & SHIFT_MASK;
    if (shift && (sweep & 0x80) && period >= 8) {
      int offset = period >> shift;

      if (sweep & NEGATE_FLAG)
        offset = negative_adjust - offset;

      if (period + offset <= 0x7FF) {
        period += offset;
        this->mRegs[2] = period % 256;
        this->mRegs[3] = (this->mRegs[3] & 0b11111000) + period / 256;
      }
    }
  }

  if (this->mRegWritten[1]) {
    this->mRegWritten[1] = false;
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
  const int period = this->mGetPeriod();
  const int timer_period = (period + 1) * 2;

  if (this->mOutput == nullptr) {
    this->mDelay = this->maintain_phase(time + this->mDelay, end_time, timer_period) - end_time;
    return;
  }

  this->mOutput->setModified();

  int offset = period >> (this->mRegs[1] & SHIFT_MASK);
  if (this->mRegs[1] & NEGATE_FLAG)
    offset = 0;

  const int volume = this->volume();
  if (volume == 0 || period < 8 || (period + offset) >= 0x800) {
    if (this->mLastAmp) {
      this->synth.offset(time, -this->mLastAmp, this->mOutput);
      this->mLastAmp = 0;
    }

    time += this->mDelay;
    time = this->maintain_phase(time, end_time, timer_period);
  } else {
    // handle duty select
    int duty_select = (this->mRegs[0] >> 6) & 3;
    int duty = 1 << duty_select;  // 1, 2, 4, 2
    int amp = 0;
    if (duty_select == 3) {
      duty = 2;  // negated 25%
      amp = volume;
    }
    if (this->phase < duty)
      amp ^= volume;

    {
      int delta = this->mUpdateAmp(amp);
      if (delta)
        this->synth.offset(time, delta, this->mOutput);
    }

    time += mDelay;
    if (time < end_time) {
      BlipBuffer *const output = this->mOutput;
      const Synth &synth = this->synth;
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

      this->mLastAmp = (delta + volume) >> 1;
      this->phase = phase;
    }
  }

  this->mDelay = time - end_time;
}

// NesTriangle

void NesTriangle::doLinearCounterClock() {
  if (this->mRegWritten[3])
    this->linear_counter = this->mRegs[0] & 0x7F;
  else if (this->linear_counter)
    this->linear_counter--;
  if (!(this->mRegs[0] & 0x80))
    this->mRegWritten[3] = false;
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
  const int timer_period = this->mGetPeriod() + 1;
  if (this->mOutput == nullptr) {
    time += this->mDelay;
    this->mDelay = 0;
    if (this->mLengthCounter && this->linear_counter && timer_period >= 3)
      this->mDelay = this->maintain_phase(time, end_time, timer_period) - end_time;
    return;
  }

  this->mOutput->setModified();

  // to do: track phase when period < 3
  // to do: Output 7.5 on dac when period < 2? More accurate, but results in
  // more clicks.

  int delta = this->mUpdateAmp(this->calc_amp());
  if (delta)
    this->synth.offset(time, delta, mOutput);

  time += this->mDelay;
  if (this->mLengthCounter == 0 || this->linear_counter == 0 || timer_period < 3) {
    time = end_time;
  } else if (time < end_time) {
    BlipBuffer *const output = this->mOutput;

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
        this->synth.offset(time, volume, output);
      }

      time += timer_period;
    } while (time < end_time);

    if (volume < 0)
      phase += PHASE_RANGE;
    this->phase = phase;
    this->mLastAmp = this->calc_amp();
  }
  this->mDelay = time - end_time;
}

// NesDmc

void NesDmc::mReset() {
  this->address = 0;
  this->dac = 0;
  this->buf = 0;
  this->bits_remain = 1;
  this->bits = 0;
  this->buf_full = false;
  this->silence = true;
  this->m_nextIrq = NesApu::NO_IRQ;
  this->m_irqFlag = false;
  this->irq_enabled = false;

  NesOsc::mReset();
  this->period = 0x1AC;
}

void NesDmc::recalc_irq() {
  nes_time_t irq = NesApu::NO_IRQ;
  if (this->irq_enabled && this->mLengthCounter)
    irq = this->mApu->m_lastDmcTime + this->mDelay +
          ((this->mLengthCounter - 1) * 8 + this->bits_remain - 1) * nes_time_t(this->period) + 1;
  if (irq != this->m_nextIrq) {
    this->m_nextIrq = irq;
    this->mApu->mIrqChanged();
  }
}

int NesDmc::count_reads(nes_time_t time, nes_time_t *last_read) const {
  if (last_read)
    *last_read = time;

  if (this->mLengthCounter == 0)
    return 0;  // not reading

  nes_time_t first_read = this->next_read_time();
  nes_time_t avail = time - first_read;
  if (avail <= 0)
    return 0;

  int count = (avail - 1) / (this->period * 8) + 1;
  if (!(this->mRegs[0] & LOOP_FLAG) && count > this->mLengthCounter)
    count = this->mLengthCounter;

  if (last_read) {
    *last_read = first_read + (count - 1) * (this->period * 8) + 1;
    check(*last_read <= time);
    check(count == this->count_reads(*last_read, nullptr));
    check(count - 1 == this->count_reads(*last_read - 1, nullptr));
  }

  return count;
}

inline void NesDmc::reload_sample() {
  this->address = 0x4000 + this->mRegs[2] * 0x40;
  this->mLengthCounter = 16 * this->mRegs[3] + 1;
}

inline uint16_t NesDmc::mGetPeriod(uint8_t data) const {
  static const uint16_t PERIOD_TABLE[][16] PROGMEM = {
      {428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54},  // NTSC
      {398, 354, 316, 298, 276, 236, 210, 198, 176, 148, 132, 118, 98, 78, 66, 50},   // PAL
  };
  return pgm_read_word(&PERIOD_TABLE[this->mApu->IsPAL()][data & 15]);
}

inline void NesDmc::mWriteR0(uint8_t data) {
  this->period = this->mGetPeriod(data);
  this->irq_enabled = (data & 0xC0) == 0x80;  // enabled only if loop disabled
  this->m_irqFlag &= this->irq_enabled;
  this->recalc_irq();
}

inline uint8_t NesDmc::sGetDelta(uint8_t dacNew, uint8_t dacOld) {
  static const uint8_t DAC_TABLE[] PROGMEM = {
      0,  1,  2,  3,  4,  5,  6,  7,  7,  8,  9,  10, 11, 12, 13, 14, 15, 15, 16, 17, 18, 19, 20, 20, 21, 22,
      23, 24, 24, 25, 26, 27, 27, 28, 29, 30, 31, 31, 32, 33, 33, 34, 35, 36, 36, 37, 38, 38, 39, 40, 41, 41,
      42, 43, 43, 44, 45, 45, 46, 47, 47, 48, 48, 49, 50, 50, 51, 52, 52, 53, 53, 54, 55, 55, 56, 56, 57, 58,
      58, 59, 59, 60, 60, 61, 61, 62, 63, 63, 64, 64, 65, 65, 66, 66, 67, 67, 68, 68, 69, 70, 70, 71, 71, 72,
      72, 73, 73, 74, 74, 75, 75, 75, 76, 76, 77, 77, 78, 78, 79, 79, 80, 80, 81, 81, 82, 82, 82, 83,
  };
  return pgm_read_byte(&DAC_TABLE[dacNew]) - pgm_read_byte(&DAC_TABLE[dacOld]);
}

inline void NesDmc::mWriteR1(uint8_t data) {
  data &= 0x7F;
  uint8_t old = this->dac;
  this->dac = data;
  // adjust mLastAmp so that "pop" amplitude will be properly non-linear with respect to change in dac
  if (!this->nonlinear)
    this->mLastAmp = data - sGetDelta(data, old);
}

void NesDmc::writeRegister(int addr, int data) {
  if (addr == 0)
    return this->mWriteR0(data);
  if (addr == 1)
    return this->mWriteR1(data);
}

void NesDmc::start() {
  this->reload_sample();
  this->fill_buffer();
  this->recalc_irq();
}

void NesDmc::fill_buffer() {
  if (!this->buf_full && this->mLengthCounter) {
    assert(this->prg_reader != nullptr);  // prg_reader must be set
    this->buf = this->prg_reader(this->prg_reader_data, 0x8000u + this->address);
    this->address = (this->address + 1) & 0x7FFF;
    this->buf_full = true;
    if (--this->mLengthCounter == 0) {
      if (this->mRegs[0] & LOOP_FLAG) {
        this->reload_sample();
      } else {
        this->mApu->m_oscEnables &= ~0x10;
        this->m_irqFlag = this->irq_enabled;
        this->m_nextIrq = NesApu::NO_IRQ;
        this->mApu->mIrqChanged();
      }
    }
  }
}

void NesDmc::run(nes_time_t time, nes_time_t end_time) {
  int delta = this->mUpdateAmp(this->dac);
  if (this->mOutput == nullptr) {
    this->silence = true;
  } else {
    this->mOutput->setModified();
    if (delta)
      this->synth.offset(time, delta, this->mOutput);
  }

  time += this->mDelay;
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
          if (unsigned(this->dac + step) <= 0x7F) {
            this->dac += step;
            this->synth.offset(time, step, this->mOutput);
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
            this->silence = this->mOutput == nullptr;
            this->fill_buffer();
          }
        }
      } while (time < end_time);

      this->mLastAmp = this->dac;
    }
  }
  this->mDelay = time - end_time;
}

// NesNoise

inline uint16_t NesNoise::mGetPeriod() const {
  static const uint16_t PERIOD_TABLE[][16] PROGMEM = {
      {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068},  // NTSC
      {4, 8, 14, 30, 60, 88, 118, 148, 188, 236, 354, 472, 708, 944, 1890, 3778},   // PAL
  };
  return pgm_read_word(&PERIOD_TABLE[this->mApu->IsPAL()][this->mRegs[2] & 15]);
}

void NesNoise::run(nes_time_t time, nes_time_t end_time) {
  const uint16_t period = this->mGetPeriod();
  if (this->mOutput == nullptr) {
    // TODO: clean up
    time += this->mDelay;
    this->mDelay = time + (end_time - time + period - 1) / period * period - end_time;
    return;
  }

  this->mOutput->setModified();

  const int volume = this->volume();
  int amp = (this->noise & 1) ? volume : 0;
  {
    int delta = this->mUpdateAmp(amp);
    if (delta)
      this->synth.offset(time, delta, this->mOutput);
  }

  time += this->mDelay;
  if (time < end_time) {
    const int mode_flag = 0x80;

    if (!volume) {
      // round to next multiple of period
      time += (end_time - time + period - 1) / period * period;

      // approximate noise cycling while muted, by shuffling up noise
      // register to do: precise muted noise cycling?
      if (!(this->mRegs[2] & mode_flag)) {
        int feedback = (this->noise << 13) ^ (this->noise << 14);
        this->noise = (feedback & 0x4000) | (this->noise >> 1);
      }
    } else {
      // using resampled time avoids conversion in synth.offset()
      blip_resampled_time_t rperiod = this->mOutput->resampledDuration(period);
      blip_resampled_time_t rtime = this->mOutput->resampledTime(time);

      int delta = amp * 2 - volume;
      const int tap = (this->mRegs[2] & mode_flag ? 8 : 13);

      do {
        int feedback = (this->noise << tap) ^ (this->noise << 14);
        time += period;

        if ((this->noise + 1) & 2) {
          // bits 0 and 1 of noise differ
          delta = -delta;
          this->synth.offsetResampled(rtime, delta, this->mOutput);
        }

        rtime += rperiod;
        this->noise = (feedback & 0x4000) | (this->noise >> 1);
      } while (time < end_time);

      this->mLastAmp = (delta + volume) >> 1;
    }
  }

  this->mDelay = time - end_time;
}

}  // namespace nes
}  // namespace emu
}  // namespace gme
