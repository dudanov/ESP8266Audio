// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#define GME_DISABLE_STEREO_DEPTH 1

#include "MusicEmu.h"

#include "gme_types.h"
#if !GME_DISABLE_STEREO_DEPTH
#include "EffectsBuffer.h"
#endif
#include "blargg_endian.h"
#include <ctype.h>
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

gme_type_t const *gme_type_list() {
  static gme_type_t const gme_type_list_[] = {
#ifdef GME_TYPE_LIST
      GME_TYPE_LIST,
#else
#ifdef USE_GME_AY
      gme_ay_type,
#endif
#ifdef USE_GME_GBS
      gme_gbs_type,
#endif
#ifdef USE_GME_GYM
      gme_gym_type,
#endif
#ifdef USE_GME_HES
      gme_hes_type,
#endif
#ifdef USE_GME_KSS
      gme_kss_type,
#endif
#ifdef USE_GME_NSF
      gme_nsf_type,
#endif
#ifdef USE_GME_NSFE
      gme_nsfe_type,
#endif
#ifdef USE_GME_SAP
      gme_sap_type,
#endif
#ifdef USE_GME_SPC
      gme_spc_type,
      gme_rsn_type,
#endif
#ifdef USE_GME_VGM
      gme_vgm_type,
      gme_vgz_type,
#endif
#endif
      0};

  return gme_type_list_;
}

const char *gme_identify_header(void const *header) {
  switch (get_be32(header)) {
    case BLARGG_4CHAR('Z', 'X', 'A', 'Y'):
      return "AY";
    case BLARGG_4CHAR('G', 'B', 'S', 0x01):
      return "GBS";
    case BLARGG_4CHAR('G', 'Y', 'M', 'X'):
      return "GYM";
    case BLARGG_4CHAR('H', 'E', 'S', 'M'):
      return "HES";
    case BLARGG_4CHAR('K', 'S', 'C', 'C'):
    case BLARGG_4CHAR('K', 'S', 'S', 'X'):
      return "KSS";
    case BLARGG_4CHAR('N', 'E', 'S', 'M'):
      return "NSF";
    case BLARGG_4CHAR('N', 'S', 'F', 'E'):
      return "NSFE";
    case BLARGG_4CHAR('S', 'A', 'P', 0x0D):
      return "SAP";
    case BLARGG_4CHAR('S', 'N', 'E', 'S'):
      return "SPC";
    case BLARGG_4CHAR('R', 'a', 'r', '!'):
      return "RSN";
    case BLARGG_4CHAR('V', 'g', 'm', ' '):
      return "VGM";
  }
  if (get_be16(header) == BLARGG_2CHAR(0x1F, 0x8B))
    return "VGZ";
  return "";
}

static void to_uppercase(const char *in, int len, char *out) {
  for (int i = 0; i < len; i++) {
    if (!(out[i] = toupper(in[i])))
      return;
  }
  *out = 0;  // extension too long
}

gme_type_t gme_identify_extension(const char *extension_) {
  char const *end = strrchr(extension_, '.');
  if (end)
    extension_ = end + 1;

  char extension[6];
  to_uppercase(extension_, sizeof extension, extension);

  for (gme_type_t const *types = gme_type_list(); *types; types++)
    if (!strcmp(extension, (*types)->extension_))
      return *types;
  return 0;
}

const char *gme_type_extension(gme_type_t music_type) {
  const gme_type_t_ *const music_typeinfo = static_cast<const gme_type_t_ *>(music_type);
  if (music_type)
    return music_typeinfo->extension_;
  return "";
}

gme_err_t gme_identify_file(const char *path, gme_type_t *type_out) {
  *type_out = gme_identify_extension(path);
  // TODO: don't examine header if file has extension?
  if (!*type_out) {
    char header[4];
    GME_FILE_READER in;
    RETURN_ERR(in.open(path));
    RETURN_ERR(in.read(header, sizeof header));
    *type_out = gme_identify_extension(gme_identify_header(header));
  }
  return 0;
}

