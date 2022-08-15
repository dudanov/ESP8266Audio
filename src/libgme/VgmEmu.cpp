// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "VgmEmu.h"

#include "blargg_endian.h"
#include <algorithm>
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

double const FM_GAIN = 3.0;  // FM emulators are internally quieter to avoid 16-bit overflow
double const rolloff = 0.990;
double const OVERSAMPLE_FACTOR = 1.0;

using std::max;
using std::min;

VgmEmu::VgmEmu() {
  disable_oversampling_ = false;
  m_psgRate = 0;
  m_setType(gme_vgm_type);

  static int const types[8] = {WAVE_TYPE | 1, WAVE_TYPE | 0, WAVE_TYPE | 2, NOISE_TYPE | 0};
  mSetChannelsTypes(types);

  mSetSilenceLookahead(1);  // tracks should already be trimmed

  SetEqualizer(MakeEqualizer(-14.0, 80));
}

VgmEmu::~VgmEmu() {}

// Track info

static uint8_t const *skip_gd3_str(uint8_t const *in, uint8_t const *end) {
  while (end - in >= 2) {
    in += 2;
    if (!(in[-2] | in[-1]))
      break;
  }
  return in;
}

static uint8_t const *get_gd3_str(uint8_t const *in, uint8_t const *end, char *field) {
  uint8_t const *mid = skip_gd3_str(in, end);
  int len = (mid - in) / 2 - 1;
  if (len > 0) {
    len = min(len, (int) GmeFile::MAX_FIELD);
    field[len] = 0;
    for (int i = 0; i < len; i++)
      field[i] = (in[i * 2 + 1] ? '?' : in[i * 2]);  // TODO: convert to utf-8
  }
  return mid;
}

static uint8_t const *get_gd3_pair(uint8_t const *in, uint8_t const *end, char *field) {
  return skip_gd3_str(get_gd3_str(in, end, field), end);
}

static void parse_gd3(uint8_t const *in, uint8_t const *end, track_info_t *out) {
  in = get_gd3_pair(in, end, out->song);
  in = get_gd3_pair(in, end, out->game);
  in = get_gd3_pair(in, end, out->system);
  in = get_gd3_pair(in, end, out->author);
  in = get_gd3_str(in, end, out->copyright);
  in = get_gd3_pair(in, end, out->dumper);
  in = get_gd3_str(in, end, out->comment);
}

int const gd3_header_size = 12;

static long check_gd3_header(uint8_t const *h, long remain) {
  if (remain < gd3_header_size)
    return 0;
  if (memcmp(h, "Gd3 ", 4))
    return 0;
  if (get_le32(h + 4) >= 0x200)
    return 0;

  long gd3_size = get_le32(h + 8);
  if (gd3_size > remain - gd3_header_size)
    return 0;

  return gd3_size;
}

uint8_t const *VgmEmu::gd3_data(int *size) const {
  if (size)
    *size = 0;

  long gd3_offset = get_le32(header().gd3_offset) - 0x2C;
  if (gd3_offset < 0)
    return 0;

  uint8_t const *gd3 = data + HEADER_SIZE + gd3_offset;
  long gd3_size = check_gd3_header(gd3, data_end - gd3);
  if (!gd3_size)
    return 0;

  if (size)
    *size = gd3_size + gd3_header_size;

  return gd3;
}

static void get_vgm_length(VgmEmu::header_t const &h, track_info_t *out) {
  long length = get_le32(h.track_duration) * 10 / 441;
  if (length > 0) {
    long loop = get_le32(h.loop_duration);
    if (loop > 0 && get_le32(h.loop_offset)) {
      out->loop_length = loop * 10 / 441;
      out->intro_length = length - out->loop_length;
    } else {
      out->length = length;        // 1000 / 44100 (VGM files used 44100 as timebase)
      out->intro_length = length;  // make it clear that track is no longer than length
      out->loop_length = 0;
    }
  }
}

blargg_err_t VgmEmu::mGetTrackInfo(track_info_t *out, int) const {
  get_vgm_length(header(), out);

  int size;
  uint8_t const *gd3 = gd3_data(&size);
  if (gd3)
    parse_gd3(gd3 + gd3_header_size, gd3 + size, out);

  return 0;
}

