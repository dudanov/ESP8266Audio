// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "VgmEmu.h"

#include "blargg_endian.h"
#include <math.h>
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

namespace gme {
namespace emu {
namespace vgm {

enum {
  cmd_gg_stereo = 0x4F,
  cmd_psg = 0x50,
  cmd_ym2413 = 0x51,
  cmd_ym2612_port0 = 0x52,
  cmd_ym2612_port1 = 0x53,
  cmd_ym2151 = 0x54,
  cmd_delay = 0x61,
  cmd_delay_735 = 0x62,
  cmd_delay_882 = 0x63,
  cmd_byte_delay = 0x64,
  cmd_end = 0x66,
  cmd_data_block = 0x67,
  cmd_short_delay = 0x70,
  cmd_pcm_delay = 0x80,
  cmd_pcm_seek = 0xE0,

  cmd_gg_stereo_2 = 0x3F,
  cmd_psg_2 = 0x30,
  cmd_ym2413_2 = 0xA1,
  cmd_ym2612_2_port0 = 0xA2,
  cmd_ym2612_2_port1 = 0xA3,

  pcm_block_type = 0x00,
  ym2612_dac_port = 0x2A
};

inline int command_len(int command) {
  switch (command >> 4) {
    case 0x03:
    case 0x04:
      return 2;

    case 0x05:
    case 0x0A:
    case 0x0B:
      return 3;

    case 0x0C:
    case 0x0D:
      return 4;

    case 0x0E:
    case 0x0F:
      return 5;
  }

  check(false);
  return 1;
}

template<class Emu> inline void YmEmu<Emu>::begin_frame(short *p) {
  require(enabled());
  mOut = p;
  mLastTime = 0;
}

template<class Emu> inline int YmEmu<Emu>::mRunUntil(int time) {
  int count = time - mLastTime;
  if (count > 0) {
    if (mLastTime < 0)
      return false;
    mLastTime = time;
    short *p = mOut;
    mOut += count * Emu::OUT_CHANNELS_NUM;
    Emu::run(count, p);
  }
  return true;
}

inline VgmEmuImpl::fm_time_t VgmEmuImpl::mToFmTime(vgm_time_t t) const {
  return (t * mFmTimeFactor + mFmTimeOffset) >> FM_TIME_BITS;
}

inline blip_time_t VgmEmuImpl::mToBlipTime(vgm_time_t t) const { return (t * mBlipTimeFactor) >> BLIP_TIME_BITS; }

void VgmEmuImpl::mWritePcm(vgm_time_t vgm_time, int amp) {
  blip_time_t blip_time = mToBlipTime(vgm_time);
  int old = mDacAmp;
  int delta = amp - old;
  mDacAmp = amp;
  if (old >= 0)
    mDacSynth.Offset(&mBlipBuf, blip_time, delta);
  else
    mDacAmp |= mDacDisabled;
}

blip_time_t VgmEmuImpl::mRunCommands(blip_clk_time_t end_time) {
  auto vgm_time = this->mVgmTime;
  const uint8_t *pos = this->mPos;
  if (pos >= mDataEnd) {
    mSetTrackEnded();
    if (pos > mDataEnd)
      m_setWarning("Stream lacked end event");
  }

  while (vgm_time < end_time && pos < mDataEnd) {
    // TODO: be sure there are enough bytes left in stream for particular
    // command so we don't read past end
    switch (*pos++) {
      case cmd_end:
        pos = mLoopBegin;  // if not looped, mLoopBegin == mDataEnd
        break;

      case cmd_delay_735:
        vgm_time += 735;
        break;

      case cmd_delay_882:
        vgm_time += 882;
        break;

      case cmd_gg_stereo:
        mPsg[0].writeGGStereo(mToBlipTime(vgm_time), *pos++);
        break;

      case cmd_psg:
        mPsg[0].writeData(mToBlipTime(vgm_time), *pos++);
        break;

      case cmd_gg_stereo_2:
        mPsg[1].writeGGStereo(mToBlipTime(vgm_time), *pos++);
        break;

      case cmd_psg_2:
        mPsg[1].writeData(mToBlipTime(vgm_time), *pos++);
        break;

      case cmd_delay:
        vgm_time += pos[1] * 0x100L + pos[0];
        pos += 2;
        break;

      case cmd_byte_delay:
        vgm_time += *pos++;
        break;

      case cmd_ym2413:
        if (mYm2413[0].mRunUntil(mToFmTime(vgm_time)))
          mYm2413[0].write(pos[0], pos[1]);
        pos += 2;
        break;

      case cmd_ym2413_2:
        if (mYm2413[1].mRunUntil(mToFmTime(vgm_time)))
          mYm2413[1].write(pos[0], pos[1]);
        pos += 2;
        break;

      case cmd_ym2612_port0:
        if (pos[0] == ym2612_dac_port) {
          mWritePcm(vgm_time, pos[1]);
        } else if (mYm2612[0].mRunUntil(mToFmTime(vgm_time))) {
          if (pos[0] == 0x2B) {
            mDacDisabled = (pos[1] >> 7 & 1) - 1;
            mDacAmp |= mDacDisabled;
          }
          mYm2612[0].write0(pos[0], pos[1]);
        }
        pos += 2;
        break;

      case cmd_ym2612_port1:
        if (mYm2612[0].mRunUntil(mToFmTime(vgm_time)))
          mYm2612[0].write1(pos[0], pos[1]);
        pos += 2;
        break;

      case cmd_ym2612_2_port0:
        if (pos[0] == ym2612_dac_port) {
          mWritePcm(vgm_time, pos[1]);
        } else if (mYm2612[1].mRunUntil(mToFmTime(vgm_time))) {
          if (pos[0] == 0x2B) {
            mDacDisabled = (pos[1] >> 7 & 1) - 1;
            mDacAmp |= mDacDisabled;
          }
          mYm2612[1].write0(pos[0], pos[1]);
        }
        pos += 2;
        break;

      case cmd_ym2612_2_port1:
        if (mYm2612[1].mRunUntil(mToFmTime(vgm_time)))
          mYm2612[1].write1(pos[0], pos[1]);
        pos += 2;
        break;

      case cmd_data_block: {
        check(*pos == cmd_end);
        int type = pos[1];
        long size = get_le32(pos + 2);
        pos += 6;
        if (type == pcm_block_type)
          mPcmData = pos;
        pos += size;
        break;
      }

      case cmd_pcm_seek:
        mPcmPos = mPcmData + pos[3] * 0x1000000L + pos[2] * 0x10000L + pos[1] * 0x100L + pos[0];
        pos += 4;
        break;

      default:
        int cmd = pos[-1];
        switch (cmd & 0xF0) {
          case cmd_pcm_delay:
            mWritePcm(vgm_time, *mPcmPos++);
            vgm_time += cmd & 0x0F;
            break;

          case cmd_short_delay:
            vgm_time += (cmd & 0x0F) + 1;
            break;

          case 0x50:
            pos += 2;
            break;

          default:
            pos += command_len(cmd) - 1;
            m_setWarning("Unknown stream event");
        }
    }
  }
  vgm_time -= end_time;
  this->mPos = pos;
  this->mVgmTime = vgm_time;

  return mToBlipTime(end_time);
}

int VgmEmuImpl::mPlayFrame(blip_time_t blip_time, int sample_count, sample_t *buf) {
  // to do: timing is working mostly by luck

  int min_pairs = sample_count >> 1;
  int vgm_time = ((long) min_pairs << FM_TIME_BITS) / mFmTimeFactor - 1;
  assert(mToFmTime(vgm_time) <= min_pairs);
  int pairs = min_pairs;
  while ((pairs = mToFmTime(vgm_time)) < min_pairs)
    vgm_time++;
  // debug_printf( "pairs: %d, min_pairs: %d\n", pairs, min_pairs );

  if (mYm2612[0].enabled()) {
    mYm2612[0].begin_frame(buf);
    if (mYm2612[1].enabled())
      mYm2612[1].begin_frame(buf);
    memset(buf, 0, pairs * STEREO * sizeof *buf);
  } else if (mYm2413[0].enabled()) {
    mYm2413[0].begin_frame(buf);
    if (mYm2413[1].enabled())
      mYm2413[1].begin_frame(buf);
    memset(buf, 0, pairs * STEREO * sizeof *buf);
  }

  mRunCommands(vgm_time);

  if (mYm2612[0].enabled())
    mYm2612[0].mRunUntil(pairs);
  if (mYm2612[1].enabled())
    mYm2612[1].mRunUntil(pairs);

  if (mYm2413[0].enabled())
    mYm2413[0].mRunUntil(pairs);
  if (mYm2413[1].enabled())
    mYm2413[1].mRunUntil(pairs);

  mFmTimeOffset = (vgm_time * mFmTimeFactor + mFmTimeOffset) - ((long) pairs << FM_TIME_BITS);

  mPsg[0].EndFrame(blip_time);
  if (mPsgDual)
    mPsg[1].EndFrame(blip_time);

  return pairs * STEREO;
}

// Update pre-1.10 header FM rates by scanning commands
void VgmEmuImpl::mUpdateFmRates(long *ym2413_rate, long *ym2612_rate) const {
  uint8_t const *p = mData + 0x40;
  while (p < mDataEnd) {
    switch (*p) {
      case cmd_end:
        return;

      case cmd_psg:
      case cmd_byte_delay:
        p += 2;
        break;

      case cmd_delay:
        p += 3;
        break;

      case cmd_data_block:
        p += 7 + get_le32(p + 3);
        break;

      case cmd_ym2413:
        *ym2612_rate = 0;
        return;

      case cmd_ym2612_port0:
      case cmd_ym2612_port1:
        *ym2612_rate = *ym2413_rate;
        *ym2413_rate = 0;
        return;

      case cmd_ym2151:
        *ym2413_rate = 0;
        *ym2612_rate = 0;
        return;

      default:
        p += command_len(*p);
    }
  }
}

}  // namespace vgm
}  // namespace emu
}  // namespace gme
