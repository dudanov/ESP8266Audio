// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "EffectsBuffer.h"

#include <algorithm>
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

#ifdef BLARGG_ENABLE_OPTIMIZER
#include BLARGG_ENABLE_OPTIMIZER
#endif

typedef blargg_long fixed_t;

using std::max;
using std::min;

#define TO_FIXED(f) fixed_t((f) * (1L << 15) + 0.5)
#define FMUL(x, y) (((x) * (y)) >> 15)

static const unsigned ECHO_SIZE = 4096;
static const unsigned ECHO_MASK = ECHO_SIZE - 1;
static_assert((ECHO_SIZE & ECHO_MASK) == 0, "ECHO_SIZE must be a power of 2");

static const unsigned REVERB_SIZE = 8192 * 2;
static const unsigned REVERB_MASK = REVERB_SIZE - 1;
static_assert((REVERB_SIZE & REVERB_MASK) == 0, "REVERB_SIZE must be a power of 2");

void EffectsBuffer::setDepth(double d) {
  float f = (float) d;
  config_t c;
  c.pan_1 = -0.6f * f;
  c.pan_2 = 0.6f * f;
  c.reverb_delay = 880 * 0.1f;
  c.echo_delay = 610 * 0.1f;
  if (f > 0.5)
    f = 0.5;  // TODO: more linear reduction of extreme reverb/echo
  c.reverb_level = 0.5f * f;
  c.echo_level = 0.30f * f;
  c.delay_variance = 180 * 0.1f;
  c.effects_enabled = (d > 0.0f);
  config(c);
}

EffectsBuffer::EffectsBuffer(int numChannels, bool centerOnly)
    : MultiBuffer(2 * numChannels),
      m_maxChannels(numChannels),
      m_bufNum(m_maxChannels * (centerOnly ? (MAX_BUFS_NUM - 4) : MAX_BUFS_NUM)),
      m_bufs(m_bufNum),
      m_chanTypes(m_maxChannels * CHAN_TYPES_NUM),
      m_stereoRemain(0),
      m_effectRemain(0),
      m_effectsEnabled(false),
      m_reverbBuf(m_maxChannels, std::vector<blip_sample_t>(REVERB_SIZE)),
      m_echoBuf(m_maxChannels, std::vector<blip_sample_t>(ECHO_SIZE)),
      m_reverbPos(m_maxChannels),
      m_echoPos(m_maxChannels) {
  setDepth(0);
}

EffectsBuffer::~EffectsBuffer() {}

blargg_err_t EffectsBuffer::SetSampleRate(long rate, int msec) {
  for (int i = 0; i < m_maxChannels; i++) {
    if (!m_echoBuf[i].size()) {
      m_echoBuf[i].resize(ECHO_SIZE);
    }

    if (!m_reverbBuf[i].size()) {
      m_reverbBuf[i].resize(REVERB_SIZE);
    }
  }

  for (auto &buf : this->m_bufs)
    RETURN_ERR(buf.SetSampleRate(rate, msec));

  config(m_config);
  clear();

  return MultiBuffer::SetSampleRate(m_bufs[0].GetSampleRate(), m_bufs[0].GetLength());
}

void EffectsBuffer::setClockRate(long rate) {
  for (auto &buf : this->m_bufs)
    buf.SetClockRate(rate);
}

void EffectsBuffer::setBassFreq(int freq) {
  for (auto &buf : this->m_bufs)
    buf.SetBassFrequency(freq);
}

void EffectsBuffer::clear() {
  m_stereoRemain = 0;
  m_effectRemain = 0;

  for (int i = 0; i < m_maxChannels; i++) {
    if (m_echoBuf[i].size())
      memset(&m_echoBuf[i][0], 0, ECHO_SIZE * sizeof m_echoBuf[i][0]);

    if (m_reverbBuf[i].size())
      memset(&m_reverbBuf[i][0], 0, REVERB_SIZE * sizeof m_reverbBuf[i][0]);
  }

  for (auto &buf : this->m_bufs)
    buf.Clear();
}

