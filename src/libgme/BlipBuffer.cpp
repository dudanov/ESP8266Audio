// BlipBuffer 0.4.1. http://www.slack.net/~ant/

#include "BlipBuffer.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

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

#ifdef BLARGG_ENABLE_OPTIMIZER
#include BLARGG_ENABLE_OPTIMIZER
#endif

static const int SILENT_BUF_SIZE = 1;  // size used for SilentBlipBuffer

BlipBuffer::BlipBuffer() {
  this->m_factor = (blip_ulong_t) -1 / 2;
  this->m_offset = 0;
  this->m_buffer = 0;
  this->m_bufferSize = 0;
  this->mSampleRate = 0;
  this->m_readerAccum = 0;
  this->m_bassShift = 0;
  this->m_clockRate = 0;
  this->m_bassFreq = 16;
  this->m_length = 0;

// assumptions code makes about implementation-defined features
#ifndef NDEBUG
  // right shift of negative value preserves sign
  buf_t_ i = -0x7FFFFFFE;
  assert((i >> 1) == -0x3FFFFFFF);

  // casting to short truncates to 16 bits and sign-extends
  i = 0x18000;
  assert((short) i == -0x8000);
#endif
}

BlipBuffer::~BlipBuffer() {
  if (this->m_bufferSize != SILENT_BUF_SIZE)
    free(this->m_buffer);
}

SilentBlipBuffer::SilentBlipBuffer() {
  this->m_factor = 0;
  this->m_buffer = this->m_buf;
  this->m_bufferSize = SILENT_BUF_SIZE;
  memset(this->m_buf, 0,
         sizeof(this->m_buf));  // in case machine takes exception for signed overflow
}

void BlipBuffer::Clear(bool entire) {
  this->m_offset = 0;
  this->m_readerAccum = 0;
  // this->m_modified = 0;
  if (this->m_buffer) {
    long count = entire ? this->m_bufferSize : this->SamplesAvailable();
    memset(this->m_buffer, 0, (count + BLIP_BUFFER_EXTRA) * sizeof(buf_t_));
  }
}

BlipBuffer::blargg_err_t BlipBuffer::SetSampleRate(long rate, int ms) {
  if (this->m_bufferSize == SILENT_BUF_SIZE) {
    assert(0);
    return "Internal (tried to resize SilentBlipBuffer)";
  }

  // start with maximum length that resampled time can represent
  long new_size = (UINT_MAX >> BLIP_BUFFER_ACCURACY) - BLIP_BUFFER_EXTRA - 64;
  if (ms != BLIP_MAX_LENGTH) {
    long s = (rate * (ms + 1) + 999) / 1000;
    if (s < new_size)
      new_size = s;
    else
      assert(0);  // fails if requested buffer length exceeds limit
  }

  if (this->m_bufferSize != new_size) {
    void *p = realloc(this->m_buffer, (new_size + BLIP_BUFFER_EXTRA) * sizeof(*this->m_buffer));
    if (!p)
      return "Out of memory";
    this->m_buffer = (buf_t_ *) p;
  }

  this->m_bufferSize = new_size;
  assert(this->m_bufferSize != SILENT_BUF_SIZE);

  // update things based on the sample rate
  this->mSampleRate = rate;
  this->m_length = new_size * 1000 / rate - 1;
  if (ms)
    assert(this->m_length == ms);  // ensure length is same as that passed in
  if (this->m_clockRate)
    this->SetClockRate(this->m_clockRate);
  this->SetBassFrequency(this->m_bassFreq);
  this->Clear();

  return nullptr;  // success
}

blip_resampled_time_t BlipBuffer::clockRateFactor(long rate) const {
  double ratio = (double) this->mSampleRate / rate;
  blip_long_t factor = (blip_long_t) floor(ratio * (1L << BLIP_BUFFER_ACCURACY) + 0.5);
  assert(factor > 0 || !this->mSampleRate);  // fails if clock/output ratio is too large
  return (blip_resampled_time_t) factor;
}

void BlipBuffer::SetBassFrequency(int freq) {
  this->m_bassFreq = freq;
  int shift = 31;
  if (freq > 0) {
    shift = 13;
    long f = (freq << 16) / this->mSampleRate;
    while ((f >>= 1) && --shift) {
    }
  }
  this->m_bassShift = shift;
}

void BlipBuffer::EndFrame(blip_time_t t) {
  this->m_offset += t * this->m_factor;
  assert(this->SamplesAvailable() <= (long) this->m_bufferSize);  // time outside buffer length
}

void BlipBuffer::removeSilence(long count) {
  assert(count <= this->SamplesAvailable());  // tried to remove more samples than available
  this->m_offset -= (blip_resampled_time_t) count << BLIP_BUFFER_ACCURACY;
}

