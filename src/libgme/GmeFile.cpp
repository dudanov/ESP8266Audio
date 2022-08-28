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

void GmeFile::ClearPlaylist() {
  mPlaylist.clear();
  mClearPlaylist();
  mTrackNum = mRawTrackCount;
}

void GmeFile::mUnload() {
  this->ClearPlaylist();  // *before* clearing track count
  mTrackNum = 0;
  mRawTrackCount = 0;
  mFileData.clear();
}

GmeFile::GmeFile() {
  mUnload();                   // clears fields
  blargg_verify_byte_order();  // used by most emulator types, so save them the
                               // trouble
}

GmeFile::~GmeFile() {
  if (mUserCleanupFn != nullptr)
    mUserCleanupFn(mUserData);
}

blargg_err_t GmeFile::mLoad(const uint8_t *data, long size) {
  require(data != mFileData.begin());  // mLoad() or mLoad() must be overridden
  MemFileReader in(data, size);
  return mLoad(in);
}

blargg_err_t GmeFile::mLoad(DataReader &in) {
  RETURN_ERR(mFileData.resize(in.remain()));
  RETURN_ERR(in.read(mFileData.begin(), mFileData.size()));
  return mLoad(mFileData.begin(), mFileData.size());
}

// public load functions call this at beginning
void GmeFile::mPreLoad() { mUnload(); }

void GmeFile::mPostLoad() {}

// public load functions call this at end
blargg_err_t GmeFile::mPostLoad(blargg_err_t err) {
  if (!this->GetTrackCount())
    mSetTrackNum(this->GetType()->track_count);
  if (!err)
    mPostLoad();
  else
    mUnload();

  return err;
}

// Public load functions

blargg_err_t GmeFile::LoadMem(void const *in, long size) {
  mPreLoad();
  return mPostLoad(mLoad((uint8_t const *) in, size));
}

blargg_err_t GmeFile::Load(DataReader &in) {
  mPreLoad();
  return mPostLoad(mLoad(in));
}

blargg_err_t GmeFile::mLoadRemaining(void const *h, long s, DataReader &in) {
  RemainingReader rem(h, s, &in);
  return this->Load(rem);
}

// Track info

const char *GmeFile::copyField(char *const out, const char *in, int in_size) {
  if (!in || !*in)
    return in;

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

  const auto next = in + len + 1;

  // remove spaces/junk from end
  while (len && unsigned(in[len - 1]) <= ' ')
    len--;

  // copy
  out[len] = 0;
  memcpy(out, in, len);

  // strip out stupid fields that should have been left blank
  if (!strcmp(out, "?") || !strcmp(out, "<?>") || !strcmp(out, "< ? >"))
    out[0] = 0;
  
  return next;
}

blargg_err_t GmeFile::RemapTrack(int *track_io) const {
  if ((unsigned) *track_io >= (unsigned) GetTrackCount())
    return "Invalid track";

  if ((unsigned) *track_io < (unsigned) mPlaylist.size()) {
    M3uPlaylist::entry_t const &e = mPlaylist[*track_io];
    *track_io = 0;
    if (e.track >= 0) {
      *track_io = e.track;
      if (!(mType->flags_ & 0x02))
        *track_io -= e.decimal_track;
    }
    if (*track_io >= mRawTrackCount)
      return "Invalid track in m3u playlist";
  } else {
    check(!mPlaylist.size());
  }
  return 0;
}

blargg_err_t GmeFile::GetTrackInfo(track_info_t *out, int track) const {
  out->track_count = GetTrackCount();
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

  copyField(out->system, GetType()->system);

  int remapped = track;
  RETURN_ERR(RemapTrack(&remapped));
  RETURN_ERR(mGetTrackInfo(out, remapped));

  // override with m3u info
  if (mPlaylist.size()) {
    M3uPlaylist::info_t const &i = mPlaylist.info();
    copyField(out->game, i.title);
    copyField(out->author, i.engineer);
    copyField(out->author, i.composer);
    copyField(out->dumper, i.ripping);

    M3uPlaylist::entry_t const &e = mPlaylist[track];
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