inline int pin_range(int n, int max, int min = 0) {
  if (n < min)
    return min;
  if (n > max)
    return max;
  return n;
}

void EffectsBuffer::config(const config_t &cfg) {
  m_channelChanged();

  // clear echo and reverb buffers
  // ensure the echo/reverb buffers have already been allocated, so this
  // method can be called before SetSampleRate is called
  if (!m_config.effects_enabled && cfg.effects_enabled && m_echoBuf[0].size()) {
    for (int i = 0; i < m_maxChannels; i++) {
      memset(&m_echoBuf[i][0], 0, ECHO_SIZE * sizeof m_echoBuf[i][0]);
      memset(&m_reverbBuf[i][0], 0, REVERB_SIZE * sizeof m_reverbBuf[i][0]);
    }
  }

  m_config = cfg;

  if (m_config.effects_enabled) {
    // convert to internal format

    chans.pan_1_levels[0] = TO_FIXED(1) - TO_FIXED(m_config.pan_1);
    chans.pan_1_levels[1] = TO_FIXED(2) - chans.pan_1_levels[0];

    chans.pan_2_levels[0] = TO_FIXED(1) - TO_FIXED(m_config.pan_2);
    chans.pan_2_levels[1] = TO_FIXED(2) - chans.pan_2_levels[0];

    chans.reverb_level = TO_FIXED(m_config.reverb_level);
    chans.echo_level = TO_FIXED(m_config.echo_level);

    int delay_offset = int(1.0 / 2000 * m_config.delay_variance * GetSampleRate());

    int reverb_sample_delay = int(1.0 / 1000 * m_config.reverb_delay * GetSampleRate());
    chans.reverb_delay_l = pin_range(REVERB_SIZE - (reverb_sample_delay - delay_offset) * 2, REVERB_SIZE - 2, 0);
    chans.reverb_delay_r = pin_range(REVERB_SIZE + 1 - (reverb_sample_delay + delay_offset) * 2, REVERB_SIZE - 1, 1);

    int echo_sample_delay = int(1.0 / 1000 * m_config.echo_delay * GetSampleRate());
    chans.echo_delay_l = pin_range(ECHO_SIZE - 1 - (echo_sample_delay - delay_offset), ECHO_SIZE - 1);
    chans.echo_delay_r = pin_range(ECHO_SIZE - 1 - (echo_sample_delay + delay_offset), ECHO_SIZE - 1);

    for (int i = 0; i < m_maxChannels; i++) {
      m_chanTypes[i * CHAN_TYPES_NUM + 0].center = &m_bufs[i * MAX_BUFS_NUM + 0];
      m_chanTypes[i * CHAN_TYPES_NUM + 0].left = &m_bufs[i * MAX_BUFS_NUM + 3];
      m_chanTypes[i * CHAN_TYPES_NUM + 0].right = &m_bufs[i * MAX_BUFS_NUM + 4];

      m_chanTypes[i * CHAN_TYPES_NUM + 1].center = &m_bufs[i * MAX_BUFS_NUM + 1];
      m_chanTypes[i * CHAN_TYPES_NUM + 1].left = &m_bufs[i * MAX_BUFS_NUM + 3];
      m_chanTypes[i * CHAN_TYPES_NUM + 1].right = &m_bufs[i * MAX_BUFS_NUM + 4];

      m_chanTypes[i * CHAN_TYPES_NUM + 2].center = &m_bufs[i * MAX_BUFS_NUM + 2];
      m_chanTypes[i * CHAN_TYPES_NUM + 2].left = &m_bufs[i * MAX_BUFS_NUM + 5];
      m_chanTypes[i * CHAN_TYPES_NUM + 2].right = &m_bufs[i * MAX_BUFS_NUM + 6];
    }
    assert(2 < CHAN_TYPES_NUM);
  } else {
    for (int i = 0; i < m_maxChannels; i++) {
      // set up outputs
      for (int j = 0; j < CHAN_TYPES_NUM; j++) {
        Channel &c = m_chanTypes[i * CHAN_TYPES_NUM + j];
        c.center = &m_bufs[i * MAX_BUFS_NUM + 0];
        c.left = &m_bufs[i * MAX_BUFS_NUM + 1];
        c.right = &m_bufs[i * MAX_BUFS_NUM + 2];
      }
    }
  }

  if (m_bufNum < MAX_BUFS_NUM)  // if centerOnly
  {
    for (int i = 0; i < m_maxChannels; i++) {
      for (int j = 0; j < CHAN_TYPES_NUM; j++) {
        Channel &c = m_chanTypes[i * CHAN_TYPES_NUM + j];
        c.left = c.center;
        c.right = c.center;
      }
    }
  }
}