long BlipBuffer::countSamples(blip_time_t t) const {
  unsigned long last_sample = this->resampledTime(t) >> BLIP_BUFFER_ACCURACY;
  unsigned long first_sample = this->m_offset >> BLIP_BUFFER_ACCURACY;
  return (long) (last_sample - first_sample);
}

blip_time_t BlipBuffer::CountClocks(long count) const {
  if (!this->m_factor) {
    assert(0);  // sample rate and clock rates must be set first
    return 0;
  }

  if (count > this->m_bufferSize)
    count = this->m_bufferSize;
  blip_resampled_time_t time = (blip_resampled_time_t) count << BLIP_BUFFER_ACCURACY;
  return (blip_time_t)((time - this->m_offset + this->m_factor - 1) / this->m_factor);
}

void BlipBuffer::RemoveSamples(long count) {
  if (!count)
    return;
  this->removeSilence(count);
  // copy remaining samples to beginning and clear old samples
  long remain = this->SamplesAvailable() + BLIP_BUFFER_EXTRA;
  memmove(this->m_buffer, this->m_buffer + count, remain * sizeof(*this->m_buffer));
  memset(this->m_buffer + remain, 0, count * sizeof(*this->m_buffer));
}

// BlipSynthImpl

void BlipSynthFastImpl::setVolumeUnit(double new_unit) {
  this->deltaFactor = int(new_unit * (1L << BLIP_SAMPLE_BITS) + 0.5);
}

#if !BLIP_BUFFER_FAST

#undef PI
#define PI 3.1415926535897932384626433832795029

static void gen_sinc(float *out, int count, double oversample, double treble, double cutoff) {
  if (cutoff >= 0.999)
    cutoff = 0.999;

  if (treble < -300.0)
    treble = -300.0;
  if (treble > 5.0)
    treble = 5.0;

  double const maxh = 4096.0;
  double const rolloff = pow(10.0, 1.0 / (maxh * 20.0) * treble / (1.0 - cutoff));
  double const pow_a_n = pow(rolloff, maxh - maxh * cutoff);
  double const to_angle = PI / 2 / maxh / oversample;
  for (int i = 0; i < count; i++) {
    double angle = ((i - count) * 2 + 1) * to_angle;
    double angle_maxh = angle * maxh;
    double angle_maxh_mid = angle_maxh * cutoff;

    double y = maxh;

    // 0 to Fs/2*cutoff, flat
    if (angle_maxh_mid)  // unstable at t=0
      y *= sin(angle_maxh_mid) / angle_maxh_mid;

    // Fs/2*cutoff to Fs/2, logarithmic rolloff
    double cosa = cos(angle);
    double den = 1 + rolloff * (rolloff - cosa - cosa);

    // Becomes unstable when rolloff is near 1.0 and t is near 0,
    // which is the only time den becomes small
    if (den > 1e-13) {
      double num = (cos(angle_maxh - angle) * rolloff - cos(angle_maxh)) * pow_a_n -
                   cos(angle_maxh_mid - angle) * rolloff + cos(angle_maxh_mid);

      y = y * cutoff + num / den;
    }

    out[i] = (float) y;
  }
}

void BlipEq::m_generate(float *out, int count) const {
  // lower cutoff freq for narrow kernels with their wider transition band
  // (8 points->1.49, 16 points->1.15)
  double oversample = BLIP_RES * 2.25 / count + 0.85;
  double half_rate = this->mSampleRate * 0.5;
  if (this->m_cutoffFreq)
    oversample = half_rate / this->m_cutoffFreq;
  double cutoff = this->m_rolloffFreq * oversample / half_rate;

  gen_sinc(out, count, BLIP_RES * oversample, this->m_treble, cutoff);

  // apply (half of) hamming window
  double to_fraction = PI / (count - 1);
  for (int i = count; i--;)
    out[i] *= 0.54f - 0.46f * (float) cos(i * to_fraction);
}

void BlipSynthImpl::adjust_impulse() {
  // sum pairs for each phase and add error correction to end of first half
  int const size = m_impulsesSize();
  for (int p = BLIP_RES; p-- >= BLIP_RES / 2;) {
    int p2 = BLIP_RES - 2 - p;
    long error = this->m_kernelUnit;
    for (int i = 1; i < size; i += BLIP_RES) {
      error -= this->m_impulses[i + p];
      error -= this->m_impulses[i + p2];
    }
    if (p == p2)
      error /= 2;  // phase = 0.5 impulse uses same half for both sides
    this->m_impulses[size - BLIP_RES + p] += (short) error;
    // printf( "error: %ld\n", error );
  }

  // for ( int i = BLIP_RES; i--; printf( "\n" ) )
  //  for ( int j = 0; j < width / 2; j++ )
  //      printf( "%5ld,", this->m_impulses [j * BLIP_RES + i + 1] );
}

