// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "NsfeEmu.h"

#include "blargg_endian.h"
#include <algorithm>
#include <ctype.h>
#include <string.h>

/* Copyright (C) 2005-2006 Shay Green. This module is free software; you
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
namespace nes {

NsfeInfo::NsfeInfo() { playlist_disabled = false; }

NsfeInfo::~NsfeInfo() {}

inline void NsfeInfo::unload() {
  track_name_data.clear();
  track_names.clear();
  playlist.clear();
  track_times.clear();
}

// TODO: if no playlist, treat as if there is a playlist that is just
// 1,2,3,4,5... ?
void NsfeInfo::disable_playlist(bool b) {
  playlist_disabled = b;
  info.track_count = playlist.size();
  if (!info.track_count || playlist_disabled)
    info.track_count = actual_track_count_;
}

int NsfeInfo::remap_track(int track) const {
  if (!playlist_disabled && (unsigned) track < playlist.size())
    track = playlist[track];
  return track;
}

// Read multiple strings and separate into individual strings
static blargg_err_t read_strs(DataReader &in, long size, blargg_vector<char> &chars,
                              blargg_vector<const char *> &strs) {
  RETURN_ERR(chars.resize(size + 1));
  chars[size] = 0;  // in case last string doesn't have terminator
  RETURN_ERR(in.read(&chars[0], size));

  RETURN_ERR(strs.resize(128));
  int count = 0;
  for (int i = 0; i < size; i++) {
    if ((int) strs.size() <= count)
      RETURN_ERR(strs.resize(count * 2));
    strs[count++] = &chars[i];
    while (i < size && chars[i])
      i++;
  }

  return strs.resize(count);
}

// Copy in to out, where out has out_max characters allocated. Truncate to
// out_max - 1 characters.
static void copy_str(const char *in, char *out, int out_max) {
  out[out_max - 1] = 0;
  strncpy(out, in, out_max - 1);
}

struct nsfe_info_t {
  uint8_t load_addr[2];
  uint8_t init_addr[2];
  uint8_t play_addr[2];
  uint8_t speed_flags;
  uint8_t chip_flags;
  uint8_t track_count;
  uint8_t first_track;
  uint8_t unused[6];
};

blargg_err_t NsfeInfo::load(DataReader &in, NsfEmu *nsf_emu) {
  int const nsfe_info_size = 16;
  assert(offsetof(nsfe_info_t, unused[6]) == nsfe_info_size);

  // check header
  uint8_t signature[4];
  blargg_err_t err = in.read(signature, sizeof signature);
  if (err)
    return (err == in.eof_error ? gme_wrong_file_type : err);
  if (memcmp(signature, "NSFE", 4))
    return gme_wrong_file_type;

  // free previous info
  track_name_data.clear();
  track_names.clear();
  playlist.clear();
  track_times.clear();

  // default nsf header
  static const NsfEmu::Header base_header = {
      {'N', 'E', 'S', 'M', '\x1A'},  // tag
      1,                             // version
      1,
      1,  // track count, first track
      {0, 0},
      {0, 0},
      {0, 0},  // addresses
      "",
      "",
      "",                        // strings
      {0x1A, 0x41},              // NTSC rate
      {0, 0, 0, 0, 0, 0, 0, 0},  // banks
      {0x20, 0x4E},              // PAL rate
      0,
      0,            // flags
      {0, 0, 0, 0}  // unused
  };
  NsfEmu::Header &header = info;
  header = base_header;

  // parse tags
  int phase = 0;
  while (phase != 3) {
    // read size and tag
    uint8_t block_header[2][4];
    RETURN_ERR(in.read(block_header, sizeof block_header));
    blargg_long size = get_le32(block_header[0]);
    blargg_long tag = get_le32(block_header[1]);

    if (size < 0)
      return "Corrupt file";

    // debug_printf( "tag: %c%c%c%c\n", char(tag), char(tag>>8),
    // char(tag>>16), char(tag>>24) );

    switch (tag) {
      case BLARGG_4CHAR('O', 'F', 'N', 'I'): {
        check(phase == 0);
        if (size < 8)
          return "Corrupt file";

        nsfe_info_t finfo;
        finfo.track_count = 1;
        finfo.first_track = 0;

        RETURN_ERR(in.read(&finfo, std::min(size, (blargg_long) nsfe_info_size)));
        if (size > nsfe_info_size)
          RETURN_ERR(in.skip(size - nsfe_info_size));
        phase = 1;
        info.speed_flags = finfo.speed_flags;
        info.chip_flags = finfo.chip_flags;
        info.track_count = finfo.track_count;
        this->actual_track_count_ = finfo.track_count;
        info.first_track = finfo.first_track;
        memcpy(info.load_addr, finfo.load_addr, 2 * 3);
        break;
      }

      case BLARGG_4CHAR('K', 'N', 'A', 'B'):
        if (size > (int) sizeof info.banks)
          return "Corrupt file";
        RETURN_ERR(in.read(info.banks, size));
        break;

      case BLARGG_4CHAR('h', 't', 'u', 'a'): {
        blargg_vector<char> chars;
        blargg_vector<const char *> strs;
        RETURN_ERR(read_strs(in, size, chars, strs));
        int n = strs.size();

        if (n > 3)
          copy_str(strs[3], info.dumper, sizeof info.dumper);

        if (n > 2)
          copy_str(strs[2], info.copyright, sizeof info.copyright);

        if (n > 1)
          copy_str(strs[1], info.author, sizeof info.author);

        if (n > 0)
          copy_str(strs[0], info.game, sizeof info.game);

        break;
      }

      case BLARGG_4CHAR('e', 'm', 'i', 't'):
        RETURN_ERR(track_times.resize(size / 4));
        RETURN_ERR(in.read(track_times.begin(), track_times.size() * 4));
        break;

      case BLARGG_4CHAR('l', 'b', 'l', 't'):
        RETURN_ERR(read_strs(in, size, track_name_data, track_names));
        break;

      case BLARGG_4CHAR('t', 's', 'l', 'p'):
        RETURN_ERR(playlist.resize(size));
        RETURN_ERR(in.read(&playlist[0], size));
        break;

      case BLARGG_4CHAR('A', 'T', 'A', 'D'): {
        check(phase == 1);
        phase = 2;
        if (!nsf_emu) {
          RETURN_ERR(in.skip(size));
        } else {
          SubsetReader sub(&in, size);  // limit emu to nsf data
          RemainingReader rem(&header, NsfEmu::HEADER_SIZE, &sub);
          RETURN_ERR(nsf_emu->load(rem));
          check(rem.remain() == 0);
        }
        break;
      }

      case BLARGG_4CHAR('D', 'N', 'E', 'N'):
        check(phase == 2);
        phase = 3;
        break;

      default:
        // tags that can be skipped start with a lowercase character
        check(islower((tag >> 24) & 0xFF));
        RETURN_ERR(in.skip(size));
        break;
    }
  }

  return 0;
}

blargg_err_t NsfeInfo::track_info_(track_info_t *out, int track) const {
  int remapped = remap_track(track);
  if ((unsigned) remapped < track_times.size()) {
    long length = (int32_t) get_le32(track_times[remapped]);
    if (length > 0)
      out->length = length;
  }
  if ((unsigned) remapped < track_names.size())
    GmeFile::copyField(out->song, track_names[remapped]);

  GME_COPY_FIELD(info, out, game);
  GME_COPY_FIELD(info, out, author);
  GME_COPY_FIELD(info, out, copyright);
  GME_COPY_FIELD(info, out, dumper);
  return 0;
}

NsfeEmu::NsfeEmu() {
  loading = false;
  mSetType(gme_nsfe_type);
}

NsfeEmu::~NsfeEmu() {}

void NsfeEmu::mUnload() {
  if (!loading)
    info.unload();  // TODO: extremely hacky!
  NsfEmu::mUnload();
}

blargg_err_t NsfeEmu::mGetTrackInfo(track_info_t *out, int track) const { return info.track_info_(out, track); }

struct NsfeFile : GmeInfo {
  NsfeInfo info;

  NsfeFile() { mSetType(gme_nsfe_type); }
  static MusicEmu *createNsfeFile() { return BLARGG_NEW NsfeFile; }

  blargg_err_t mLoad(DataReader &in) override {
    RETURN_ERR(info.load(in, 0));
    info.disable_playlist(false);
    m_setTrackNum(info.info.track_count);
    return 0;
  }

  blargg_err_t mGetTrackInfo(track_info_t *out, int track) const override { return info.track_info_(out, track); }
};

blargg_err_t NsfeEmu::mLoad(DataReader &in) {
  if (loading)
    return NsfEmu::mLoad(in);

  // TODO: this hacky recursion-avoidance could have subtle problems
  loading = true;
  blargg_err_t err = info.load(in, this);
  loading = false;
  disable_playlist(false);
  return err;
}

void NsfeEmu::disable_playlist(bool b) {
  info.disable_playlist(b);
  m_setTrackNum(info.info.track_count);
}

void NsfeEmu::m_clearPlaylist() {
  disable_playlist();
  NsfEmu::m_clearPlaylist();
}

blargg_err_t NsfeEmu::mStartTrack(int track) { return NsfEmu::mStartTrack(info.remap_track(track)); }

}  // namespace nes
}  // namespace emu
}  // namespace gme

static gme_type_t_ const gme_nsfe_type_ = {
    "Nintendo NES", 0, 0, &gme::emu::nes::NsfeEmu::createNsfeEmu, &gme::emu::nes::NsfeFile::createNsfeFile, "NSFE", 1};
extern gme_type_t const gme_nsfe_type = &gme_nsfe_type_;
