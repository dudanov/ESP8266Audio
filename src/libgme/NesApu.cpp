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
  mTempo = 1.0;
  mDmc.mPrgReader = nullptr;
  mIRQNotifier = nullptr;

  SetOutput(NULL);
  SetVolume(1.0);
  Reset(false);
}

void NesApu::SetTrebleEq(const BlipEq &eq) {
  mSquareSynth.SetTrebleEq(eq);
  mTriangle.mSynth.SetTrebleEq(eq);
  mNoise.mSynth.SetTrebleEq(eq);
  mDmc.mSynth.SetTrebleEq(eq);
}

void NesApu::mEnableNonlinear(double v) {
  mDmc.mNonlinear = true;
  mSquareSynth.SetVolume(1.3 * 0.25751258 / 0.742467605 * 0.25 / AMP_RANGE * v);

  const double tnd = 0.48 / 202 * mNonlinearTndGain();
  mTriangle.mSynth.SetVolume(3.0 * tnd);
  mNoise.mSynth.SetVolume(2.0 * tnd);
  mDmc.mSynth.SetVolume(tnd);

  mSquare1.mLastAmp = 0;
  mSquare2.mLastAmp = 0;
  mTriangle.mLastAmp = 0;
  mNoise.mLastAmp = 0;
  mDmc.mLastAmp = 0;
}

void NesApu::SetVolume(double v) {
  mDmc.mNonlinear = false;
  mSquareSynth.SetVolume(0.1128 / AMP_RANGE * v);
  mTriangle.mSynth.SetVolume(0.12765 / AMP_RANGE * v);
  mNoise.mSynth.SetVolume(0.0741 / AMP_RANGE * v);
  mDmc.mSynth.SetVolume(0.42545 / 127 * v);
}

void NesApu::SetOutput(BlipBuffer *buffer) {
  for (auto osc : mOscs)
    osc->SetOutput(buffer);
}

void NesApu::SetTempo(double t) {
  mTempo = t;
  mFramePeriod = (mPalMode ? 8314 : 7458);
  if (t != 1.0)
    mFramePeriod = (int) (mFramePeriod / t) & ~1;  // must be even
}

void NesApu::Reset(bool pal_mode, int initial_dmc_dac) {
  mPalMode = pal_mode;
  SetTempo(mTempo);

  mSquare1.mReset();
  mSquare2.mReset();
  mTriangle.mReset();
  mNoise.mReset();
  mDmc.mReset();

  mLastTime = 0;
  mLastDmcTime = 0;
  mOscEnables = 0;
  mIRQFlag = false;
  mEarliestIRQ = NO_IRQ;
  mFrameDelay = 1;
  WriteRegister(0, 0x4017, 0x00);
  WriteRegister(0, 0x4015, 0x00);

  for (nes_addr_t addr = START_ADDR; addr <= 0x4013; addr++)
    WriteRegister(0, addr, (addr & 3) ? 0x00 : 0x10);

  mDmc.mDac = initial_dmc_dac;
  if (!mDmc.mNonlinear)
    mTriangle.mLastAmp = 15;
  if (!mDmc.mNonlinear)                // TODO: remove?
    mDmc.mLastAmp = initial_dmc_dac;  // prevent output transition
}

void NesApu::mIRQChanged() {
  nes_time_t new_irq = mDmc.mNextIRQ;
  if (mDmc.mIRQFlag | mIRQFlag) {
    new_irq = 0;
  } else if (new_irq > mNextIRQ) {
    new_irq = mNextIRQ;
  }

  if (new_irq != mEarliestIRQ) {
    mEarliestIRQ = new_irq;
    if (mIRQNotifier)
      mIRQNotifier(mIRQData);
  }
}

// frames

void NesApu::RunUntil(nes_time_t end_time) {
  require(end_time >= mLastDmcTime);
  if (end_time > NextDmcReadTime()) {
    nes_time_t start = mLastDmcTime;
    mLastDmcTime = end_time;
    mDmc.mRun(start, end_time);
  }
}

void NesApu::mRunUntil(nes_time_t end_time) {
  require(end_time >= mLastTime);

  if (end_time == mLastTime)
    return;

  if (mLastDmcTime < end_time) {
    nes_time_t start = mLastDmcTime;
    mLastDmcTime = end_time;
    mDmc.mRun(start, end_time);
  }

  while (true) {
    // earlier of next frame time or end time
    nes_time_t time = mLastTime + mFrameDelay;
    if (time > end_time)
      time = end_time;
    mFrameDelay -= time - mLastTime;

    // run oscs to present
    mSquare1.run(mLastTime, time);
    mSquare2.run(mLastTime, time);
    mTriangle.run(mLastTime, time);
    mNoise.run(mLastTime, time);
    mLastTime = time;

    if (time == end_time)
      break;  // no more frames to run

    // take frame-specific actions
    mFrameDelay = mFramePeriod;
    switch (mFrame++) {
      case 0:
        if (!(mFrameMode & 0xC0)) {
          mNextIRQ = time + mFramePeriod * 4 + 2;
          mIRQFlag = true;
        }
        // fall through
      case 2:
        // clock length and sweep on frames 0 and 2
        mSquare1.doLengthClock(0x20);
        mSquare2.doLengthClock(0x20);
        mNoise.doLengthClock(0x20);
        mTriangle.doLengthClock(0x80);  // different bit for halt flag on triangle

        mSquare1.doSweepClock(-1);
        mSquare2.doSweepClock(0);

        // frame 2 is slightly shorter in mode 1
        if (mPalMode && mFrame == 3)
          mFrameDelay -= 2;
        break;

      case 1:
        // frame 1 is slightly shorter in mode 0
        if (!mPalMode)
          mFrameDelay -= 2;
        break;

      case 3:
        mFrame = 0;

        // frame 3 is almost twice as long in mode 1
        if (mFrameMode & 0x80)
          mFrameDelay += mFramePeriod - (mPalMode ? 2 : 6);
        break;
    }

    // clock envelopes and linear counter every frame
    mTriangle.doLinearCounterClock();
    mSquare1.doEnvelopeClock();
    mSquare2.doEnvelopeClock();
    mNoise.doEnvelopeClock();
  }
}