const EffectsBuffer::Channel &EffectsBuffer::getChannelBuffers(int i, int type) const {
  int out = CHAN_TYPES_NUM - 1;
  if (!type) {
    out = i % 5;
    if (out > CHAN_TYPES_NUM - 1)
      out = CHAN_TYPES_NUM - 1;
  } else if (!(type & NOISE_TYPE) && (type & TYPE_INDEX_MASK) % 3 != 0) {
    out = type & 1;
  }
  return m_chanTypes[(i % m_maxChannels) * CHAN_TYPES_NUM + out];
}

void EffectsBuffer::EndFrame(blip_time_t clock_count) {
  int bufs_used = 0;
  int stereo_mask = (m_config.effects_enabled ? 0x78 : 0x06);

  const int buf_count_per_voice = m_bufNum / m_maxChannels;
  for (int v = 0; v < m_maxChannels; v++)  // foreach voice
  {
    for (int i = 0; i < buf_count_per_voice; i++)  // foreach buffer of that voice
    {
      bufs_used |= m_bufs[v * buf_count_per_voice + i].ClearModified() << i;
      m_bufs[v * buf_count_per_voice + i].EndFrame(clock_count);

      if ((bufs_used & stereo_mask) && m_bufNum == m_maxChannels * MAX_BUFS_NUM)
        m_stereoRemain = max(m_stereoRemain, m_bufs[v * buf_count_per_voice + i].SamplesAvailable() +
                                                 m_bufs[v * buf_count_per_voice + i].GetOutputLatency());
      if (m_effectsEnabled || m_config.effects_enabled)
        m_effectRemain = max(m_effectRemain, m_bufs[v * buf_count_per_voice + i].SamplesAvailable() +
                                                 m_bufs[v * buf_count_per_voice + i].GetOutputLatency());
    }
    bufs_used = 0;
  }

  m_effectsEnabled = m_config.effects_enabled;
}

long EffectsBuffer::samplesAvailable() const { return m_bufs[0].SamplesAvailable() * 2; }

long EffectsBuffer::readSamples(blip_sample_t *out, long total_samples) {
  const int n_channels = m_maxChannels * 2;
  const int buf_count_per_voice = m_bufNum / m_maxChannels;

  require(total_samples % n_channels == 0);  // as many items needed to fill at least one frame

  long remain = m_bufs[0].SamplesAvailable();
  total_samples = remain = min(remain, total_samples / n_channels);

  while (remain) {
    int active_bufs = buf_count_per_voice;
    long count = remain;

    // optimizing mixing to skip any channels which had nothing added
    if (m_effectRemain) {
      if (count > m_effectRemain)
        count = m_effectRemain;

      if (m_stereoRemain) {
        m_mixEnhanced(out, count);
      } else {
        m_mixMonoEnhanced(out, count);
        active_bufs = 3;
      }
    } else if (m_stereoRemain) {
      m_mixStereo(out, count);
      active_bufs = 3;
    } else {
      m_mixMono(out, count);
      active_bufs = 1;
    }

    out += count * n_channels;
    remain -= count;

    m_stereoRemain -= count;
    if (m_stereoRemain < 0)
      m_stereoRemain = 0;

    m_effectRemain -= count;
    if (m_effectRemain < 0)
      m_effectRemain = 0;

    // skip the output from any buffers that didn't contribute to the sound
    // output during this frame (e.g. if we only render mono then only the
    // very first buf is 'active')
    for (int v = 0; v < m_maxChannels; v++)  // foreach voice
    {
      for (int i = 0; i < buf_count_per_voice; i++)  // foreach buffer of that voice
      {
        if (i < active_bufs)
          m_bufs[v * buf_count_per_voice + i].RemoveSamples(count);
        else  // keep time synchronized
          m_bufs[v * buf_count_per_voice + i].RemoveSilence(count);
      }
    }
  }

  return total_samples * n_channels;
}

