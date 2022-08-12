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
  if (this->lengthCounter && !(this->regs[0] & halt_mask))
    this->lengthCounter--;
}

void NesEnvelope::doEnvelopeClock() {
  int period = this->regs[0] & 15;
  if (this->regWritten[3]) {
    this->regWritten[3] = false;
    this->env_delay = period;
    this->envelope = 15;
  } else if (--this->env_delay < 0) {
    this->env_delay = period;
    if (this->envelope | (this->regs[0] & 0x20))
      this->envelope = (this->envelope - 1) & 15;
  }
}

int NesEnvelope::volume() const {
  if (!this->lengthCounter)
    return 0;
  if (this->regs[0] & 0x10)
    return this->regs[0] & 15;
  return this->envelope;
}

// NesSquare

void NesSquare::doSweepClock(int negative_adjust) {
  const int sweep = this->regs[1];

  if (--this->sweep_delay < 0) {
    this->regWritten[1] = true;

    int period = this->getPeriod();
    int shift = sweep & SHIFT_MASK;
    if (shift && (sweep & 0x80) && period >= 8) {
      int offset = period >> shift;

      if (sweep & NEGATE_FLAG)
        offset = negative_adjust - offset;

      if (period + offset <= 0x7FF) {
        period += offset;
        this->regs[2] = period % 256;
        this->regs[3] = (this->regs[3] & 0b11111000) + period / 256;
      }
    }
  }

  if (this->regWritten[1]) {
    this->regWritten[1] = false;
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
  const int period = this->getPeriod();
  const int timer_period = (period + 1) * 2;

  if (this->m_output == nullptr) {
    this->delay = this->maintain_phase(time + this->delay, end_time, timer_period) - end_time;
    return;
  }

  this->m_output->setModified();

  int offset = period >> (this->regs[1] & SHIFT_MASK);
  if (this->regs[1] & NEGATE_FLAG)
    offset = 0;

  const int volume = this->volume();
  if (volume == 0 || period < 8 || (period + offset) >= 0x800) {
    if (this->lastAmp) {
      this->synth.offset(time, -this->lastAmp, this->m_output);
      this->lastAmp = 0;
    }

    time += this->delay;
    time = this->maintain_phase(time, end_time, timer_period);
  } else {
    // handle duty select
    int duty_select = (this->regs[0] >> 6) & 3;
    int duty = 1 << duty_select;  // 1, 2, 4, 2
    int amp = 0;
    if (duty_select == 3) {
      duty = 2;  // negated 25%
      amp = volume;
    }
    if (this->phase < duty)
      amp ^= volume;

    {
      int delta = this->m_updateAmp(amp);
      if (delta)
        this->synth.offset(time, delta, this->m_output);
    }

    time += delay;
    if (time < end_time) {
      BlipBuffer *const output = this->m_output;
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

      this->lastAmp = (delta + volume) >> 1;
      this->phase = phase;
    }
  }

  this->delay = time - end_time;
}

// NesTriangle

void NesTriangle::doLinearCounterClock() {
  if (this->regWritten[3])
    this->linear_counter = this->regs[0] & 0x7F;
  else if (this->linear_counter)
    this->linear_counter--;
  if (!(this->regs[0] & 0x80))
    this->regWritten[3] = false;
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
  const int timer_period = this->getPeriod() + 1;
  if (this->m_output == nullptr) {
    time += this->delay;
    this->delay = 0;
    if (this->lengthCounter && this->linear_counter && timer_period >= 3)
      this->delay = this->maintain_phase(time, end_time, timer_period) - end_time;
    return;
  }

  this->m_output->setModified();

  // to do: track phase when period < 3
  // to do: Output 7.5 on dac when period < 2? More accurate, but results in
  // more clicks.

  int delta = this->m_updateAmp(this->calc_amp());
  if (delta)
    this->synth.offset(time, delta, m_output);

  time += this->delay;
  if (this->lengthCounter == 0 || this->linear_counter == 0 || timer_period < 3) {
    time = end_time;
  } else if (time < end_time) {
    BlipBuffer *const output = this->m_output;

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
    this->lastAmp = this->calc_amp();
  }
  this->delay = time - end_time;
}

// NesDmc

void NesDmc::reset() {
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

  NesOsc::m_reset();
  this->period = 0x1AC;
}

void NesDmc::recalc_irq() {
  nes_time_t irq = NesApu::NO_IRQ;
  if (this->irq_enabled && this->lengthCounter)
    irq = this->m_apu->m_lastDmcTime + this->delay +
          ((this->lengthCounter - 1) * 8 + this->bits_remain - 1) * nes_time_t(this->period) + 1;
  if (irq != this->m_nextIrq) {
    this->m_nextIrq = irq;
    this->m_apu->mIrqChanged();
  }
}

int NesDmc::count_reads(nes_time_t time, nes_time_t *last_read) const {
  if (last_read)
    *last_read = time;

  if (this->lengthCounter == 0)
    return 0;  // not reading

  nes_time_t first_read = this->next_read_time();
  nes_time_t avail = time - first_read;
  if (avail <= 0)
    return 0;

  int count = (avail - 1) / (this->period * 8) + 1;
  if (!(this->regs[0] & LOOP_FLAG) && count > this->lengthCounter)
    count = this->lengthCounter;

  if (last_read) {
    *last_read = first_read + (count - 1) * (this->period * 8) + 1;
    check(*last_read <= time);
    check(count == this->count_reads(*last_read, nullptr));
    check(count - 1 == this->count_reads(*last_read - 1, nullptr));
  }

  return count;
}

inline void NesDmc::reload_sample() {
  this->address = 0x4000 + this->regs[2] * 0x40;
  this->lengthCounter = 16 * this->regs[3] + 1;
}

inline uint16_t NesDmc::mGetPeriod(int data) const {
  static const uint16_t PERIOD_TABLE[][16] PROGMEM = {
      {428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54},  // NTSC
      {398, 354, 316, 298, 276, 236, 210, 198, 176, 148, 132, 118, 98, 78, 66, 50},   // PAL
  };
  return pgm_read_word(&PERIOD_TABLE[this->m_apu->IsPAL()][data & 15]);
}

inline void NesDmc::mWriteR0(int data) {
  this->period = this->mGetPeriod(data);
  this->irq_enabled = (data & 0xC0) == 0x80;  // enabled only if loop disabled
  this->m_irqFlag &= this->irq_enabled;
  this->recalc_irq();
}

static int sGetDelta(size_t dacNew, size_t dacOld) {
  static const uint8_t DAC_TABLE[] PROGMEM = {
      0,  1,  2,  3,  4,  5,  6,  7,  7,  8,  9,  10, 11, 12, 13, 14, 15, 15, 16, 17, 18, 19, 20, 20, 21, 22,
      23, 24, 24, 25, 26, 27, 27, 28, 29, 30, 31, 31, 32, 33, 33, 34, 35, 36, 36, 37, 38, 38, 39, 40, 41, 41,
      42, 43, 43, 44, 45, 45, 46, 47, 47, 48, 48, 49, 50, 50, 51, 52, 52, 53, 53, 54, 55, 55, 56, 56, 57, 58,
      58, 59, 59, 60, 60, 61, 61, 62, 63, 63, 64, 64, 65, 65, 66, 66, 67, 67, 68, 68, 69, 70, 70, 71, 71, 72,
      72, 73, 73, 74, 74, 75, 75, 75, 76, 76, 77, 77, 78, 78, 79, 79, 80, 80, 81, 81, 82, 82, 82, 83,
  };
  return pgm_read_byte(&DAC_TABLE[dacNew]) - pgm_read_byte(&DAC_TABLE[dacOld]);
}

inline void NesDmc::mWriteR1(int data) {
  data &= 0x7F;
  int old = this->dac;
  this->dac = data;
  // adjust lastAmp so that "pop" amplitude will be properly non-linear with respect to change in dac
  if (!this->nonlinear)
    this->lastAmp = data - sGetDelta(data, old);
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
  if (!this->buf_full && this->lengthCounter) {
    assert(this->prg_reader != nullptr);  // prg_reader must be set
    this->buf = this->prg_reader(this->prg_reader_data, 0x8000u + this->address);
    this->address = (this->address + 1) & 0x7FFF;
    this->buf_full = true;
    if (--this->lengthCounter == 0) {
      if (this->regs[0] & LOOP_FLAG) {
        this->reload_sample();
      } else {
        this->m_apu->m_oscEnables &= ~0x10;
        this->m_irqFlag = this->irq_enabled;
        this->m_nextIrq = NesApu::NO_IRQ;
        this->m_apu->mIrqChanged();
      }
    }
  }
}

void NesDmc::run(nes_time_t time, nes_time_t end_time) {
  int delta = this->m_updateAmp(this->dac);
  if (this->m_output == nullptr) {
    this->silence = true;
  } else {
    this->m_output->setModified();
    if (delta)
      this->synth.offset(time, delta, this->m_output);
  }

  time += this->delay;
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
            this->synth.offset(time, step, this->m_output);
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
            this->silence = this->m_output == nullptr;
            this->fill_buffer();
          }
        }
      } while (time < end_time);

      this->lastAmp = this->dac;
    }
  }
  this->delay = time - end_time;
}

