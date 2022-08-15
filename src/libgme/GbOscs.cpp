// Gb_Snd_Emu 0.1.5. http://www.slack.net/~ant/

#include "GbApu.h"
#include <cstring>

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
namespace gb {

// GbOsc

void GbOsc::reset() {
  this->m_delay = 0;
  this->m_lastAmp = 0;
  this->m_length = 0;
  this->mOutput = this->m_outputs[CHANNEL_CENTER];
}

void GbOsc::doLengthClock() {
  if ((this->m_regs[4] & GbOsc::LEN_ENABLED_MASK) && this->m_length)
    this->m_length--;
}

// GbEnv

void GbEnv::doEnvelopeClock() {
  if (this->m_envDelay && !--this->m_envDelay) {
    this->m_envDelay = m_regs[2] & 0b111;
    int v = this->m_volume - 1 + (this->m_regs[2] >> 2 & 2);
    if ((unsigned) v < 15)
      m_volume = v;
  }
}

bool GbEnv::writeRegister(int reg, int data) {
  switch (reg) {
    case 1:
      this->m_length = 64 - (this->m_regs[1] & 63);
      break;
    case 2:
      if (!(data >> 4))
        this->m_enabled = false;
      break;
    case 4:
      if (data & GbEnv::TRIGGER) {
        this->m_envDelay = this->m_regs[2] & 0b111;
        this->m_volume = this->m_regs[2] >> 4;
        this->m_enabled = true;
        if (this->m_length == 0)
          this->m_length = 64;
        return true;
      }
  }
  return false;
}

// GbSquare

void GbSquare::reset() {
  this->m_phase = 0;
  this->m_sweepFrequency = 0;
  this->m_sweepDelay = 0;
  GbEnv::reset();
}

void GbSquare::doSweepClock() {
  const uint8_t period = this->m_getSweepPeriod();
  if (period && this->m_sweepDelay && !--this->m_sweepDelay) {
    this->m_sweepDelay = period;
    this->m_regs[3] = this->m_sweepFrequency % 256;
    this->m_regs[4] = (this->m_regs[4] & 0b11111000) | (this->m_sweepFrequency / 256);
    int offset = this->m_sweepFrequency >> this->m_getShift();
    if (this->m_regs[0] & 0x08)
      offset = -offset;
    this->m_sweepFrequency += offset;
    if (this->m_sweepFrequency < 0) {
      this->m_sweepFrequency = 0;
    } else if (this->m_sweepFrequency >= 2048) {
      this->m_sweepFrequency = 2048;  // silence sound immediately
      this->m_sweepDelay = 0;         // don't modify channel frequency any further
    }
  }
}

void GbSquare::run(blip_time_t time, blip_time_t end_time) {
  static const uint8_t DUTY_TABLE[4] = {1, 2, 4, 6};
  bool playing = this->m_enabled && this->m_volume && (!(this->m_regs[4] & GbOsc::LEN_ENABLED_MASK) || this->m_length);

  if (this->m_sweepFrequency == 2048)
    playing = false;

  const int duty = DUTY_TABLE[this->m_regs[1] >> 6];
  int amp = playing ? this->m_volume : 0;

  if (this->m_phase >= duty)
    amp = -amp;

  int frequency = this->getFrequency();
  if (unsigned(frequency - 1) > 2040)  // frequency < 1 || frequency > 2041
  {
    // really high frequency results in DC at half volume
    amp = this->m_volume >> 1;
    playing = false;
  }

  {
    int delta = amp - this->m_lastAmp;
    if (delta) {
      this->m_lastAmp = amp;
      this->synth->offset(time, delta, this->mOutput);
    }
  }

  time += this->m_delay;
  if (!playing)
    time = end_time;

  if (time < end_time) {
    const int period = (2048 - frequency) * 4;
    int delta = amp * 2;
    do {
      this->m_phase = (this->m_phase + 1) & 7;
      if (this->m_phase == 0 || this->m_phase == duty) {
        delta = -delta;
        this->synth->offset(time, delta, this->mOutput);
      }
      time += period;
    } while (time < end_time);

    this->m_lastAmp = delta >> 1;
  }
  this->m_delay = time - end_time;
}

// GbNoise

void GbNoise::run(blip_time_t time, blip_time_t end_time) {
  bool playing = this->m_enabled && this->m_volume && (!(this->m_regs[4] & GbOsc::LEN_ENABLED_MASK) || this->m_length);
  int amp = playing ? this->m_volume : 0;
  int tap = 13 - (this->m_regs[3] & 8);
  if (this->m_lfsr >> tap & 2)
    amp = -amp;

  {
    int delta = amp - this->m_lastAmp;
    if (delta) {
      this->m_lastAmp = amp;
      this->synth->offset(time, delta, this->mOutput);
    }
  }

  time += this->m_delay;
  if (!playing)
    time = end_time;

  if (time < end_time) {
    static const uint8_t DIVISOR_TABLE[8] = {8, 16, 32, 48, 64, 80, 96, 112};
    int period = DIVISOR_TABLE[this->m_regs[3] & 7] << (this->m_regs[3] >> 4);

    // keep parallel resampled time to eliminate time conversion in the loop
    const blip_resampled_time_t resampled_period = this->mOutput->resampledDuration(period);
    blip_resampled_time_t resampled_time = this->mOutput->resampledTime(time);
    int delta = amp * 2;

    do {
      unsigned changed = (this->m_lfsr >> tap) + 1;
      time += period;
      this->m_lfsr <<= 1;
      if (changed & 2) {
        delta = -delta;
        this->m_lfsr |= 1;
        this->synth->offsetResampled(resampled_time, delta, this->mOutput);
      }
      resampled_time += resampled_period;
    } while (time < end_time);

    this->m_lastAmp = delta >> 1;
  }
  this->m_delay = time - end_time;
}

// GbWave

void GbWave::run(blip_time_t time, blip_time_t end_time) {
  bool playing = this->m_enabled && this->m_volume && (!(this->m_regs[4] & GbOsc::LEN_ENABLED_MASK) || this->m_length);
  int volume_shift = (this->m_volume - 1) & 7;  // volume = 0 causes shift = 7
  int frequency;
  {
    int amp = playing ? (this->m_getSample() >> volume_shift) : 0;

    frequency = this->getFrequency();
    if (unsigned(frequency - 1) > 2044)  // frequency < 1 || frequency > 2045
    {
      amp = playing ? (30 >> volume_shift) : 0;
      playing = false;
    }

    int delta = amp - this->m_lastAmp;
    if (delta) {
      this->m_lastAmp = amp;
      synth->offset(time, delta, this->mOutput);
    }
  }

  time += this->m_delay;
  if (!playing)
    time = end_time;

  if (time < end_time) {
    const auto period = (2048 - frequency) * 2;
    do {
      int amp = this->m_advNextSample() >> volume_shift;
      int delta = amp - this->m_lastAmp;
      if (delta) {
        this->m_lastAmp = amp;
        this->synth->offset(time, delta, this->mOutput);
      }
      time += period;
    } while (time < end_time);
  }
  this->m_delay = time - end_time;
}

bool GbWave::writeRegister(int reg, int data) {
  switch (reg) {
    case 0:
      if (!(data & 0x80))
        this->m_enabled = false;
      break;
    case 1:
      this->m_length = 256 - this->m_regs[1];
      break;
    case 2:
      this->m_volume = data >> 5 & 3;
      break;
    case 4:
      if (data & TRIGGER & this->m_regs[0]) {
        this->mReset();
        this->m_enabled = true;
        if (this->m_length == 0)
          this->m_length = 256;
        return true;
      }
  }
  return false;
}

}  // namespace gb
}  // namespace emu
}  // namespace gme
