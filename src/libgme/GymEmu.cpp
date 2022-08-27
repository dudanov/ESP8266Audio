// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "GymEmu.h"

#include "blargg_endian.h"
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

static const double MIN_TEMPO = 0.25;
static const double OVERSAMPLE_FACTOR = 5 / 3.0;
static const double FM_GAIN = 3.0;
static const long BASE_CLOCK = 53700300;
static const long CLOCK_RATE = BASE_CLOCK / 15;

GymEmu::GymEmu() {
  data = 0;
  pos = 0;
  this->mSetType(gme_gym_type);

  static const char *const names[] = {"FM 1", "FM 2", "FM 3", "FM 4", "FM 5", "FM 6", "PCM", "PSG"};
  this->mSetChannelsNames(names);
  this->mSetSilenceLookahead(1);  // tracks should already be trimmed
}

GymEmu::~GymEmu() {}

// Track info

static void get_gym_info(const GymEmu::header_t &h, long length, track_info_t *out) {
  if (memcmp(h.tag, "GYMX", 4))
    return;
  length = length * 50 / 3;  // 1000 / 60
  long loop = get_le32(h.loop_start);
  if (loop) {
    out->intro_length = loop * 50 / 3;
    out->loop_length = length - out->intro_length;
  } else {
    out->length = length;
    out->intro_length = length;  // make it clear that track is no longer than length
    out->loop_length = 0;
  }

  // more stupidity where the field should have been left
  if (strcmp(h.song, "Unknown Song"))
    GME_COPY_FIELD(h, out, song);

  if (strcmp(h.game, "Unknown Game"))
    GME_COPY_FIELD(h, out, game);

  if (strcmp(h.copyright, "Unknown Publisher"))
    GME_COPY_FIELD(h, out, copyright);

  if (strcmp(h.dumper, "Unknown Person"))
    GME_COPY_FIELD(h, out, dumper);

  if (strcmp(h.comment, "Header added by YMAMP"))
    GME_COPY_FIELD(h, out, comment);
}

static long gym_track_length(uint8_t const *p, uint8_t const *end) {
  long time = 0;
  while (p < end) {
    switch (*p++) {
      case 0:
        time++;
        break;
      case 1:
      case 2:
        p += 2;
        break;
      case 3:
        p += 1;
        break;
    }
  }
  return time;
}

blargg_err_t GymEmu::mGetTrackInfo(track_info_t *out, int) const {
  get_gym_info(header_, gym_track_length(data, data_end), out);
  return 0;
}

static blargg_err_t check_header(const uint8_t *in, long size, int *data_offset = nullptr) {
  if (size < 4)
    return gme_wrong_file_type;

  if (memcmp(in, "GYMX", 4) == 0) {
    if (size < GymEmu::header_size + 1)
      return gme_wrong_file_type;

    if (memcmp(((GymEmu::header_t const *) in)->packed, "\0\0\0\0", 4) != 0)
      return "Packed GYM file not supported";

    if (data_offset)
      *data_offset = GymEmu::header_size;
  } else if (*in > 3) {
    return gme_wrong_file_type;
  }

  return 0;
}

struct GymFile : GmeInfo {
  GymFile() { mSetType(gme_gym_type); }
  const uint8_t *file_begin;
  const uint8_t *file_end;
  int data_offset;

  blargg_err_t mLoad(uint8_t const *in, long size) override {
    file_begin = in;
    file_end = in + size;
    data_offset = 0;
    return check_header(in, size, &data_offset);
  }

  blargg_err_t mGetTrackInfo(track_info_t *out, int) const override {
    long length = gym_track_length(&file_begin[data_offset], file_end);
    get_gym_info(*(const GymEmu::header_t *) file_begin, length, out);
    return 0;
  }
};

static MusicEmu *new_gym_emu() { return BLARGG_NEW GymEmu; }
static MusicEmu *new_gym_file() { return BLARGG_NEW GymFile; }

static gme_type_t_ const gme_gym_type_ = {"Sega Genesis", 1, 0, &new_gym_emu, &new_gym_file, "GYM", 0};
extern gme_type_t const gme_gym_type = &gme_gym_type_;

// Setup

blargg_err_t GymEmu::mSetSampleRate(long sample_rate) {
  BlipEq eq(-32, 8000, sample_rate);
  mApu.setTrebleEq(eq);
  dac_synth.SetTrebleEq(eq);
  mApu.setVolume(0.135 * FM_GAIN * mGetGain());
  dac_synth.SetVolume(0.125 / 256 * FM_GAIN * mGetGain());
  double factor = DualResampler::setup(OVERSAMPLE_FACTOR, 0.990, FM_GAIN * mGetGain());
  fm_sample_rate = sample_rate * factor;

  RETURN_ERR(blip_buf.SetSampleRate(sample_rate, int(1000 / 60.0 / MIN_TEMPO)));
  blip_buf.SetClockRate(CLOCK_RATE);

  RETURN_ERR(fm.set_rate(fm_sample_rate, BASE_CLOCK / 7.0));
  RETURN_ERR(DualResampler::reset(long(1.0 / 60 / MIN_TEMPO * sample_rate)));

  return 0;
}