void BlipSynthImpl::setTrebleEq(BlipEq const &eq) {
  float fimpulse[BLIP_RES / 2 * (BLIP_WIDEST_IMPULSE - 1) + BLIP_RES * 2];

  int const half_size = BLIP_RES / 2 * (this->m_width - 1);
  eq.m_generate(&fimpulse[BLIP_RES], half_size);

  int i;

  // need mirror slightly past center for calculation
  for (i = BLIP_RES; i--;)
    fimpulse[BLIP_RES + half_size + i] = fimpulse[BLIP_RES + half_size - 1 - i];

  // starts at 0
  for (i = 0; i < BLIP_RES; i++)
    fimpulse[i] = 0.0f;

  // find rescale factor
  double total = 0.0;
  for (i = 0; i < half_size; i++)
    total += fimpulse[BLIP_RES + i];

  // double const base_unit = 44800.0 - 128 * 18; // allows treble up to +0 dB
  // double const base_unit = 37888.0; // allows treble to +5 dB
  double const base_unit = 32768.0;  // necessary for BLIP_UNSCALED to work
  double rescale = base_unit / 2 / total;
  this->m_kernelUnit = (long) base_unit;

  // integrate, first difference, rescale, convert to int
  double sum = 0.0;
  double next = 0.0;
  int const impulses_size = this->m_impulsesSize();
  for (i = 0; i < impulses_size; i++) {
    this->m_impulses[i] = (short) floor((next - sum) * rescale + 0.5);
    sum += fimpulse[i];
    next += fimpulse[i + BLIP_RES];
  }
  adjust_impulse();

  // volume might require rescaling
  double vol = this->m_volumeUnit;
  if (vol) {
    this->m_volumeUnit = 0.0;
    setVolumeUnit(vol);
  }
}

void BlipSynthImpl::setVolumeUnit(double new_unit) {
  if (new_unit != this->m_volumeUnit) {
    // use default eq if it hasn't been set yet
    if (!this->m_kernelUnit)
      setTrebleEq(-8.0);

    this->m_volumeUnit = new_unit;
    double factor = new_unit * (1L << BLIP_SAMPLE_BITS) / this->m_kernelUnit;

    if (factor > 0.0) {
      int shift = 0;

      // if unit is really small, might need to attenuate kernel
      while (factor < 2.0) {
        shift++;
        factor *= 2.0;
      }

      if (shift) {
        this->m_kernelUnit >>= shift;
        assert(this->m_kernelUnit > 0);  // fails if volume unit is too low

        // keep values positive to avoid round-towards-zero of
        // sign-preserving right shift for negative values
        long offset = 0x8000 + (1 << (shift - 1));
        long offset2 = 0x8000 >> shift;
        for (int i = m_impulsesSize(); i--;)
          this->m_impulses[i] = (short) (((this->m_impulses[i] + offset) >> shift) - offset2);
        adjust_impulse();
      }
    }
    deltaFactor = (int) floor(factor + 0.5);
    // printf( "deltaFactor: %d, m_kernelUnit: %d\n", deltaFactor,
    // this->m_kernelUnit );
  }
}
#endif

long BlipBuffer::ReadSamples(blip_sample_t *out, long max_samples, int stereo) {
  long count = this->SamplesAvailable();
  if (count > max_samples)
    count = max_samples;
  if (!count)
    return 0;
  const int bass = BLIP_READER_BASS(*this);
  BLIP_READER_BEGIN(reader, *this);

  if (stereo) {
    for (blip_long_t n = count; n; --n) {
      blip_long_t s = BLIP_READER_READ(reader);
      if ((blip_sample_t) s != s)
        s = 0x7FFF - (s >> 24);
      *out = (blip_sample_t) s;
      out += 2;
      BLIP_READER_NEXT(reader, bass);
    }
  } else {
    for (blip_long_t n = count; n; --n) {
      blip_long_t s = BLIP_READER_READ(reader);
      if ((blip_sample_t) s != s)
        s = 0x7FFF - (s >> 24);
      *out++ = (blip_sample_t) s;
      BLIP_READER_NEXT(reader, bass);
    }
  }
  BLIP_READER_END(reader, *this);
  this->RemoveSamples(count);
  return count;
}

void BlipBuffer::mixSamples(const blip_sample_t *in, long num) {
  if (this->m_bufferSize == SILENT_BUF_SIZE || num == 0) {
    assert(0);
    return;
  }

  buf_t_ *out = this->m_buffer + (this->m_offset >> BLIP_BUFFER_ACCURACY) + BLIP_WIDEST_IMPULSE / 2;

  constexpr int SAMPLE_SHIFT = BLIP_SAMPLE_BITS - 16;
  blip_long_t prev = 0;
  do {
    blip_long_t s = static_cast<blip_long_t>(*in++) << SAMPLE_SHIFT;
    *out++ += s - prev;
    prev = s;
  } while (--num);
  *out -= prev;
}
