// Gb_Snd_Emu 0.1.5. http://www.slack.net/~ant/

#include "GbApu.h"
#include <algorithm>
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

GbApu::GbApu() {
  this->m_square1.synth = &this->m_squareSynth;
  this->m_square2.synth = &this->m_squareSynth;
  this->m_wave.synth = &this->m_otherSynth;
  this->m_noise.synth = &this->m_otherSynth;

  this->m_oscs[0] = &this->m_square1;
  this->m_oscs[1] = &this->m_square2;
  this->m_oscs[2] = &this->m_wave;
  this->m_oscs[3] = &this->m_noise;

  uint8_t *ptr = this->m_regs.data();
  for (auto osc : this->m_oscs) {
    osc->m_regs = ptr;
    osc->setOutputs(nullptr, nullptr, nullptr);
    ptr += 5;
  }

  this->setTempo(1.0);
  this->setVolume(1.0);
  this->reset();
}

void GbApu::setTrebleEq(const BlipEq &eq) {
  this->m_squareSynth.setTrebleEq(eq);
  this->m_otherSynth.setTrebleEq(eq);
}

void GbApu::setOscOutput(int idx, BlipBuffer *center, BlipBuffer *left, BlipBuffer *right) {
  require((unsigned) idx < OSC_NUM);
  require((center && left && right) || (!center && !left && !right));
  this->m_oscs[idx]->setOutputs(center, left, right);
}

void GbApu::setOutput(BlipBuffer *center, BlipBuffer *left, BlipBuffer *right) {
  for (int i = 0; i < OSC_NUM; i++)
    this->setOscOutput(i, center, left, right);
}

void GbApu::m_updateVolume() {
  // TODO: doesn't handle differing left/right global volume (support would
  // require modification to all oscillator code)
  uint8_t data = this->m_getRegister(NR50);
  double vol = (std::max(data & 7, data >> 4 & 7) + 1) * this->m_volumeUnit;
  this->m_squareSynth.setVolume(vol);
  this->m_otherSynth.setVolume(vol);
}

void GbApu::setTempo(double t) {
  this->m_framePeriod = 4194304 / 256;  // 256 Hz
  if (t != 1.0)
    this->m_framePeriod = blip_time_t(this->m_framePeriod / t);
}

void GbApu::reset() {
  this->m_nextFrameTime = 0;
  this->m_lastTime = 0;
  this->m_frameCounter = 0;

  this->m_square1.reset();
  this->m_square2.reset();
  this->m_wave.reset();
  this->m_noise.reset();
  this->m_noise.reset();
  this->m_wave.reset();

  // avoid click at beginning
  this->m_setRegister(NR50, 0x77);
  this->m_updateVolume();

  this->m_setRegister(NR52, 0x01);  // force power
  this->writeRegister(0, NR52, 0x00);
}

void GbApu::m_runUntil(blip_time_t end_time) {
  require(end_time >= this->m_lastTime);  // end_time must not be before previous time
  if (end_time == this->m_lastTime)
    return;

  while (true) {
    blip_time_t time = this->m_nextFrameTime;
    if (time > end_time)
      time = end_time;

    // run oscillators
    for (auto osc : this->m_oscs) {
      if (osc->m_enabled) {
        osc->m_output->setModified();  // TODO: misses optimization opportunities?
        osc->run(this->m_lastTime, time);
      }
    }
    this->m_lastTime = time;

    if (time == end_time)
      break;

    this->m_nextFrameTime += this->m_framePeriod;

    // 256 Hz
    this->m_square1.doLengthClock();
    this->m_square2.doLengthClock();
    this->m_wave.doLengthClock();
    this->m_noise.doLengthClock();

    this->m_frameCounter = (this->m_frameCounter + 1) % 4;

    // 64 Hz
    if (this->m_frameCounter == 0) {
      this->m_square1.doEnvelopeClock();
      this->m_square2.doEnvelopeClock();
      this->m_noise.doEnvelopeClock();
    }

    // 128 Hz
    if (this->m_frameCounter & 1)
      this->m_square1.doSweepClock();
  }
}