void GymEmu::mSetTempo(double t) {
  if (t < MIN_TEMPO) {
    SetTempo(MIN_TEMPO);
    return;
  }

  if (blip_buf.GetSampleRate()) {
    clocks_per_frame = long(CLOCK_RATE / 60 / mGetTempo());
    DualResampler::resize(long(GetSampleRate() / (60.0 * mGetTempo())));
  }
}

void GymEmu::mMuteChannel(int mask) {
  MusicEmu::mMuteChannel(mask);
  fm.mute_voices(mask);
  dac_muted = (mask & 0x40) != 0;
  mApu.SetOutput((mask & 0x80) ? 0 : &blip_buf);
}

blargg_err_t GymEmu::mLoad(uint8_t const *in, long size) {
  assert(offsetof(header_t, packed[4]) == header_size);
  int offset = 0;
  RETURN_ERR(check_header(in, size, &offset));
  mSetChannelsNumber(8);

  data = in + offset;
  data_end = in + size;
  loop_begin = 0;

  if (offset)
    header_ = *(header_t const *) in;
  else
    memset(&header_, 0, sizeof header_);

  return 0;
}

// Emulation

blargg_err_t GymEmu::mStartTrack(int track) {
  RETURN_ERR(MusicEmu::mStartTrack(track));

  pos = data;
  loop_remain = get_le32(header_.loop_start);

  prev_dac_count = 0;
  dac_enabled = false;
  dac_amp = -1;

  fm.reset();
  mApu.reset();
  blip_buf.Clear();
  DualResampler::clear();
  return 0;
}

void GymEmu::run_dac(int dac_count) {
  // Guess beginning and end of sample and adjust rate and buffer position
  // accordingly.

  // count dac samples in next frame
  int next_dac_count = 0;
  const uint8_t *p = this->pos;
  int cmd;
  while ((cmd = *p++) != 0) {
    int data = *p++;
    if (cmd <= 2)
      ++p;
    if (cmd == 1 && data == 0x2A)
      next_dac_count++;
  }

  // detect beginning and end of sample
  int rate_count = dac_count;
  int start = 0;
  if (!prev_dac_count && next_dac_count && dac_count < next_dac_count) {
    rate_count = next_dac_count;
    start = next_dac_count - dac_count;
  } else if (prev_dac_count && !next_dac_count && dac_count < prev_dac_count) {
    rate_count = prev_dac_count;
  }

  // Evenly space samples within buffer section being used
  blip_resampled_time_t period = blip_buf.ResampledDuration(clocks_per_frame) / rate_count;

  blip_resampled_time_t time = blip_buf.ResampledTime(0) + period * start + (period >> 1);

  int dac_amp = this->dac_amp;
  if (dac_amp < 0)
    dac_amp = dac_buf[0];

  for (int i = 0; i < dac_count; i++) {
    int delta = dac_buf[i] - dac_amp;
    dac_amp += delta;
    dac_synth.OffsetResampled(&blip_buf, time, delta);
    time += period;
  }
  this->dac_amp = dac_amp;
}

void GymEmu::parse_frame() {
  int dac_count = 0;
  const uint8_t *pos = this->pos;

  if (loop_remain && !--loop_remain)
    loop_begin = pos;  // find loop on first time through sequence

  int cmd;
  while ((cmd = *pos++) != 0) {
    int data = *pos++;
    if (cmd == 1) {
      int data2 = *pos++;
      if (data != 0x2A) {
        if (data == 0x2B)
          dac_enabled = (data2 & 0x80) != 0;

        fm.write0(data, data2);
      } else if (dac_count < (int) sizeof dac_buf) {
        dac_buf[dac_count] = data2;
        dac_count += dac_enabled;
      }
    } else if (cmd == 2) {
      fm.write1(data, *pos++);
    } else if (cmd == 3) {
      mApu.writeData(0, data);
    } else {
      // to do: many GYM streams are full of errors, and error count
      // should reflect cases where music is really having problems
      // log_error();
      --pos;  // put data back
    }
  }

  // loop
  if (pos >= data_end) {
    check(pos == data_end);

    if (loop_begin)
      pos = loop_begin;
    else
      mSetTrackEnded();
  }
  this->pos = pos;

  // dac
  if (dac_count && !dac_muted)
    run_dac(dac_count);
  prev_dac_count = dac_count;
}

int GymEmu::mPlayFrame(blip_time_t blip_time, int sample_count, sample_t *buf) {
  if (!IsTrackEnded())
    parse_frame();

  mApu.EndFrame(blip_time);

  memset(buf, 0, sample_count * sizeof *buf);
  fm.run(sample_count >> 1, buf);

  return sample_count;
}

blargg_err_t GymEmu::mPlay(long count, sample_t *out) {
  DualResampler::dualPlay(count, out, blip_buf);
  return 0;
}