static blargg_err_t check_vgm_header(const VgmEmu::header_t &h) {
  if (memcmp(h.tag, "Vgm ", 4))
    return gme_wrong_file_type;
  return 0;
}

struct VgmFile : GmeInfo {
  VgmEmu::header_t h;
  blargg_vector<uint8_t> gd3;

  VgmFile() { m_setType(gme_vgm_type); }
  static MusicEmu *createVgmFile() { return BLARGG_NEW VgmFile; }

  blargg_err_t mLoad(DataReader &in) override {
    long file_size = in.remain();
    if (file_size <= VgmEmu::HEADER_SIZE)
      return gme_wrong_file_type;

    RETURN_ERR(in.read(&h, VgmEmu::HEADER_SIZE));
    RETURN_ERR(check_vgm_header(h));

    long gd3_offset = get_le32(h.gd3_offset) - 0x2C;
    long remain = file_size - VgmEmu::HEADER_SIZE - gd3_offset;
    uint8_t gd3_h[gd3_header_size];
    if (gd3_offset > 0 && remain >= gd3_header_size) {
      RETURN_ERR(in.skip(gd3_offset));
      RETURN_ERR(in.read(gd3_h, sizeof gd3_h));
      long gd3_size = check_gd3_header(gd3_h, remain);
      if (gd3_size) {
        RETURN_ERR(gd3.resize(gd3_size));
        RETURN_ERR(in.read(gd3.begin(), gd3.size()));
      }
    }
    return 0;
  }

  blargg_err_t mGetTrackInfo(track_info_t *out, int) const override {
    get_vgm_length(h, out);
    if (gd3.size())
      parse_gd3(gd3.begin(), gd3.end(), out);
    return 0;
  }
};

// Setup

void VgmEmu::mSetTempo(double t) {
  if (m_psgRate) {
    m_vgmRate = (long) (44100 * t + 0.5);
    blip_time_factor = (long) floor(double(1L << blip_time_bits) / m_vgmRate * m_psgRate + 0.5);
    // debug_printf( "blip_time_factor: %ld\n", blip_time_factor );
    // debug_printf( "m_vgmRate: %ld\n", m_vgmRate );
    // TODO: remove? calculates m_vgmRate more accurately (above differs at
    // most by one Hz only)
    // blip_time_factor = (long) floor( double (1L << blip_time_bits) *
    // m_psgRate / 44100 / t + 0.5 ); m_vgmRate = (long) floor( double (1L <<
    // blip_time_bits) * m_psgRate / blip_time_factor + 0.5 );

    fm_time_factor = 2 + (long) floor(m_fmRate * (1L << fm_time_bits) / m_vgmRate + 0.5);
  }
}

blargg_err_t VgmEmu::mSetSampleRate(long sample_rate) {
  RETURN_ERR(blip_buf.SetSampleRate(sample_rate, 1000 / 30));
  return ClassicEmu::mSetSampleRate(sample_rate);
}

blargg_err_t VgmEmu::SetMultiChannel(bool is_enabled) {
  // we acutally should check here whether this is classic emu or not
  // however SetMultiChannel() is called before setup_fm() resulting in
  // uninited is_classic_emu() hard code it to unsupported
#if 0
	if ( is_classic_emu() )
	{
		RETURN_ERR( Music_Emu::mSetMultiChannel( is_enabled ) );
		return 0;
	}
	else
#endif
  {
    (void) is_enabled;
    return "multichannel rendering not supported for YM2*** FM sound chip "
           "emulators";
  }
}

void VgmEmu::mUpdateEq(BlipEq const &eq) {
  psg[0].setTrebleEq(eq);
  if (psg_dual)
    psg[1].setTrebleEq(eq);
  dac_synth.setTrebleEq(eq);
}

