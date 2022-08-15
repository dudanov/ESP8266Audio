// Nes_Snd_Emu 0.1.8. http://www.slack.net/~ant/

#include "NesApu.h"
#include <pgmspace.h>

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

static const int AMP_RANGE = 15;
static const unsigned OSC_REGS = 4;

NesApu::NesApu() : mTriangle(this), mNoise(this), mDmc(this) {
  this->mTempo = 1.0;
  this->mDmc.prg_reader = nullptr;
  this->m_irqNotifier = nullptr;

  SetOutput(NULL);
  SetVolume(1.0);
  Reset(false);
}

void NesApu::SetTrebleEq(const BlipEq &eq) {
  this->mSquareSynth.setTrebleEq(eq);
  this->mTriangle.synth.setTrebleEq(eq);
  this->mNoise.synth.setTrebleEq(eq);
  this->mDmc.synth.setTrebleEq(eq);
}

void NesApu::mEnableNonlinear(double v) {
  this->mDmc.nonlinear = true;
  this->mSquareSynth.setVolume(1.3 * 0.25751258 / 0.742467605 * 0.25 / AMP_RANGE * v);

  const double tnd = 0.48 / 202 * this->mNonlinearTndGain();
  this->mTriangle.synth.setVolume(3.0 * tnd);
  this->mNoise.synth.setVolume(2.0 * tnd);
  this->mDmc.synth.setVolume(tnd);

  this->mSquare1.mLastAmp = 0;
  this->mSquare2.mLastAmp = 0;
  this->mTriangle.mLastAmp = 0;
  this->mNoise.mLastAmp = 0;
  this->mDmc.mLastAmp = 0;
}

void NesApu::SetVolume(double v) {
  this->mDmc.nonlinear = false;
  this->mSquareSynth.setVolume(0.1128 / AMP_RANGE * v);
  this->mTriangle.synth.setVolume(0.12765 / AMP_RANGE * v);
  this->mNoise.synth.setVolume(0.0741 / AMP_RANGE * v);
  this->mDmc.synth.setVolume(0.42545 / 127 * v);
}

void NesApu::SetOutput(BlipBuffer *buffer) {
  for (auto osc : this->mOscs)
    osc->SetOutput(buffer);
}

void NesApu::SetTempo(double t) {
  this->mTempo = t;
  this->m_framePeriod = (this->m_palMode ? 8314 : 7458);
  if (t != 1.0)
    this->m_framePeriod = (int) (this->m_framePeriod / t) & ~1;  // must be even
}

void NesApu::Reset(bool pal_mode, int initial_dmc_dac) {
  this->m_palMode = pal_mode;
  SetTempo(this->mTempo);

  this->mSquare1.reset();
  this->mSquare2.reset();
  this->mTriangle.reset();
  this->mNoise.reset();
  this->mDmc.reset();

  this->mLastTime = 0;
  this->m_lastDmcTime = 0;
  this->m_oscEnables = 0;
  this->m_irqFlag = false;
  this->m_earliestIrq = NO_IRQ;
  this->m_frameDelay = 1;
  WriteRegister(0, 0x4017, 0x00);
  WriteRegister(0, 0x4015, 0x00);

  for (nes_addr_t addr = START_ADDR; addr <= 0x4013; addr++)
    WriteRegister(0, addr, (addr & 3) ? 0x00 : 0x10);

  this->mDmc.dac = initial_dmc_dac;
  if (!this->mDmc.nonlinear)
    this->mTriangle.mLastAmp = 15;
  if (!this->mDmc.nonlinear)               // TODO: remove?
    this->mDmc.mLastAmp = initial_dmc_dac;  // prevent output transition
}

void NesApu::mIrqChanged() {
  nes_time_t new_irq = this->mDmc.m_nextIrq;
  if (this->mDmc.m_irqFlag | this->m_irqFlag) {
    new_irq = 0;
  } else if (new_irq > this->m_nextIrq) {
    new_irq = this->m_nextIrq;
  }

  if (new_irq != this->m_earliestIrq) {
    this->m_earliestIrq = new_irq;
    if (this->m_irqNotifier)
      this->m_irqNotifier(this->m_irqData);
  }
}

