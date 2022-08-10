// BlipBuffer 0.4.1. http://www.slack.net/~ant/

#include "MultiBuffer.h"

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

#ifdef BLARGG_ENABLE_OPTIMIZER
#include BLARGG_ENABLE_OPTIMIZER
#endif

// MonoBuffer

blargg_err_t MonoBuffer::setSampleRate(long rate, int msec) {
  RETURN_ERR(m_buf.setSampleRate(rate, msec));
  return MultiBuffer::setSampleRate(m_buf.getSampleRate(), m_buf.getLength());
}

// StereoBuffer

blargg_err_t StereoBuffer::setSampleRate(long rate, int msec) {
  for (auto &buf : this->m_bufs)
    RETURN_ERR(buf.setSampleRate(rate, msec));
  return MultiBuffer::setSampleRate(m_bufs[0].getSampleRate(), m_bufs[0].getLength());
}

void StereoBuffer::setClockRate(long rate) {
  for (auto &buf : this->m_bufs)
    buf.setClockRate(rate);
}

void StereoBuffer::setBassFreq(int bass) {
  for (auto &buf : this->m_bufs)
    buf.setBassFrequency(bass);
}

void StereoBuffer::clear() {
  this->m_stereoAdded = 0;
  this->m_wasStereo = false;
  for (auto &buf : this->m_bufs)
    buf.clear();
}

void StereoBuffer::endFrame(blip_time_t clock_count) {
  this->m_stereoAdded = 0;
  unsigned mask = 1;
  for (auto &buf : this->m_bufs) {
    if (buf.clearModified())
      this->m_stereoAdded |= mask;
    buf.end_frame(clock_count);
    mask <<= 1;
  }
}

long StereoBuffer::readSamples(blip_sample_t *out, long count) {
  require(!(count & 1));  // count must be even
  count = (unsigned) count / 2;

  long avail = this->m_bufs[0].samplesAvailable();
  if (count > avail)
    count = avail;
  if (count) {
    int bufs_used = this->m_stereoAdded | this->m_wasStereo;
    // debug_printf( "%X\n", bufs_used );
    if (bufs_used <= 1) {
      this->m_mixMono(out, count);
      this->m_bufs[0].removeSamples(count);
      this->m_bufs[1].removeSilence(count);
      this->m_bufs[2].removeSilence(count);
    } else if (bufs_used & 1) {
      this->m_mixStereo(out, count);
      this->m_bufs[0].removeSamples(count);
      this->m_bufs[1].removeSamples(count);
      this->m_bufs[2].removeSamples(count);
    } else {
      this->m_mixStereoNoCenter(out, count);
      this->m_bufs[0].removeSilence(count);
      this->m_bufs[1].removeSamples(count);
      this->m_bufs[2].removeSamples(count);
    }

    // to do: this might miss opportunities for optimization
    if (!this->m_bufs[0].samplesAvailable()) {
      this->m_wasStereo = this->m_stereoAdded;
      this->m_stereoAdded = 0;
    }
  }
  return count * 2;
}

void StereoBuffer::m_mixStereo(blip_sample_t *out_, blargg_long count) {
  blip_sample_t *out = out_;
  int const bass = BLIP_READER_BASS(this->m_bufs[1]);
  BLIP_READER_BEGIN(center, this->m_bufs[0]);
  BLIP_READER_BEGIN(left, this->m_bufs[1]);
  BLIP_READER_BEGIN(right, this->m_bufs[2]);

  for (; count; --count) {
    int c = BLIP_READER_READ(center);
    blargg_long l = c + BLIP_READER_READ(left);
    blargg_long r = c + BLIP_READER_READ(right);
    if ((int16_t) l != l)
      l = 0x7FFF - (l >> 24);

    BLIP_READER_NEXT(center, bass);
    if ((int16_t) r != r)
      r = 0x7FFF - (r >> 24);

    BLIP_READER_NEXT(left, bass);
    BLIP_READER_NEXT(right, bass);

    out[0] = l;
    out[1] = r;
    out += 2;
  }

  BLIP_READER_END(center, m_bufs[0]);
  BLIP_READER_END(left, m_bufs[1]);
  BLIP_READER_END(right, m_bufs[2]);
}

void StereoBuffer::m_mixStereoNoCenter(blip_sample_t *out_, blargg_long count) {
  blip_sample_t *out = out_;
  int const bass = BLIP_READER_BASS(m_bufs[1]);
  BLIP_READER_BEGIN(left, m_bufs[1]);
  BLIP_READER_BEGIN(right, m_bufs[2]);

  for (; count; --count) {
    blargg_long l = BLIP_READER_READ(left);
    if ((int16_t) l != l)
      l = 0x7FFF - (l >> 24);

    blargg_long r = BLIP_READER_READ(right);
    if ((int16_t) r != r)
      r = 0x7FFF - (r >> 24);

    BLIP_READER_NEXT(left, bass);
    BLIP_READER_NEXT(right, bass);

    out[0] = l;
    out[1] = r;
    out += 2;
  }

  BLIP_READER_END(left, m_bufs[1]);
  BLIP_READER_END(right, m_bufs[2]);
}

void StereoBuffer::m_mixMono(blip_sample_t *out_, blargg_long count) {
  blip_sample_t *out = out_;
  const int bass = BLIP_READER_BASS(this->m_bufs[0]);
  BLIP_READER_BEGIN(center, this->m_bufs[0]);

  for (; count; --count) {
    blargg_long s = BLIP_READER_READ(center);
    if ((int16_t) s != s)
      s = 0x7FFF - (s >> 24);

    BLIP_READER_NEXT(center, bass);
    out[0] = s;
    out[1] = s;
    out += 2;
  }

  BLIP_READER_END(center, this->m_bufs[0]);
}