void VgmEmu::mSetChannel(int i, BlipBuffer *c, BlipBuffer *l, BlipBuffer *r) {
  if (psg_dual) {
    if (psg_t6w28) {
      // TODO: Make proper output of each PSG chip: 0 - all right, 1 - all
      // left
      if (i < psg[0].OSCS_NUM)
        psg[0].setOscOutput(i, c, r, r);
      if (i < psg[1].OSCS_NUM)
        psg[1].setOscOutput(i, c, l, l);
    } else {
      if (i < psg[0].OSCS_NUM)
        psg[0].setOscOutput(i, c, l, r);
      if (i < psg[1].OSCS_NUM)
        psg[1].setOscOutput(i, c, l, r);
    }
  } else {
    if (i < psg[0].OSCS_NUM)
      psg[0].setOscOutput(i, c, l, r);
  }
}

void VgmEmu::mMuteChannel(int mask) {
  ClassicEmu::mMuteChannel(mask);
  dac_synth.SetOutput(&blip_buf);
  if (uses_fm) {
    psg[0].SetOutput((mask & 0x80) ? 0 : &blip_buf);
    if (psg_dual)
      psg[1].SetOutput((mask & 0x80) ? 0 : &blip_buf);
    if (ym2612[0].enabled()) {
      dac_synth.setVolume((mask & 0x40) ? 0.0 : 0.1115 / 256 * FM_GAIN * mGetGain());
      ym2612[0].mute_voices(mask);
      if (ym2612[1].enabled())
        ym2612[1].mute_voices(mask);
    }
    if (ym2413[0].enabled()) {
      int m = mask & 0x3F;
      if (mask & 0x20)
        m |= 0x01E0;  // channels 5-8
      if (mask & 0x40)
        m |= 0x3E00;
      ym2413[0].mute_voices(m);
      if (ym2413[1].enabled())
        ym2413[1].mute_voices(m);
    }
  }
}

blargg_err_t VgmEmu::mLoad(uint8_t const *new_data, long new_size) {
  assert(offsetof(header_t, unused2[8]) == HEADER_SIZE);

  if (new_size <= HEADER_SIZE)
    return gme_wrong_file_type;

  header_t const &h = *(header_t const *) new_data;

  RETURN_ERR(check_vgm_header(h));

  check(get_le32(h.version) <= 0x150);

  // psg rate
  m_psgRate = get_le32(h.psg_rate);
  if (!m_psgRate)
    m_psgRate = 3579545;
  psg_dual = (m_psgRate & 0x40000000) != 0;
  psg_t6w28 = (m_psgRate & 0x80000000) != 0;
  m_psgRate &= 0x0FFFFFFF;
  blip_buf.SetClockRate(m_psgRate);

  data = new_data;
  data_end = new_data + new_size;

  // get loop
  loop_begin = data_end;
  if (get_le32(h.loop_offset))
    loop_begin = &data[get_le32(h.loop_offset) + offsetof(header_t, loop_offset)];

  mSetChannelsNumber(psg[0].OSCS_NUM);

  RETURN_ERR(setup_fm());

  static const char *const FM_NAMES[] = {"FM 1", "FM 2", "FM 3", "FM 4", "FM 5", "FM 6", "PCM", "PSG"};
  static const char *const PSG_NAMES[] = {"Square 1", "Square 2", "Square 3", "Noise"};
  mSetChannelsNames(uses_fm ? FM_NAMES : PSG_NAMES);

  // do after FM in case output buffer is changed
  return ClassicEmu::mSetupBuffer(m_psgRate);
}