// NesNoise

inline uint16_t NesNoise::mGetPeriod() const {
  static const uint16_t PERIOD_TABLE[][16] PROGMEM = {
      {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068},  // NTSC
      {4, 8, 14, 30, 60, 88, 118, 148, 188, 236, 354, 472, 708, 944, 1890, 3778},   // PAL
  };
  return pgm_read_word(&PERIOD_TABLE[this->m_apu->IsPAL()][this->regs[2] & 15]);
}

void NesNoise::run(nes_time_t time, nes_time_t end_time) {
  const uint16_t period = this->mGetPeriod();
  if (this->m_output == nullptr) {
    // TODO: clean up
    time += this->delay;
    this->delay = time + (end_time - time + period - 1) / period * period - end_time;
    return;
  }

  this->m_output->setModified();

  const int volume = this->volume();
  int amp = (this->noise & 1) ? volume : 0;
  {
    int delta = this->m_updateAmp(amp);
    if (delta)
      this->synth.offset(time, delta, this->m_output);
  }

  time += this->delay;
  if (time < end_time) {
    const int mode_flag = 0x80;

    if (!volume) {
      // round to next multiple of period
      time += (end_time - time + period - 1) / period * period;

      // approximate noise cycling while muted, by shuffling up noise
      // register to do: precise muted noise cycling?
      if (!(this->regs[2] & mode_flag)) {
        int feedback = (this->noise << 13) ^ (this->noise << 14);
        this->noise = (feedback & 0x4000) | (this->noise >> 1);
      }
    } else {
      // using resampled time avoids conversion in synth.offset()
      blip_resampled_time_t rperiod = this->m_output->resampledDuration(period);
      blip_resampled_time_t rtime = this->m_output->resampledTime(time);

      int delta = amp * 2 - volume;
      const int tap = (this->regs[2] & mode_flag ? 8 : 13);

      do {
        int feedback = (this->noise << tap) ^ (this->noise << 14);
        time += period;

        if ((this->noise + 1) & 2) {
          // bits 0 and 1 of noise differ
          delta = -delta;
          this->synth.offsetResampled(rtime, delta, this->m_output);
        }

        rtime += rperiod;
        this->noise = (feedback & 0x4000) | (this->noise >> 1);
      } while (time < end_time);

      this->lastAmp = (delta + volume) >> 1;
    }
  }

  this->delay = time - end_time;
}

}  // namespace nes
}  // namespace emu
}  // namespace gme