gme_err_t gme_open_data(void const *data, long size, MusicEmu **out, int sample_rate) {
  require((data || !size) && out);
  *out = 0;

  gme_type_t file_type = 0;
  if (size >= 4)
    file_type = gme_identify_extension(gme_identify_header(data));
  if (!file_type)
    return gme_wrong_file_type;

  MusicEmu *emu = gme_new_emu(file_type, sample_rate);
  CHECK_ALLOC(emu);

  gme_err_t err = gme_load_data(emu, data, size);

  if (err)
    delete emu;
  else
    *out = emu;

  return err;
}

gme_err_t gme_open_file(const char *path, MusicEmu **out, int sample_rate) {
  require(path && out);
  *out = 0;

  GME_FILE_READER in;
  RETURN_ERR(in.open(path));

  char header[4];
  int header_size = 0;

  gme_type_t file_type = gme_identify_extension(path);
  if (!file_type) {
    header_size = sizeof header;
    RETURN_ERR(in.read(header, sizeof header));
    file_type = gme_identify_extension(gme_identify_header(header));
  }
  if (!file_type)
    return gme_wrong_file_type;

  MusicEmu *emu = gme_new_emu(file_type, sample_rate);
  CHECK_ALLOC(emu);

  // optimization: avoids seeking/re-reading header
  RemainingReader rem(header, header_size, &in);
  gme_err_t err = emu->load(rem);
  in.close();

  if (emu->m_isArchive)
    err = emu->loadArchive(path);

  if (err)
    delete emu;
  else
    *out = emu;

  return err;
}

void gme_set_autoload_playback_limit(MusicEmu *emu, int do_autoload_limit) {
  emu->setAutoloadPlaybackLimit(do_autoload_limit != 0);
}

int gme_autoload_playback_limit(MusicEmu *const emu) { return emu->autoloadPlaybackLimit(); }

// Used to implement gme_new_emu and gme_new_emu_multi_channel
MusicEmu *m_gmeInternalNewEmu(gme_type_t type, int rate, bool multi_channel) {
  if (type) {
    if (rate == gme_info_only)
      return type->new_info();

    MusicEmu *me = type->new_emu();
    if (me) {
#if !GME_DISABLE_STEREO_DEPTH
      me->setMultiChannel(multi_channel);

      if (type->flags_ & 1) {
        if (me->getMultiChannel()) {
          me->m_effectsBuffer = BLARGG_NEW EffectsBuffer(8);
        } else {
          me->m_effectsBuffer = BLARGG_NEW EffectsBuffer(1);
        }
        if (me->m_effectsBuffer)
          me->setBuffer(me->m_effectsBuffer);
      }

      if (!(type->flags_ & 1) || me->m_effectsBuffer)
#endif
      {
        if (!me->setSampleRate(rate)) {
          check(me->type() == type);
          return me;
        }
      }
      delete me;
    }
  }
  return 0;
}

MusicEmu *gme_new_emu(gme_type_t type, int rate) {
  return m_gmeInternalNewEmu(type, rate, false /* no multichannel */);
}

MusicEmu *gme_new_emu_multi_channel(gme_type_t type, int rate) {
  // multi-channel emulator (if possible, not all emu types support
  // multi-channel)
  return m_gmeInternalNewEmu(type, rate, true /* multichannel */);
}

gme_err_t gme_load_file(MusicEmu *me, const char *path) { return me->loadFile(path); }

gme_err_t gme_load_data(MusicEmu *me, void const *data, long size) {
  MemFileReader in(data, size);
  return me->load(in);
}

gme_err_t gme_load_custom(MusicEmu *me, gme_reader_t func, long size, void *data) {
  CallbackReader in(func, size, data);
  return me->load(in);
}

void gme_delete(MusicEmu *me) { delete me; }

gme_type_t gme_type(MusicEmu const *me) { return me->type(); }

const char *gme_warning(MusicEmu *me) { return me->warning(); }

int gme_track_count(MusicEmu const *me) { return me->getTrackCount(); }

struct gme_info_t_ : gme_info_t {
  track_info_t info;
};

