// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "DualResampler.h"

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

#include "blargg_source.h"

blargg_err_t DualResampler::reset(int pairs) {
  // expand allocations a bit
  RETURN_ERR(m_sampleBuf.resize((pairs + (pairs >> 2)) * 2));
  resize(pairs);
  m_resamplerSize = m_oversamplesPerFrame + (m_oversamplesPerFrame >> 2);
  return m_resampler.setBufferSize(m_resamplerSize);
}

void DualResampler::resize(int pairs) {
  int new_sample_buf_size = pairs * 2;
  if (m_sampleBufSize != new_sample_buf_size) {
    if ((unsigned) new_sample_buf_size > m_sampleBuf.size()) {
      check(false);
      return;
    }
    m_sampleBufSize = new_sample_buf_size;
    m_oversamplesPerFrame = int(pairs * m_resampler.getRatio()) * 2 + 2;
    clear();
  }
}

void DualResampler::m_playFrame(BlipBuffer &blip_buf, dsample_t *out) {
  long pair_count = m_sampleBufSize >> 1;
  blip_time_t blip_time = blip_buf.CountClocks(pair_count);
  int sample_count = m_oversamplesPerFrame - m_resampler.written();

  int new_count = m_playFrame(blip_time, sample_count, m_resampler.buffer());
  assert(new_count < m_resamplerSize);

  blip_buf.EndFrame(blip_time);
  assert(blip_buf.SamplesAvailable() == pair_count);

  m_resampler.write(new_count);

#ifdef NDEBUG  // Avoid warning when asserts are disabled
  m_resampler.read(m_sampleBuf.begin(), m_sampleBufSize);
#else
  long count = m_resampler.read(m_sampleBuf.begin(), m_sampleBufSize);
  assert(count == (long) m_sampleBufSize);
#endif

  m_mixSamples(blip_buf, out);
  blip_buf.RemoveSamples(pair_count);
}

void DualResampler::dualPlay(long count, dsample_t *out, BlipBuffer &blip_buf) {
  // empty extra buffer
  long remain = m_sampleBufSize - m_bufPosition;
  if (remain) {
    if (remain > count)
      remain = count;
    count -= remain;
    memcpy(out, &m_sampleBuf[m_bufPosition], remain * sizeof *out);
    out += remain;
    m_bufPosition += remain;
  }

  // entire frames
  while (count >= (long) m_sampleBufSize) {
    m_playFrame(blip_buf, out);
    out += m_sampleBufSize;
    count -= m_sampleBufSize;
  }

  // extra
  if (count) {
    m_playFrame(blip_buf, m_sampleBuf.begin());
    m_bufPosition = count;
    memcpy(out, m_sampleBuf.begin(), count * sizeof *out);
    out += count;
  }
}

void DualResampler::m_mixSamples(BlipBuffer &blip_buf, dsample_t *out) {
  BLIP_READER_BEGIN(sn, blip_buf);
  int bass = BLIP_READER_BASS(blip_buf);
  const dsample_t *in = m_sampleBuf.begin();

  for (int n = m_sampleBufSize >> 1; n--;) {
    int s = BLIP_READER_READ(sn);
    blargg_long l = (blargg_long) in[0] * 2 + s;
    if ((int16_t) l != l)
      l = 0x7FFF - (l >> 24);

    BLIP_READER_NEXT(sn, bass);
    blargg_long r = (blargg_long) in[1] * 2 + s;
    if ((int16_t) r != r)
      r = 0x7FFF - (r >> 24);

    in += 2;
    out[0] = l;
    out[1] = r;
    out += 2;
  }

  BLIP_READER_END(sn, blip_buf);
}
