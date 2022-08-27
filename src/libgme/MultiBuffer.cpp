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

blargg_err_t MonoBuffer::SetSampleRate(long rate, int msec) {
  RETURN_ERR(mBuf.SetSampleRate(rate, msec));
  return MultiBuffer::SetSampleRate(mBuf.GetSampleRate(), mBuf.GetLength());
}

// StereoBuffer

blargg_err_t StereoBuffer::SetSampleRate(long rate, int msec) {
  for (auto &buf : mBufs)
    RETURN_ERR(buf.SetSampleRate(rate, msec));
  return MultiBuffer::SetSampleRate(mBufs[0].GetSampleRate(), mBufs[0].GetLength());
}

void StereoBuffer::SetClockRate(long rate) {
  for (auto &buf : mBufs)
    buf.SetClockRate(rate);
}

void StereoBuffer::SetBassFreq(int bass) {
  for (auto &buf : mBufs)
    buf.SetBassFrequency(bass);
}

void StereoBuffer::Clear() {
  mStereoAdded = 0;
  mWasStereo = false;
  for (auto &buf : mBufs)
    buf.Clear();
}

void StereoBuffer::EndFrame(blip_time_t clock_count) {
  mStereoAdded = 0;
  unsigned mask = 1;
  for (auto &buf : mBufs) {
    if (buf.ClearModified())
      mStereoAdded |= mask;
    buf.EndFrame(clock_count);
    mask <<= 1;
  }
}

long StereoBuffer::ReadSamples(blip_sample_t *out, long count) {
  require(!(count & 1));  // count must be even
  count = (unsigned) count / 2;

  long avail = mBufs[0].SamplesAvailable();
  if (count > avail)
    count = avail;
  if (count) {
    int bufs_used = mStereoAdded | mWasStereo;
    // debug_printf( "%X\n", bufs_used );
    if (bufs_used <= 1) {
      mMixMono(out, count);
      mBufs[0].RemoveSamples(count);
      mBufs[1].RemoveSilence(count);
      mBufs[2].RemoveSilence(count);
    } else if (bufs_used & 1) {
      mMixStereo(out, count);
      mBufs[0].RemoveSamples(count);
      mBufs[1].RemoveSamples(count);
      mBufs[2].RemoveSamples(count);
    } else {
      mMixStereoNoCenter(out, count);
      mBufs[0].RemoveSilence(count);
      mBufs[1].RemoveSamples(count);
      mBufs[2].RemoveSamples(count);
    }

    // to do: this might miss opportunities for optimization
    if (!mBufs[0].SamplesAvailable()) {
      mWasStereo = mStereoAdded;
      mStereoAdded = 0;
    }
  }
  return count * 2;
}

void StereoBuffer::mMixStereo(blip_sample_t *out_, blargg_long count) {
  blip_sample_t *out = out_;
  int const bass = BLIP_READER_BASS(mBufs[1]);
  BLIP_READER_BEGIN(Center, mBufs[0]);
  BLIP_READER_BEGIN(Left, mBufs[1]);
  BLIP_READER_BEGIN(Right, mBufs[2]);

  for (; count; --count) {
    int c = BLIP_READER_READ(Center);
    blargg_long l = c + BLIP_READER_READ(Left);
    blargg_long r = c + BLIP_READER_READ(Right);
    if ((int16_t) l != l)
      l = 0x7FFF - (l >> 24);

    BLIP_READER_NEXT(Center, bass);
    if ((int16_t) r != r)
      r = 0x7FFF - (r >> 24);

    BLIP_READER_NEXT(Left, bass);
    BLIP_READER_NEXT(Right, bass);

    out[0] = l;
    out[1] = r;
    out += 2;
  }

  BLIP_READER_END(Center, mBufs[0]);
  BLIP_READER_END(Left, mBufs[1]);
  BLIP_READER_END(Right, mBufs[2]);
}

void StereoBuffer::mMixStereoNoCenter(blip_sample_t *out_, blargg_long count) {
  blip_sample_t *out = out_;
  int const bass = BLIP_READER_BASS(mBufs[1]);
  BLIP_READER_BEGIN(Left, mBufs[1]);
  BLIP_READER_BEGIN(Right, mBufs[2]);

  for (; count; --count) {
    blargg_long l = BLIP_READER_READ(Left);
    if ((int16_t) l != l)
      l = 0x7FFF - (l >> 24);

    blargg_long r = BLIP_READER_READ(Right);
    if ((int16_t) r != r)
      r = 0x7FFF - (r >> 24);

    BLIP_READER_NEXT(Left, bass);
    BLIP_READER_NEXT(Right, bass);

    out[0] = l;
    out[1] = r;
    out += 2;
  }

  BLIP_READER_END(Left, mBufs[1]);
  BLIP_READER_END(Right, mBufs[2]);
}

void StereoBuffer::mMixMono(blip_sample_t *out_, blargg_long count) {
  blip_sample_t *out = out_;
  const int bass = BLIP_READER_BASS(mBufs[0]);
  BLIP_READER_BEGIN(Center, mBufs[0]);

  for (; count; --count) {
    blargg_long s = BLIP_READER_READ(Center);
    if ((int16_t) s != s)
      s = 0x7FFF - (s >> 24);

    BLIP_READER_NEXT(Center, bass);
    out[0] = s;
    out[1] = s;
    out += 2;
  }

  BLIP_READER_END(Center, mBufs[0]);
}