void GbApu::endFrame(blip_time_t end_time) {
  if (end_time > this->m_lastTime)
    this->m_runUntil(end_time);

  assert(this->m_nextFrameTime >= end_time);
  this->m_nextFrameTime -= end_time;

  assert(this->m_lastTime >= end_time);
  this->m_lastTime -= end_time;
}

void GbApu::m_writeNR50(blip_time_t time) {
  // global volume
  // return all oscs to 0
  for (auto osc : this->m_oscs) {
    int amp = osc->m_lastAmp;
    osc->m_lastAmp = 0;
    if (amp && osc->m_enabled && osc->m_output != nullptr)
      this->m_otherSynth.offset(time, -amp, osc->m_output);
  }

  if (this->m_wave.m_outputs[3])
    this->m_otherSynth.offset(time, 30, this->m_wave.m_outputs[3]);

  this->m_updateVolume();

  if (this->m_wave.m_outputs[3])
    this->m_otherSynth.offset(time, -30, this->m_wave.m_outputs[3]);

  // oscs will update with new amplitude when next run
}

void GbApu::m_writeNR5152(blip_time_t time) {
  uint8_t channels = 0;
  bool mask = false;
  if (this->m_getRegister(NR52) & 0x80) {
    channels = this->m_getRegister(NR51);
    mask = true;
  }
  // left/right assignments
  for (auto osc : this->m_oscs) {
    auto oldOutput = osc->m_output;
    osc->m_output = osc->m_outputs[(channels >> 3 & 0b10) | (channels & 0b01)];
    channels >>= 1;
    osc->m_enabled &= mask;
    if (osc->m_output == oldOutput)
      continue;
    // current output has been changed. zero old output.
    int amp = osc->m_lastAmp;
    osc->m_lastAmp = 0;
    if (amp && oldOutput != nullptr)
      this->m_otherSynth.offset(time, -amp, oldOutput);
  }
}

void GbApu::writeRegister(blip_time_t time, unsigned address, uint8_t data) {
  unsigned reg = address - START_ADDR;
  if (reg >= REGS_NUM)
    return;

  this->m_runUntil(time);

  const uint8_t oldData = this->m_getRegister(address);
  this->m_setRegister(address, data);

  // Write to oscillators control registers
  if (address < NR50)
    return this->m_writeOsc(reg / 5, reg % 5, data);

  // Write pair of 4-bit samples to wave table of wave oscillator
  if (address >= 0xFF30)
    return this->m_wave.writeSamples(address, data);

  switch (address) {
    case NR50:
      if (data != oldData)
        this->m_writeNR50(time);
      return;
    case NR52:
      if (data != oldData && !(data & 0x80)) {
        static const uint8_t POWERUP_REGS[0x20] = {0x80, 0x3F, 0x00, 0xFF, 0xBF,  // square 1
                                                   0xFF, 0x3F, 0x00, 0xFF, 0xBF,  // square 2
                                                   0x7F, 0xFF, 0x9F, 0xFF, 0xBF,  // wave
                                                   0xFF, 0xFF, 0x00, 0x00, 0xBF,  // noise
                                                   0x00,                          // left/right enables
                                                   0x77,                          // master volume
                                                   0x80,                          // power
                                                   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        for (unsigned i = 0; i < sizeof(POWERUP_REGS); i++)
          if (i != NR52 - START_ADDR)
            this->writeRegister(time, START_ADDR + i, POWERUP_REGS[i]);
      }
      /* FALLTHRU */
    case NR51:
      this->m_writeNR5152(time);
  }
}

uint8_t GbApu::readRegister(blip_time_t time, unsigned address) {
  this->m_runUntil(time);

  //  require(address <= END_ADDRESS);
  uint8_t data = this->m_getRegister(address);
  if (address != NR52)
    return data;

  data = (data & 0x80) | 0x70;
  uint8_t mask = 1;
  for (auto osc : this->m_oscs) {
    if (osc->m_enabled && (osc->m_length || !(osc->m_regs[4] & GbOsc::LEN_ENABLED_MASK)))
      data |= mask;
    mask <<= 1;
  }
  return data;
}

void GbApu::m_writeOsc(unsigned idx, unsigned reg, uint8_t data) {
  if (this->m_oscs[idx]->writeRegister(reg, data) && idx == 0)
    m_square1.onTrigger();
}

}  // namespace gb
}  // namespace emu
}  // namespace gme
