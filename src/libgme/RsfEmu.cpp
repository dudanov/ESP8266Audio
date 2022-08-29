// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "RsfEmu.h"

#include "blargg_endian.h"
#include <string.h>
#include <pgmspace.h>

#include <algorithm>  // min, max

/* Copyright (C) 2006 Shay Green. This module is free software; you
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
namespace ay {

static const uint32_t CLK_SPECTRUM = 3546900;

RsfEmu::RsfEmu() {
  static const char *const CHANNELS_NAMES[] = {"Wave 1", "Wave 2", "Wave 3"};
  static int const CHANNELS_TYPES[] = {WAVE_TYPE | 0, WAVE_TYPE | 1, WAVE_TYPE | 2};
  mSetType(gme_rsf_type);
  mSetChannelsNames(CHANNELS_NAMES);
  mSetChannelsTypes(CHANNELS_TYPES);
  mSetSilenceLookahead(6);
}

RsfEmu::~RsfEmu() {}

// Track info

static unsigned count_bits(unsigned value) {
  unsigned nbits = 0;
  if (value != 0) {
    do {
      nbits++;
    } while (value &= (value - 1));
  }
  return nbits;
}

static const uint8_t *find_frame(const RsfEmu::file_t *file, const uint32_t frame) {
  if (frame >= get_le32(file->header->frames))
    return file->begin;

  const uint8_t *it = file->begin;
  uint32_t current = 0;

  for (; current < frame && it < file->end; ++it) {
    if (*it != 0xFE) {
      current++;
      if (*it != 0xFF)
        it += count_bits(get_be16(it)) + 1;
    } else {
      current += *++it;
    }
  }

  return (current >= frame) ? it : file->begin;
}

static blargg_err_t parse_header(const uint8_t *in, long size, RsfEmu::file_t *out) {
  typedef RsfEmu::header_t header_t;
  out->header = (const header_t *) in;
  out->end = in + size;

  if (size <= RsfEmu::HEADER_SIZE)
    return gme_wrong_file_type;

  if (memcmp_P(out->header->tag, PSTR("RSF\x03"), 4))
    return gme_wrong_file_type;

  const uint32_t loop_frame = get_le32(out->header->loop);

  if (loop_frame >= get_le32(out->header->frames))
    return gme_wrong_file_type;

  out->begin = in + get_le16(out->header->song_offset);

  if (std::distance(out->begin, out->end) <= 0)
    return gme_wrong_file_type;

  out->loop = find_frame(out, loop_frame);
  return nullptr;
}

static void copy_rsf_fields(const RsfEmu::file_t &file, track_info_t *out) {
  out->track_count = 1;
  auto p = GmeFile::copyField(out->song, (const char *) file.header->info);
  p = GmeFile::copyField(out->author, p);
  GmeFile::copyField(out->comment, p);
}

blargg_err_t RsfEmu::mGetTrackInfo(track_info_t *out, int track) const {
  copy_rsf_fields(mFile, out);
  return nullptr;
}

struct RsfFile : GmeInfo {
  RsfEmu::file_t file;

  RsfFile() { mSetType(gme_rsf_type); }
  static MusicEmu *createRsfFile() { return BLARGG_NEW RsfFile; }

  blargg_err_t mLoad(uint8_t const *begin, long size) override {
    RETURN_ERR(parse_header(begin, size, &file));
    mSetTrackNum(1);
    return 0;
  }

  blargg_err_t mGetTrackInfo(track_info_t *out, int track) const override {
    copy_rsf_fields(file, out);
    return nullptr;
  }
};

// Setup

blargg_err_t RsfEmu::mLoad(const uint8_t *begin, long size) {
  RETURN_ERR(parse_header(begin, size, &mFile));
  mSetTrackNum(1);
  mSetChannelsNumber(AyApu::OSCS_NUM);
  mApu.SetVolume(0.5);
  return mSetupBuffer(get_le32(mFile.header->chip_freq));
}

void RsfEmu::mUpdateEq(BlipEq const &eq) { mApu.SetTrebleEq(eq); }

void RsfEmu::mSetChannel(int i, BlipBuffer *center, BlipBuffer *, BlipBuffer *) { mApu.SetOscOutput(i, center); }

// Emulation

void RsfEmu::mSetTempo(double t) {
  mPlayPeriod = static_cast<blip_clk_time_t>(mGetClockRate() / get_le16(mFile.header->frame_freq) / t);
}

blargg_err_t RsfEmu::mStartTrack(int track) {
  RETURN_ERR(ClassicEmu::mStartTrack(track));
  mNextPlay = mPlayPeriod;
  mIt = mFile.begin;
  mApu.Reset();
  return nullptr;
}

void RsfEmu::mWriteRegisters(blip_clk_time_t time) {
  uint16_t mask = get_be16(mIt++);
  for (unsigned addr = 0; mask != 0; mask >>= 1, addr++) {
    if (mask & 1)
      mApu.Write(time, addr, *++mIt);
  }
}

blargg_err_t RsfEmu::mRunClocks(blip_clk_time_t &duration) {
  blip_clk_time_t time = 0;
  while (time != duration) {
    time = std::min(duration, mNextPlay);
    if (time != mNextPlay)
      continue;
    if (*mIt != 0xFE) {
      if (*mIt != 0xFF)
        mWriteRegisters(time);
      mNextPlay += mPlayPeriod;
    } else {
      mNextPlay += *++mIt * mPlayPeriod;
    }
    if (++mIt >= mFile.end)
      mIt = mFile.loop;
  }
  mNextPlay -= duration;
  mApu.EndFrame(duration);
  return 0;
}

}  // namespace ay
}  // namespace emu
}  // namespace gme

static const gme_type_t_ gme_rsf_type_ = {
    "ZX Spectrum", 1, 0, &gme::emu::ay::RsfEmu::createRsfEmu, &gme::emu::ay::RsfFile::createRsfFile, "RSF", 1,
};
extern gme_type_t const gme_rsf_type = &gme_rsf_type_;