void EffectsBuffer::m_mixMono(blip_sample_t *out_, blargg_long count) {
  for (int i = 0; i < m_maxChannels; i++) {
    blip_sample_t *out = out_;
    int const bass = BLIP_READER_BASS(m_bufs[i * MAX_BUFS_NUM + 0]);
    BLIP_READER_BEGIN(c, m_bufs[i * MAX_BUFS_NUM + 0]);

    // unrolled loop
    for (blargg_long n = count >> 1; n; --n) {
      blargg_long cs0 = BLIP_READER_READ(c);
      BLIP_READER_NEXT(c, bass);

      blargg_long cs1 = BLIP_READER_READ(c);
      BLIP_READER_NEXT(c, bass);

      if ((int16_t) cs0 != cs0)
        cs0 = 0x7FFF - (cs0 >> 24);
      ((uint32_t *) out)[i * 2 + 0] = ((uint16_t) cs0) | (uint16_t(cs0) << 16);

      if ((int16_t) cs1 != cs1)
        cs1 = 0x7FFF - (cs1 >> 24);
      ((uint32_t *) out)[i * 2 + 1] = ((uint16_t) cs1) | (uint16_t(cs1) << 16);
      out += m_maxChannels * 4;
    }

    if (count & 1) {
      int s = BLIP_READER_READ(c);
      BLIP_READER_NEXT(c, bass);
      out[i * 2 + 0] = s;
      out[i * 2 + 1] = s;
      if ((int16_t) s != s) {
        s = 0x7FFF - (s >> 24);
        out[i * 2 + 0] = s;
        out[i * 2 + 1] = s;
      }
    }

    BLIP_READER_END(c, m_bufs[i * MAX_BUFS_NUM + 0]);
  }
}

void EffectsBuffer::m_mixStereo(blip_sample_t *out_, blargg_long frames) {
  for (int i = 0; i < m_maxChannels; i++) {
    blip_sample_t *out = out_;
    int const bass = BLIP_READER_BASS(m_bufs[i * MAX_BUFS_NUM + 0]);
    BLIP_READER_BEGIN(c, m_bufs[i * MAX_BUFS_NUM + 0]);
    BLIP_READER_BEGIN(l, m_bufs[i * MAX_BUFS_NUM + 1]);
    BLIP_READER_BEGIN(r, m_bufs[i * MAX_BUFS_NUM + 2]);

    int count = frames;
    while (count--) {
      int cs = BLIP_READER_READ(c);
      BLIP_READER_NEXT(c, bass);
      int left = cs + BLIP_READER_READ(l);
      int right = cs + BLIP_READER_READ(r);
      BLIP_READER_NEXT(l, bass);
      BLIP_READER_NEXT(r, bass);

      if ((int16_t) left != left)
        left = 0x7FFF - (left >> 24);

      if ((int16_t) right != right)
        right = 0x7FFF - (right >> 24);

      out[i * 2 + 0] = left;
      out[i * 2 + 1] = right;

      out += m_maxChannels * 2;
    }

    BLIP_READER_END(r, m_bufs[i * MAX_BUFS_NUM + 2]);
    BLIP_READER_END(l, m_bufs[i * MAX_BUFS_NUM + 1]);
    BLIP_READER_END(c, m_bufs[i * MAX_BUFS_NUM + 0]);
  }
}