// frames

void NesApu::RunUntil(nes_time_t end_time) {
  require(end_time >= this->m_lastDmcTime);
  if (end_time > NextDmcReadTime()) {
    nes_time_t start = this->m_lastDmcTime;
    this->m_lastDmcTime = end_time;
    this->mDmc.run(start, end_time);
  }
}

void NesApu::mRunUntil(nes_time_t end_time) {
  require(end_time >= this->mLastTime);

  if (end_time == this->mLastTime)
    return;

  if (this->m_lastDmcTime < end_time) {
    nes_time_t start = this->m_lastDmcTime;
    this->m_lastDmcTime = end_time;
    this->mDmc.run(start, end_time);
  }

  while (true) {
    // earlier of next frame time or end time
    nes_time_t time = this->mLastTime + this->m_frameDelay;
    if (time > end_time)
      time = end_time;
    this->m_frameDelay -= time - this->mLastTime;

    // run oscs to present
    this->mSquare1.run(this->mLastTime, time);
    this->mSquare2.run(this->mLastTime, time);
    this->mTriangle.run(this->mLastTime, time);
    this->mNoise.run(this->mLastTime, time);
    this->mLastTime = time;

    if (time == end_time)
      break;  // no more frames to run

    // take frame-specific actions
    this->m_frameDelay = this->m_framePeriod;
    switch (this->m_frame++) {
      case 0:
        if (!(this->m_frameMode & 0xC0)) {
          this->m_nextIrq = time + this->m_framePeriod * 4 + 2;
          this->m_irqFlag = true;
        }
        // fall through
      case 2:
        // clock length and sweep on frames 0 and 2
        this->mSquare1.doLengthClock(0x20);
        this->mSquare2.doLengthClock(0x20);
        this->mNoise.doLengthClock(0x20);
        this->mTriangle.doLengthClock(0x80);  // different bit for halt flag on triangle

        this->mSquare1.doSweepClock(-1);
        this->mSquare2.doSweepClock(0);

        // frame 2 is slightly shorter in mode 1
        if (this->m_palMode && this->m_frame == 3)
          this->m_frameDelay -= 2;
        break;

      case 1:
        // frame 1 is slightly shorter in mode 0
        if (!this->m_palMode)
          this->m_frameDelay -= 2;
        break;

      case 3:
        this->m_frame = 0;

        // frame 3 is almost twice as long in mode 1
        if (this->m_frameMode & 0x80)
          this->m_frameDelay += this->m_framePeriod - (this->m_palMode ? 2 : 6);
        break;
    }

    // clock envelopes and linear counter every frame
    this->mTriangle.doLinearCounterClock();
    this->mSquare1.doEnvelopeClock();
    this->mSquare2.doEnvelopeClock();
    this->mNoise.doEnvelopeClock();
  }
}

template<class T> inline void zero_apu_osc(T *osc, nes_time_t time) {
  BlipBuffer *output = osc->mOutput;
  int last_amp = osc->mLastAmp;
  osc->mLastAmp = 0;
  if (output && last_amp)
    osc->synth.offset(time, -last_amp, output);
}

void NesApu::EndFrame(nes_time_t end_time) {
  if (end_time > this->mLastTime)
    this->mRunUntil(end_time);

  if (this->mDmc.nonlinear) {
    zero_apu_osc(&this->mSquare1, this->mLastTime);
    zero_apu_osc(&this->mSquare2, this->mLastTime);
    zero_apu_osc(&this->mTriangle, this->mLastTime);
    zero_apu_osc(&this->mNoise, this->mLastTime);
    zero_apu_osc(&this->mDmc, this->mLastTime);
  }

  // make times relative to new frame
  this->mLastTime -= end_time;
  require(this->mLastTime >= 0);

  this->m_lastDmcTime -= end_time;
  require(this->m_lastDmcTime >= 0);

  if (this->m_nextIrq != NO_IRQ) {
    this->m_nextIrq -= end_time;
    check(this->m_nextIrq >= 0);
  }
  if (this->mDmc.m_nextIrq != NO_IRQ) {
    this->mDmc.m_nextIrq -= end_time;
    check(this->mDmc.m_nextIrq >= 0);
  }
  if (this->m_earliestIrq != NO_IRQ) {
    this->m_earliestIrq -= end_time;
    if (this->m_earliestIrq < 0)
      this->m_earliestIrq = 0;
  }
}

