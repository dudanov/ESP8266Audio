// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "GmeFile.h"

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

const char *const gme_wrong_file_type = "Wrong file type for this emulator";

void GmeFile::clearPlaylist() {
  this->m_playlist.clear();
  this->m_clearPlaylist();
  this->m_trackNum = this->m_rawTrackCount;
}

void GmeFile::mUnload() {
  this->clearPlaylist();  // *before* clearing track count
  this->m_trackNum = 0;
  this->m_rawTrackCount = 0;
  this->mFileData.clear();
}

GmeFile::GmeFile() {
  this->mUnload();            // clears fields
  blargg_verify_byte_order();  // used by most emulator types, so save them the
                               // trouble
}

GmeFile::~GmeFile() {
  if (this->m_userCleanupFn != nullptr)
    this->m_userCleanupFn(this->m_userData);
}

blargg_err_t GmeFile::mLoad(const uint8_t *data, long size) {
  require(data != this->mFileData.begin());  // mLoad() or mLoad() must be overridden
  MemFileReader in(data, size);
  return this->mLoad(in);
}

blargg_err_t GmeFile::mLoad(DataReader &in) {
  RETURN_ERR(this->mFileData.resize(in.remain()));
  RETURN_ERR(in.read(this->mFileData.begin(), this->mFileData.size()));
  return this->mLoad(this->mFileData.begin(), this->mFileData.size());
}

// public load functions call this at beginning
void GmeFile::mPreLoad() { this->mUnload(); }

void GmeFile::mPostLoad() {}

// public load functions call this at end
blargg_err_t GmeFile::mPostLoad(blargg_err_t err) {
  if (!this->getTrackCount())
    this->m_setTrackNum(this->type()->track_count);
  if (!err)
    this->mPostLoad();
  else
    this->mUnload();

  return err;
}

// Public load functions

blargg_err_t GmeFile::loadMem(void const *in, long size) {
  this->mPreLoad();
  return this->mPostLoad(this->mLoad((uint8_t const *) in, size));
}

blargg_err_t GmeFile::load(DataReader &in) {
  this->mPreLoad();
  return this->mPostLoad(this->mLoad(in));
}

blargg_err_t GmeFile::loadFile(const char *path) {
  this->mPreLoad();
  GME_FILE_READER in;
  RETURN_ERR(in.open(path));
  return this->mPostLoad(this->mLoad(in));
}

blargg_err_t GmeFile::m_loadRemaining(void const *h, long s, DataReader &in) {
  RemainingReader rem(h, s, &in);
  return this->load(rem);
}

// Track info

void GmeFile::copyField(char *out, const char *in, int in_size) {
  if (!in || !*in)
    return;

  // remove spaces/junk from beginning
  while (in_size && unsigned(*in - 1) <= ' ' - 1) {
    in++;
    in_size--;
  }

  // truncate
  if (in_size > MAX_FIELD)
    in_size = MAX_FIELD;

  // find terminator
  int len = 0;
  while (len < in_size && in[len])
    len++;

  // remove spaces/junk from end
  while (len && unsigned(in[len - 1]) <= ' ')
    len--;

  // copy
  out[len] = 0;
  memcpy(out, in, len);

  // strip out stupid fields that should have been left blank
  if (!strcmp(out, "?") || !strcmp(out, "<?>") || !strcmp(out, "< ? >"))
    out[0] = 0;
}

void GmeFile::copyField(char *out, const char *in) { copyField(out, in, MAX_FIELD); }

blargg_err_t GmeFile::remapTrack(int *track_io) const {
  if ((unsigned) *track_io >= (unsigned) getTrackCount())
    return "Invalid track";

  if ((unsigned) *track_io < (unsigned) m_playlist.size()) {
    M3uPlaylist::entry_t const &e = m_playlist[*track_io];
    *track_io = 0;
    if (e.track >= 0) {
      *track_io = e.track;
      if (!(m_type->flags_ & 0x02))
        *track_io -= e.decimal_track;
    }
    if (*track_io >= m_rawTrackCount)
      return "Invalid track in m3u playlist";
  } else {
    check(!m_playlist.size());
  }
  return 0;
}

blargg_err_t GmeFile::GetTrackInfo(track_info_t *out, int track) const {
  out->track_count = getTrackCount();
  out->length = -1;
  out->loop_length = -1;
  out->intro_length = -1;
  out->fade_length = -1;
  out->song[0] = 0;

  out->game[0] = 0;
  out->author[0] = 0;
  out->copyright[0] = 0;
  out->comment[0] = 0;
  out->dumper[0] = 0;
  out->system[0] = 0;

  copyField(out->system, type()->system);

  int remapped = track;
  RETURN_ERR(remapTrack(&remapped));
  RETURN_ERR(mGetTrackInfo(out, remapped));

  // override with m3u info
  if (m_playlist.size()) {
    M3uPlaylist::info_t const &i = m_playlist.info();
    copyField(out->game, i.title);
    copyField(out->author, i.engineer);
    copyField(out->author, i.composer);
    copyField(out->dumper, i.ripping);

    M3uPlaylist::entry_t const &e = m_playlist[track];
    copyField(out->song, e.name);
    if (e.length >= 0)
      out->length = e.length;
    if (e.intro >= 0)
      out->intro_length = e.intro;
    if (e.loop >= 0)
      out->loop_length = e.loop;
    if (e.fade >= 0)
      out->fade_length = e.fade;
  }
  return 0;
}