void EffectsBuffer::m_mixMonoEnhanced(blip_sample_t *out_, blargg_long frames) {
  for (int i = 0; i < m_maxChannels; i++) {
    blip_sample_t *out = out_;
    int const bass = BLIP_READER_BASS(m_bufs[i * MAX_BUFS_NUM + 2]);
    BLIP_READER_BEGIN(center, m_bufs[i * MAX_BUFS_NUM + 2]);
    BLIP_READER_BEGIN(sq1, m_bufs[i * MAX_BUFS_NUM + 0]);
    BLIP_READER_BEGIN(sq2, m_bufs[i * MAX_BUFS_NUM + 1]);

    blip_sample_t *const reverb_buf = &this->m_reverbBuf[i][0];
    blip_sample_t *const echo_buf = &this->m_echoBuf[i][0];
    int echo_pos = this->m_echoPos[i];
    int reverb_pos = this->m_reverbPos[i];

    int count = frames;
    while (count--) {
      int sum1_s = BLIP_READER_READ(sq1);
      int sum2_s = BLIP_READER_READ(sq2);

      BLIP_READER_NEXT(sq1, bass);
      BLIP_READER_NEXT(sq2, bass);

      int new_reverb_l = FMUL(sum1_s, chans.pan_1_levels[0]) + FMUL(sum2_s, chans.pan_2_levels[0]) +
                         reverb_buf[(reverb_pos + chans.reverb_delay_l) & REVERB_MASK];

      int new_reverb_r = FMUL(sum1_s, chans.pan_1_levels[1]) + FMUL(sum2_s, chans.pan_2_levels[1]) +
                         reverb_buf[(reverb_pos + chans.reverb_delay_r) & REVERB_MASK];

      fixed_t reverb_level = chans.reverb_level;
      reverb_buf[reverb_pos] = (blip_sample_t) FMUL(new_reverb_l, reverb_level);
      reverb_buf[reverb_pos + 1] = (blip_sample_t) FMUL(new_reverb_r, reverb_level);
      reverb_pos = (reverb_pos + 2) & REVERB_MASK;

      int sum3_s = BLIP_READER_READ(center);
      BLIP_READER_NEXT(center, bass);

      int left = new_reverb_l + sum3_s + FMUL(chans.echo_level, echo_buf[(echo_pos + chans.echo_delay_l) & ECHO_MASK]);
      int right = new_reverb_r + sum3_s + FMUL(chans.echo_level, echo_buf[(echo_pos + chans.echo_delay_r) & ECHO_MASK]);

      echo_buf[echo_pos] = sum3_s;
      echo_pos = (echo_pos + 1) & ECHO_MASK;

      if ((int16_t) left != left)
        left = 0x7FFF - (left >> 24);

      if ((int16_t) right != right)
        right = 0x7FFF - (right >> 24);

      out[i * 2 + 0] = left;
      out[i * 2 + 1] = right;
      out += m_maxChannels * 2;
    }
    this->m_reverbPos[i] = reverb_pos;
    this->m_echoPos[i] = echo_pos;

    BLIP_READER_END(sq1, m_bufs[i * MAX_BUFS_NUM + 0]);
    BLIP_READER_END(sq2, m_bufs[i * MAX_BUFS_NUM + 1]);
    BLIP_READER_END(center, m_bufs[i * MAX_BUFS_NUM + 2]);
  }
}

