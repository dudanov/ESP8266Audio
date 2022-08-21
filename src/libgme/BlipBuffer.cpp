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
  this->mFactor = (blip_ulong_t) -1 / 2;
  this->mOffset = 0;
  this->mBuffer = 0;
  this->mBufferSize = 0;
  this->mSampleRate = 0;
  this->mReaderAccum = 0;
  this->mBassShift = 0;
  this->mClockRate = 0;
  this->mBassFreq = 16;
  this->mLength = 0;

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
  if (this->mBufferSize != SILENT_BUF_SIZE)
    free(this->mBuffer);
}

SilentBlipBuffer::SilentBlipBuffer() {
  this->mFactor = 0;
  this->mBuffer = this->m_buf;
  this->mBufferSize = SILENT_BUF_SIZE;
  memset(this->m_buf, 0,
         sizeof(this->m_buf));  // in case machine takes exception for signed overflow
}

void BlipBuffer::Clear(bool entire) {
  this->mOffset = 0;
  this->mReaderAccum = 0;
  // this->m_modified = 0;
  if (this->mBuffer) {
    long count = entire ? this->mBufferSize : this->SamplesAvailable();
    memset(this->mBuffer, 0, (count + BLIP_BUFFER_EXTRA) * sizeof(buf_t_));
  }
}

BlipBuffer::blargg_err_t BlipBuffer::SetSampleRate(long rate, int ms) {
  if (this->mBufferSize == SILENT_BUF_SIZE) {
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

  if (this->mBufferSize != new_size) {
    void *p = realloc(this->mBuffer, (new_size + BLIP_BUFFER_EXTRA) * sizeof(*this->mBuffer));
    if (!p)
      return "Out of memory";
    this->mBuffer = (buf_t_ *) p;
  }

  this->mBufferSize = new_size;
  assert(this->mBufferSize != SILENT_BUF_SIZE);

  // update things based on the sample rate
  this->mSampleRate = rate;
  this->mLength = new_size * 1000 / rate - 1;
  if (ms)
    assert(this->mLength == ms);  // ensure length is same as that passed in
  if (this->mClockRate)
    this->SetClockRate(this->mClockRate);
  this->SetBassFrequency(this->mBassFreq);
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
  this->mBassFreq = freq;
  int shift = 31;
  if (freq > 0) {
    shift = 13;
    long f = (freq << 16) / this->mSampleRate;
    while ((f >>= 1) && --shift) {
    }
  }
  this->mBassShift = shift;
}

void BlipBuffer::EndFrame(blip_time_t t) {
  this->mOffset += t * this->mFactor;
  assert(this->SamplesAvailable() <= (long) this->mBufferSize);  // time outside buffer length
}

void BlipBuffer::removeSilence(long count) {
  assert(count <= this->SamplesAvailable());  // tried to remove more samples than available
  this->mOffset -= (blip_resampled_time_t) count << BLIP_BUFFER_ACCURACY;
}

long BlipBuffer::countSamples(blip_time_t t) const {
  unsigned long last_sample = this->resampledTime(t) >> BLIP_BUFFER_ACCURACY;
  unsigned long first_sample = this->mOffset >> BLIP_BUFFER_ACCURACY;
  return (long) (last_sample - first_sample);
}

blip_time_t BlipBuffer::CountClocks(long count) const {
  if (!this->mFactor) {
    assert(0);  // sample rate and clock rates must be set first
    return 0;
  }

  if (count > this->mBufferSize)
    count = this->mBufferSize;
  blip_resampled_time_t time = (blip_resampled_time_t) count << BLIP_BUFFER_ACCURACY;
  return (blip_time_t)((time - this->mOffset + this->mFactor - 1) / this->mFactor);
}

void BlipBuffer::RemoveSamples(long count) {
  if (!count)
    return;
  this->removeSilence(count);
  // copy remaining samples to beginning and clear old samples
  long remain = this->SamplesAvailable() + BLIP_BUFFER_EXTRA;
  memmove(this->mBuffer, this->mBuffer + count, remain * sizeof(*this->mBuffer));
  memset(this->mBuffer + remain, 0, count * sizeof(*this->mBuffer));
}

// BlipSynthImpl

void BlipSynthFastImpl::setVolumeUnit(double new_unit) {
  this->deltaFactor = int(new_unit * (1L << BLIP_SAMPLE_BITS) + 0.5);
}

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
  if (this->mBufferSize == SILENT_BUF_SIZE || num == 0) {
    assert(0);
    return;
  }

  buf_t_ *out = this->mBuffer + (this->mOffset >> BLIP_BUFFER_ACCURACY) + BLIP_WIDEST_IMPULSE / 2;

  constexpr int SAMPLE_SHIFT = BLIP_SAMPLE_BITS - 16;
  blip_long_t prev = 0;
  do {
    blip_long_t s = static_cast<blip_long_t>(*in++) << SAMPLE_SHIFT;
    *out++ += s - prev;
    prev = s;
  } while (--num);
  *out -= prev;
}