gme_err_t gme_track_info(MusicEmu const *me, gme_info_t **out, int track) {
  *out = NULL;

  gme_info_t_ *info = BLARGG_NEW gme_info_t_;
  CHECK_ALLOC(info);

  gme_err_t err = me->getTrackInfo(&info->info, track);
  if (err) {
    gme_free_info(info);
    return err;
  }

#define COPY(name) info->name = info->info.name;

  COPY(length);
  COPY(intro_length);
  COPY(loop_length);
  COPY(fade_length);

  info->i5 = -1;
  info->i6 = -1;
  info->i7 = -1;
  info->i8 = -1;
  info->i9 = -1;
  info->i10 = -1;
  info->i11 = -1;
  info->i12 = -1;
  info->i13 = -1;
  info->i14 = -1;
  info->i15 = -1;

  info->s7 = "";
  info->s8 = "";
  info->s9 = "";
  info->s10 = "";
  info->s11 = "";
  info->s12 = "";
  info->s13 = "";
  info->s14 = "";
  info->s15 = "";

  COPY(system);
  COPY(game);
  COPY(song);
  COPY(author);
  COPY(copyright);
  COPY(comment);
  COPY(dumper);

#undef COPY

  info->play_length = info->length;
  if (info->play_length <= 0) {
    info->play_length = info->intro_length + 2 * info->loop_length;  // intro + 2 loops
    if (info->play_length <= 0)
      info->play_length = 150 * 1000;  // 2.5 minutes
  }

  *out = info;

  return 0;
}

void gme_free_info(gme_info_t *info) { delete STATIC_CAST(gme_info_t_ *, info); }

void gme_set_stereo_depth(MusicEmu *me, double depth) {
#if !GME_DISABLE_STEREO_DEPTH
  if (me->m_effectsBuffer)
    STATIC_CAST(EffectsBuffer *, me->m_effectsBuffer)->setDepth(depth);
#endif
}

void *gme_user_data(MusicEmu const *me) { return me->getUserData(); }
void gme_set_user_data(MusicEmu *me, void *new_user_data) { me->setUserData(new_user_data); }
void gme_set_user_cleanup(MusicEmu *me, gme_user_cleanup_t func) { me->setUserCleanupFn(func); }

gme_err_t gme_start_track(MusicEmu *me, int index) { return me->startTrack(index); }
gme_err_t gme_play(MusicEmu *me, int n, short *p) { return me->play(n, p); }
void gme_set_fade(MusicEmu *me, int start_msec, int fade_msec) { me->setFade(start_msec, fade_msec); }
int gme_track_ended(MusicEmu const *me) { return me->isTrackEnded(); }
int gme_tell(MusicEmu const *me) { return me->tell(); }
int gme_tell_samples(MusicEmu const *me) { return me->tellSamples(); }
gme_err_t gme_seek(MusicEmu *me, int msec) { return me->seek(msec); }
gme_err_t gme_seek_samples(MusicEmu *me, int n) { return me->seekSamples(n); }
int gme_voice_count(MusicEmu const *me) { return me->getChannelsNum(); }
void gme_ignore_silence(MusicEmu *me, int disable) { me->setIgnoreSilence(disable != 0); }
void gme_set_tempo(MusicEmu *me, double t) { me->setTempo(t); }
void gme_mute_voice(MusicEmu *me, int index, int mute) { me->muteChannel(index, mute != 0); }
void gme_mute_voices(MusicEmu *me, int mask) { me->muteChannels(mask); }
void gme_enable_accuracy(MusicEmu *me, int enabled) { me->setAccuracy(enabled); }
void gme_clear_playlist(MusicEmu *me) { me->clearPlaylist(); }
int gme_type_multitrack(gme_type_t t) { return t->track_count != 1; }
int gme_multi_channel(MusicEmu const *me) { return me->getMultiChannel(); }

void gme_set_equalizer(MusicEmu *me, gme_equalizer_t const *eq) {
  MusicEmu::equalizer_t e = me->GetEqualizer();
  e.treble = eq->treble;
  e.bass = eq->bass;
  me->SetEqualizer(e);
}

void gme_equalizer(MusicEmu const *me, gme_equalizer_t *out) {
  gme_equalizer_t e = gme_equalizer_t();  // Default-init all fields to 0.0f
  e.treble = me->GetEqualizer().treble;
  e.bass = me->GetEqualizer().bass;
  *out = e;
}

const char *gme_voice_name(MusicEmu const *me, int i) {
  assert((unsigned) i < (unsigned) me->getChannelsNum());
  return me->getChannelsNames()[i];
}

const char *gme_type_system(gme_type_t type) {
  assert(type);
  return type->system;
}