void EffectsBuffer::m_mixEnhanced(blip_sample_t *out_, blargg_long frames) {
  for (int i = 0; i < m_maxChannels; i++) {
    blip_sample_t *out = out_;
    int const bass = BLIP_READER_BASS(m_bufs[i * MAX_BUFS_NUM + 2]);
    BLIP_READER_BEGIN(center, m_bufs[i * MAX_BUFS_NUM + 2]);
    BLIP_READER_BEGIN(l1, m_bufs[i * MAX_BUFS_NUM + 3]);
    BLIP_READER_BEGIN(r1, m_bufs[i * MAX_BUFS_NUM + 4]);
    BLIP_READER_BEGIN(l2, m_bufs[i * MAX_BUFS_NUM + 5]);
    BLIP_READER_BEGIN(r2, m_bufs[i * MAX_BUFS_NUM + 6]);
    BLIP_READER_BEGIN(sq1, m_bufs[i * MAX_BUFS_NUM + 0]);
    BLIP_READER_BEGIN(sq2, m_bufs[i * MAX_BUFS_NUM + 1]);

    blip_sample_t *const reverb_buf = &this->m_reverbBuf[i][0];
    blip_sample_t *const echo_buf = &this->m_echoBuf[i][0];
    int echo_pos = this->m_echoPos[i];
    int reverb_pos = this->m_reverbPos[i];

    int count = frames;
    while (count--) {
      int sum1_s = BLIP_READER_READ(sq1);
      int sum2_s = BLIP_READER_READ(sq2);

      BLIP_READER_NEXT(sq1, bass);
      BLIP_READER_NEXT(sq2, bass);

      int new_reverb_l = FMUL(sum1_s, chans.pan_1_levels[0]) + FMUL(sum2_s, chans.pan_2_levels[0]) +
                         BLIP_READER_READ(l1) + reverb_buf[(reverb_pos + chans.reverb_delay_l) & REVERB_MASK];

      int new_reverb_r = FMUL(sum1_s, chans.pan_1_levels[1]) + FMUL(sum2_s, chans.pan_2_levels[1]) +
                         BLIP_READER_READ(r1) + reverb_buf[(reverb_pos + chans.reverb_delay_r) & REVERB_MASK];

      BLIP_READER_NEXT(l1, bass);
      BLIP_READER_NEXT(r1, bass);

      fixed_t reverb_level = chans.reverb_level;
      reverb_buf[reverb_pos] = (blip_sample_t) FMUL(new_reverb_l, reverb_level);
      reverb_buf[reverb_pos + 1] = (blip_sample_t) FMUL(new_reverb_r, reverb_level);
      reverb_pos = (reverb_pos + 2) & REVERB_MASK;

      int sum3_s = BLIP_READER_READ(center);
      BLIP_READER_NEXT(center, bass);

      int left = new_reverb_l + sum3_s + BLIP_READER_READ(l2) +
                 FMUL(chans.echo_level, echo_buf[(echo_pos + chans.echo_delay_l) & ECHO_MASK]);
      int right = new_reverb_r + sum3_s + BLIP_READER_READ(r2) +
                  FMUL(chans.echo_level, echo_buf[(echo_pos + chans.echo_delay_r) & ECHO_MASK]);

      BLIP_READER_NEXT(l2, bass);
      BLIP_READER_NEXT(r2, bass);

      echo_buf[echo_pos] = sum3_s;
      echo_pos = (echo_pos + 1) & ECHO_MASK;

      if ((int16_t) left != left)
        left = 0x7FFF - (left >> 24);

      if ((int16_t) right != right)
        right = 0x7FFF - (right >> 24);

      out[i * 2 + 0] = left;
      out[i * 2 + 1] = right;

      out += m_maxChannels * 2;
    }
    this->m_reverbPos[i] = reverb_pos;
    this->m_echoPos[i] = echo_pos;

    BLIP_READER_END(l1, m_bufs[i * MAX_BUFS_NUM + 3]);
    BLIP_READER_END(r1, m_bufs[i * MAX_BUFS_NUM + 4]);
    BLIP_READER_END(l2, m_bufs[i * MAX_BUFS_NUM + 5]);
    BLIP_READER_END(r2, m_bufs[i * MAX_BUFS_NUM + 6]);
    BLIP_READER_END(sq1, m_bufs[i * MAX_BUFS_NUM + 0]);
    BLIP_READER_END(sq2, m_bufs[i * MAX_BUFS_NUM + 1]);
    BLIP_READER_END(center, m_bufs[i * MAX_BUFS_NUM + 2]);
  }
}
