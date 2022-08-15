// Sms_Snd_Emu 0.1.4. http://www.slack.net/~ant/

#include "SmsApu.h"

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
namespace sms {

// SmsOsc

SmsOsc::SmsOsc() {
  this->output = 0;
  this->outputs[0] = 0;  // always stays NULL
  this->outputs[1] = 0;
  this->outputs[2] = 0;
  this->outputs[3] = 0;
}

void SmsOsc::reset() {
  this->mDelay = 0;
  this->last_amp = 0;
  this->volume = 0;
  this->output_select = 3;
  this->output = this->outputs[3];
}

// SmsSquare

inline void SmsSquare::reset() {
  this->period = 0;
  this->phase = 0;
  SmsOsc::reset();
}

void SmsSquare::run(blip_time_t time, blip_time_t end_time) {
  if (!this->volume || this->period <= 128) {
    // ignore 16kHz and higher
    if (this->last_amp) {
      this->synth->offset(time, -this->last_amp, this->output);
      this->last_amp = 0;
    }
    time += this->mDelay;
    if (!this->period) {
      time = end_time;
    } else if (time < end_time) {
      // keep calculating phase
      int count = (end_time - time + this->period - 1) / this->period;
      this->phase = (this->phase + count) & 1;
      time += count * this->period;
    }
  } else {
    int amp = this->phase ? this->volume : -this->volume;
    {
      int delta = amp - this->last_amp;
      if (delta) {
        this->last_amp = amp;
        this->synth->offset(time, delta, this->output);
      }
    }

    time += this->mDelay;
    if (time < end_time) {
      int delta = amp * 2;
      do {
        delta = -delta;
        this->synth->offset(time, delta, this->output);
        time += this->period;
        this->phase ^= 1;
      } while (time < end_time);
      this->last_amp = this->phase ? this->volume : -this->volume;
    }
  }
  this->mDelay = time - end_time;
}

// SmsNoise

static const int NOISE_PERIODS[3] = {0x100, 0x200, 0x400};

inline void SmsNoise::reset() {
  this->period = &NOISE_PERIODS[0];
  this->shifter = 0x8000;
  this->feedback = 0x9000;
  SmsOsc::reset();
}

void SmsNoise::run(blip_time_t time, blip_time_t end_time) {
  int amp = this->volume;
  if (this->shifter & 1)
    amp = -amp;
  {
    int delta = amp - this->last_amp;
    if (delta) {
      this->last_amp = amp;
      this->synth.offset(time, delta, this->output);
    }
  }

  time += this->mDelay;
  if (!this->volume)
    time = end_time;

  if (time < end_time) {
    int delta = amp * 2;
    int period = *this->period * 2;
    if (!period)
      period = 16;

    do {
      int changed = shifter + 1;
      shifter = (feedback & -(shifter & 1)) ^ (shifter >> 1);
      if (changed & 2)  // true if bits 0 and 1 differ
      {
        delta = -delta;
        synth.offset(time, delta, output);
      }
      time += period;
    } while (time < end_time);

    this->last_amp = delta >> 1;
  }
  mDelay = time - end_time;
}

// SmsApu

SmsApu::SmsApu() {
  for (int i = 0; i < 3; i++) {
    m_squares[i].synth = &m_squareSynth;
    m_oscs[i] = &m_squares[i];
  }
  m_oscs[3] = &m_noise;

  setVolume(1.0);
  reset();
}

SmsApu::~SmsApu() {}

void SmsApu::setVolume(double vol) {
  vol *= 0.85 / (OSCS_NUM * 64 * 2);
  m_squareSynth.setVolume(vol);
  m_noise.synth.setVolume(vol);
}

void SmsApu::setTrebleEq(const BlipEq &eq) {
  m_squareSynth.setTrebleEq(eq);
  m_noise.synth.setTrebleEq(eq);
}

void SmsApu::setOscOutput(int index, BlipBuffer *center, BlipBuffer *left, BlipBuffer *right) {
  require((unsigned) index < OSCS_NUM);
  require((center && left && right) || (!center && !left && !right));
  SmsOsc &osc = *m_oscs[index];
  osc.outputs[1] = right;
  osc.outputs[2] = left;
  osc.outputs[3] = center;
  osc.output = osc.outputs[osc.output_select];
}

void SmsApu::SetOutput(BlipBuffer *center, BlipBuffer *left, BlipBuffer *right) {
  for (int i = 0; i < OSCS_NUM; i++)
    setOscOutput(i, center, left, right);
}

void SmsApu::reset(unsigned feedback, int noise_width) {
  m_lastTime = 0;
  m_latch = 0;

  if (!feedback || !noise_width) {
    feedback = 0x0009;
    noise_width = 16;
  }
  // convert to "Galios configuration"
  m_loopedFeedback = 1 << (noise_width - 1);
  m_noiseFeedback = 0;
  while (noise_width--) {
    m_noiseFeedback = (m_noiseFeedback << 1) | (feedback & 1);
    feedback >>= 1;
  }

  m_squares[0].reset();
  m_squares[1].reset();
  m_squares[2].reset();
  m_noise.reset();
}

void SmsApu::run_until(blip_time_t end_time) {
  require(end_time >= m_lastTime);  // end_time must not be before previous time

  if (end_time > m_lastTime) {
    // run oscillators
    for (int i = 0; i < OSCS_NUM; ++i) {
      SmsOsc &osc = *m_oscs[i];
      if (osc.output) {
        osc.output->setModified();
        if (i < 3)
          m_squares[i].run(m_lastTime, end_time);
        else
          m_noise.run(m_lastTime, end_time);
      }
    }

    m_lastTime = end_time;
  }
}

void SmsApu::EndFrame(blip_time_t end_time) {
  if (end_time > m_lastTime)
    run_until(end_time);

  assert(m_lastTime >= end_time);
  m_lastTime -= end_time;
}

void SmsApu::writeGGStereo(blip_time_t time, int data) {
  require((unsigned) data <= 0xFF);

  run_until(time);

  for (int i = 0; i < OSCS_NUM; i++) {
    SmsOsc &osc = *m_oscs[i];
    int flags = data >> i;
    BlipBuffer *old_output = osc.output;
    osc.output_select = (flags >> 3 & 2) | (flags & 1);
    osc.output = osc.outputs[osc.output_select];
    if (osc.output != old_output && osc.last_amp) {
      if (old_output) {
        old_output->setModified();
        m_squareSynth.offset(time, -osc.last_amp, old_output);
      }
      osc.last_amp = 0;
    }
  }
}

// volumes [i] = 64 * pow( 1.26, 15 - i ) / pow( 1.26, 15 )
static unsigned char const volumes[16] = {64, 50, 39, 31, 24, 19, 15, 12, 9, 7, 5, 4, 3, 2, 1, 0};

void SmsApu::writeData(blip_time_t time, int data) {
  require((unsigned) data <= 0xFF);

  run_until(time);

  if (data & 0x80)
    m_latch = data;

  int index = (m_latch >> 5) & 3;
  if (m_latch & 0x10) {
    m_oscs[index]->volume = volumes[data & 15];
  } else if (index < 3) {
    SmsSquare &sq = m_squares[index];
    if (data & 0x80)
      sq.period = (sq.period & 0xFF00) | (data << 4 & 0x00FF);
    else
      sq.period = (sq.period & 0x00FF) | (data << 8 & 0x3F00);
  } else {
    int select = data & 3;
    if (select < 3)
      m_noise.period = &NOISE_PERIODS[select];
    else
      m_noise.period = &m_squares[2].period;

    m_noise.feedback = (data & 0x04) ? m_noiseFeedback : m_loopedFeedback;
    m_noise.shifter = 0x8000;
  }
}

}  // namespace sms
}  // namespace emu
}  // namespace gme