// registers

void NesApu::WriteRegister(nes_time_t time, nes_addr_t addr, uint8_t data) {
  static const uint8_t LENGTH_TABLE[] PROGMEM = {0x0A, 0xFE, 0x14, 0x02, 0x28, 0x04, 0x50, 0x06, 0xA0, 0x08, 0x3C,
                                                 0x0A, 0x0E, 0x0C, 0x1A, 0x0E, 0x0C, 0x10, 0x18, 0x12, 0x30, 0x14,
                                                 0x60, 0x16, 0xC0, 0x18, 0x48, 0x1A, 0x10, 0x1C, 0x20, 0x1E};
  require(addr > 0x20);  // addr must be actual address (i.e. 0x40xx)

  // Ignore addresses outside range
  addr -= START_ADDR;
  if (addr > END_ADDR - START_ADDR)
    return;

  this->mRunUntil(time);

  if (addr < 0x14) {
    // Write to channel
    unsigned channel = addr / OSC_REGS;
    unsigned reg = addr % OSC_REGS;

    NesOsc *osc = this->mOscs[channel];
    osc->mRegs[reg] = data;
    osc->mRegWritten[reg] = true;

    if (channel == 4) {
      // handle DMC specially
      this->mDmc.writeRegister(reg, data);
      return;
    }

    if (reg == 3) {
      // load length counter
      if (this->m_oscEnables & (1 << channel))
        osc->mLengthCounter = pgm_read_byte(LENGTH_TABLE + (data >> 3));
      // reset square phase
      if (channel < 2)
        static_cast<NesSquare *>(osc)->phase = NesSquare::PHASE_RANGE - 1;
    }
    return;
  }

  if (addr == 0x15) {
    // Channel enables
    for (int i = OSCS_NUM; i--;)
      if (!((data >> i) & 1))
        this->mOscs[i]->mLengthCounter = 0;

    bool recalc_irq = this->mDmc.m_irqFlag;
    this->mDmc.m_irqFlag = false;

    int old_enables = this->m_oscEnables;
    this->m_oscEnables = data;
    if (!(data & 0x10)) {
      this->mDmc.m_nextIrq = NO_IRQ;
      recalc_irq = true;
    } else if (!(old_enables & 0x10)) {
      this->mDmc.start();  // dmc just enabled
    }

    if (recalc_irq)
      this->mIrqChanged();
    return;
  }

  if (addr == 0x17) {
    // Frame mode
    this->m_frameMode = data;

    bool irq_enabled = !(data & 0x40);
    this->m_irqFlag &= irq_enabled;
    this->m_nextIrq = NO_IRQ;

    // mode 1
    this->m_frameDelay = (this->m_frameDelay & 1);
    this->m_frame = 0;

    if (!(data & 0x80)) {
      // mode 0
      this->m_frame = 1;
      this->m_frameDelay += this->m_framePeriod;
      if (irq_enabled)
        this->m_nextIrq = time + this->m_frameDelay + this->m_framePeriod * 3 + 1;
    }

    this->mIrqChanged();
  }
}

uint8_t NesApu::ReadStatus(nes_time_t time) {
  this->mRunUntil(time - 1);

  uint8_t result = (this->mDmc.m_irqFlag << 7) | (this->m_irqFlag << 6);
  uint8_t mask = 1;

  for (auto osc : this->mOscs) {
    if (osc->mLengthCounter)
      result |= mask;
    mask <<= 1;
  }

  this->mRunUntil(time);

  if (this->m_irqFlag) {
    result |= 0x40;
    this->m_irqFlag = false;
    this->mIrqChanged();
  }

  // debug_printf( "%6d/%d Read $4015->$%02X\n", this->m_frameDelay, this->m_frame, result );

  return result;
}

}  // namespace nes
}  // namespace emu
}  // namespace gme