blargg_err_t VgmEmu::setup_fm() {
  long ym2612_rate = get_le32(header().ym2612_rate);
  bool ym2612_dual = (ym2612_rate & 0x40000000) != 0;
  long ym2413_rate = get_le32(header().ym2413_rate);
  bool ym2413_dual = (ym2413_rate & 0x40000000) != 0;
  if (ym2413_rate && get_le32(header().version) < 0x110)
    update_fm_rates(&ym2413_rate, &ym2612_rate);

  uses_fm = false;

  m_fmRate = blip_buf.GetSampleRate() * OVERSAMPLE_FACTOR;

  if (ym2612_rate) {
    ym2612_rate &= ~0xC0000000;
    uses_fm = true;
    if (disable_oversampling_)
      m_fmRate = ym2612_rate / 144.0;
    DualResampler::setup(m_fmRate / blip_buf.GetSampleRate(), rolloff, FM_GAIN * mGetGain());
    RETURN_ERR(ym2612[0].set_rate(m_fmRate, ym2612_rate));
    ym2612[0].enable(true);
    if (ym2612_dual) {
      RETURN_ERR(ym2612[1].set_rate(m_fmRate, ym2612_rate));
      ym2612[1].enable(true);
    }
    mSetChannelsNumber(8);
  }

  if (!uses_fm && ym2413_rate) {
    ym2413_rate &= ~0xC0000000;
    uses_fm = true;
    if (disable_oversampling_)
      m_fmRate = ym2413_rate / 72.0;
    DualResampler::setup(m_fmRate / blip_buf.GetSampleRate(), rolloff, FM_GAIN * mGetGain());
    int result = ym2413[0].set_rate(m_fmRate, ym2413_rate);
    if (result == 2)
      return "YM2413 FM sound isn't supported";
    CHECK_ALLOC(!result);
    ym2413[0].enable(true);
    if (ym2413_dual) {
      ym2413[1].enable(true);
      int result = ym2413[1].set_rate(m_fmRate, ym2413_rate);
      if (result == 2)
        return "YM2413 FM sound isn't supported";
      CHECK_ALLOC(!result);
    }
    mSetChannelsNumber(8);
  }

  if (uses_fm) {
    RETURN_ERR(DualResampler::reset(blip_buf.GetLength() * blip_buf.GetSampleRate() / 1000));
    psg[0].setVolume(0.135 * FM_GAIN * mGetGain());
    if (psg_dual)
      psg[1].setVolume(0.135 * FM_GAIN * mGetGain());
  } else {
    ym2612[0].enable(false);
    ym2612[1].enable(false);
    ym2413[0].enable(false);
    ym2413[1].enable(false);
    psg[0].setVolume(mGetGain());
    psg[1].setVolume(mGetGain());
  }

  return 0;
}

// Emulation

blargg_err_t VgmEmu::mStartTrack(int track) {
  RETURN_ERR(ClassicEmu::mStartTrack(track));
  psg[0].reset(get_le16(header().noise_feedback), header().noise_width);
  if (psg_dual)
    psg[1].reset(get_le16(header().noise_feedback), header().noise_width);

  dac_disabled = -1;
  pos = data + HEADER_SIZE;
  pcm_data = pos;
  pcm_pos = pos;
  dac_amp = -1;
  vgm_time = 0;
  if (get_le32(header().version) >= 0x150) {
    long data_offset = get_le32(header().data_offset);
    check(data_offset);
    if (data_offset)
      pos += data_offset + offsetof(header_t, data_offset) - 0x40;
  }

  if (uses_fm) {
    if (ym2413[0].enabled())
      ym2413[0].reset();

    if (ym2413[1].enabled())
      ym2413[1].reset();

    if (ym2612[0].enabled())
      ym2612[0].reset();

    if (ym2612[1].enabled())
      ym2612[1].reset();

    fm_time_offset = 0;
    blip_buf.Clear();
    DualResampler::clear();
  }
  return 0;
}

blargg_err_t VgmEmu::mRunClocks(blip_time_t &time_io, int msec) {
  time_io = run_commands(msec * m_vgmRate / 1000);
  psg[0].EndFrame(time_io);
  if (psg_dual)
    psg[1].EndFrame(time_io);
  return 0;
}

blargg_err_t VgmEmu::mPlay(long count, sample_t *out) {
  if (!uses_fm)
    return ClassicEmu::mPlay(count, out);

  DualResampler::dualPlay(count, out, blip_buf);
  return 0;
}

}  // namespace vgm
}  // namespace emu
}  // namespace gme

static gme_type_t_ const gme_vgm_type_ = {
    "Sega SMS/Genesis", 1, &gme::emu::vgm::VgmEmu::createVgmEmu, &gme::emu::vgm::VgmFile::createVgmFile, "VGM", 1};
extern gme_type_t const gme_vgm_type = &gme_vgm_type_;

static gme_type_t_ const gme_vgz_type_ = {
    "Sega SMS/Genesis", 1, &gme::emu::vgm::VgmEmu::createVgmEmu, &gme::emu::vgm::VgmFile::createVgmFile, "VGZ", 1};
extern gme_type_t const gme_vgz_type = &gme_vgz_type_;