template<class T> inline void zero_apu_osc(T *osc, nes_time_t time) {
  BlipBuffer *output = osc->mOutput;
  int last_amp = osc->mLastAmp;
  osc->mLastAmp = 0;
  if (output && last_amp)
    osc->mSynth.Offset(output, time, -last_amp);
}

void NesApu::EndFrame(nes_time_t end_time) {
  if (end_time > mLastTime)
    mRunUntil(end_time);

  if (mDmc.mNonlinear) {
    zero_apu_osc(&mSquare1, mLastTime);
    zero_apu_osc(&mSquare2, mLastTime);
    zero_apu_osc(&mTriangle, mLastTime);
    zero_apu_osc(&mNoise, mLastTime);
    zero_apu_osc(&mDmc, mLastTime);
  }

  // make times relative to new frame
  mLastTime -= end_time;
  require(mLastTime >= 0);

  mLastDmcTime -= end_time;
  require(mLastDmcTime >= 0);

  if (mNextIRQ != NO_IRQ) {
    mNextIRQ -= end_time;
    check(mNextIRQ >= 0);
  }
  if (mDmc.mNextIRQ != NO_IRQ) {
    mDmc.mNextIRQ -= end_time;
    check(mDmc.mNextIRQ >= 0);
  }
  if (mEarliestIRQ != NO_IRQ) {
    mEarliestIRQ -= end_time;
    if (mEarliestIRQ < 0)
      mEarliestIRQ = 0;
  }
}

// registers

void NesApu::WriteRegister(nes_time_t time, nes_addr_t addr, uint8_t data) {
  // Ignore addresses outside range
  addr -= START_ADDR;
  if (addr > END_ADDR - START_ADDR)
    return;

  mRunUntil(time);

  if (addr <= 0x13)
    return mWriteChannelReg(addr, data);
  if (addr == 0x15)
    return mWriteR4015(data);
  if (addr == 0x17)
    return mWriteR4017(time, data);
}

void NesApu::mWriteChannelReg(nes_addr_t addr, uint8_t data) {
  size_t channel = addr / OSC_REGS;
  size_t reg = addr % OSC_REGS;
  NesOsc *osc = mOscs[channel];

  osc->mRegs[reg] = data;
  osc->mRegWritten[reg] = true;

  // DMC channel?
  if (channel == 4)
    return mDmc.mWriteRegister(reg, data);

  if (reg == 3) {
    // load length counter
    if (mOscEnables & (1 << channel)) {
      static const uint8_t LENGTH_TABLE[32] PROGMEM = {
          0x0A, 0xFE, 0x14, 0x02, 0x28, 0x04, 0x50, 0x06, 0xA0, 0x08, 0x3C, 0x0A, 0x0E, 0x0C, 0x1A, 0x0E,
          0x0C, 0x10, 0x18, 0x12, 0x30, 0x14, 0x60, 0x16, 0xC0, 0x18, 0x48, 0x1A, 0x10, 0x1C, 0x20, 0x1E,
      };
      osc->mLengthCounter = pgm_read_byte(&LENGTH_TABLE[data >> 3]);
    }
    // reset square phase
    if (channel < 2)
      static_cast<NesSquare *>(osc)->phase = NesSquare::PHASE_RANGE - 1;
  }
}

void NesApu::mWriteR4015(uint8_t data) {
  // Channel enables
  for (int i = OSCS_NUM; i--;)
    if (!((data >> i) & 1))
      mOscs[i]->mLengthCounter = 0;

  bool recalc_irq = mDmc.mIRQFlag;
  mDmc.mIRQFlag = false;

  int old_enables = mOscEnables;
  mOscEnables = data;
  if (!(data & 0x10)) {
    mDmc.mNextIRQ = NO_IRQ;
    recalc_irq = true;
  } else if (!(old_enables & 0x10)) {
    mDmc.mStart();  // dmc just enabled
  }

  if (recalc_irq)
    mIRQChanged();
}

void NesApu::mWriteR4017(nes_time_t time, uint8_t data) {
  // Frame mode
  mFrameMode = data;

  bool irq_enabled = !(data & 0x40);
  mIRQFlag &= irq_enabled;
  mNextIRQ = NO_IRQ;

  // mode 1
  mFrameDelay = (mFrameDelay & 1);
  mFrame = 0;

  if (!(data & 0x80)) {
    // mode 0
    mFrame = 1;
    mFrameDelay += mFramePeriod;
    if (irq_enabled)
      mNextIRQ = time + mFrameDelay + mFramePeriod * 3 + 1;
  }
  mIRQChanged();
}

uint8_t NesApu::ReadStatus(nes_time_t time) {
  mRunUntil(time - 1);

  uint8_t result = (mDmc.mIRQFlag << 7) | (mIRQFlag << 6);
  uint8_t mask = 1;

  for (auto osc : mOscs) {
    if (osc->mLengthCounter)
      result |= mask;
    mask <<= 1;
  }

  mRunUntil(time);

  if (mIRQFlag) {
    result |= 0x40;
    mIRQFlag = false;
    mIRQChanged();
  }

  // debug_printf( "%6d/%d Read $4015->$%02X\n", mFrameDelay, mFrame, result );

  return result;
}

}  // namespace nes
}  // namespace emu
}  // namespace gme
