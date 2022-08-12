// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "SpcEmu.h"

#include "blargg_endian.h"
#include <algorithm>
#include <stdlib.h>
#include <string.h>

/* Copyright (C) 2004-2006 Shay Green. This module is free software; you
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
namespace snes {

#ifdef RARDLL
#define PASCAL
#define CALLBACK
#define LONG long
#define HANDLE void *
#define LPARAM intptr_t
#define UINT __attribute__((unused)) unsigned int
#include <dll.hpp>
#endif

// TODO: support Spc_Filter's bass

SpcEmu::SpcEmu(gme_type_t type) {
  static const char *const CHANNELS_NAMES[] = {"DSP 1", "DSP 2", "DSP 3", "DSP 4", "DSP 5", "DSP 6", "DSP 7", "DSP 8"};
  this->m_setChannelsNames(CHANNELS_NAMES);
  this->m_setType(type);
  this->setGain(1.4);
}

// Track info

static const long SPC_SIZE = SnesSpc::SPC_FILE_SIZE;
static const long HEAD_SIZE = SpcEmu::HEADER_SIZE;

const uint8_t *SpcEmu::m_trailer() const { return this->m_fileData + std::min(this->m_fileSize, SPC_SIZE); }
long SpcEmu::m_trailerSize() const { return std::max(0L, this->m_fileSize - SPC_SIZE); }

const uint8_t *RsnEmu::m_trailer(int trackIdx) const {
  auto trackData = this->m_spc[trackIdx];
  long trackSize = this->m_spc[trackIdx + 1] - trackData;
  return trackData + std::min(trackSize, SPC_SIZE);
}

long RsnEmu::m_trailerSize(int trackIdx) const {
  long trackSize = this->m_spc[trackIdx + 1] - this->m_spc[trackIdx];
  return std::max(0L, trackSize - SPC_SIZE);
}

static void get_spc_xid6(const uint8_t *begin, long size, track_info_t *out) {
  // header
  const uint8_t *end = begin + size;
  if (size < 8 || memcmp(begin, "xid6", 4)) {
    check(false);
    return;
  }
  long infoSize = get_le32(begin + 4);
  const uint8_t *in = begin + 8;
  if (end - in > infoSize) {
    debug_printf("Extra data after SPC xid6 info\n");
    end = in + infoSize;
  }

  int year = 0;
  char copyright[256 + 5];
  int copyrightLen = 0;
  const int yearLen = 5;

  while (end - in >= 4) {
    // header
    int id = in[0];
    int type = in[1];
    int data = get_le16(in + 2);
    int len = type ? data : 0;
    in += 4;
    if (len > end - in) {
      check(false);
      break;  // block goes past end of data
    }

    // handle specific block types
    char *field = 0;
    switch (id) {
      case 0x01:
        field = out->song;
        break;
      case 0x02:
        field = out->game;
        break;
      case 0x03:
        field = out->author;
        break;
      case 0x04:
        field = out->dumper;
        break;
      case 0x07:
        field = out->comment;
        break;
      case 0x14:
        year = data;
        break;

        // case 0x30: // intro length
        // Many SPCs have intro length set wrong for looped tracks, making
        // it useless
        /*
        case 0x30:
                check( len == 4 );
                if ( len >= 4 )
                {
                        out->intro_length = get_le32( in ) / 64;
                        if ( out->length > 0 )
                        {
                                long loop = out->length - out->intro_length;
                                if ( loop >= 2000 )
                                        out->loop_length = loop;
                        }
                }
                break;
        */

      case 0x13:
        copyrightLen = std::min(len, (int) sizeof(copyright) - yearLen);
        memcpy(&copyright[yearLen], in, copyrightLen);
        break;

      default:
        if (id < 0x01 || (id > 0x07 && id < 0x10) || (id > 0x14 && id < 0x30) || id > 0x36)
          debug_printf("Unknown SPC xid6 block: %X\n", (int) id);
        break;
    }
    if (field != nullptr) {
      check(type == 1);
      GmeFile::copyField(field, (char const *) in, len);
    }

    // skip to next block
    in += len;

    // blocks are supposed to be 4-byte aligned with zero-padding...
    const uint8_t *unaligned = in;
    while ((in - begin) & 3 && in < end) {
      if (*in++ != 0) {
        // ...but some files have no padding
        in = unaligned;
        debug_printf("SPC info tag wasn't properly padded to align\n");
        break;
      }
    }
  }

  char *p = &copyright[yearLen];
  if (year) {
    *--p = ' ';
    for (int n = 4; n--;) {
      *--p = char(year % 10 + '0');
      year /= 10;
    }
    copyrightLen += yearLen;
  }
  if (copyrightLen)
    GmeFile::copyField(out->copyright, p, copyrightLen);

  check(in == end);
}

static void get_spc_info(const SpcEmu::header_t &hdr, const uint8_t *xid6, long xid6_size, track_info_t *out) {
  // decode length (can be in text or binary format, sometimes ambiguous ugh)
  long len_secs = 0;
  for (int i = 0; i < 3; i++) {
    unsigned n = hdr.len_secs[i] - '0';
    if (n > 9) {
      // ignore single-digit text lengths
      // (except if author field is present and begins at offset 1, ugh)
      if (i == 1 && (hdr.author[0] || !hdr.author[1]))
        len_secs = 0;
      break;
    }
    len_secs *= 10;
    len_secs += n;
  }
  if (!len_secs || len_secs > 0x1FFF)
    len_secs = get_le16(hdr.len_secs);
  if (len_secs < 0x1FFF)
    out->length = len_secs * 1000;

  int offset = (hdr.author[0] < ' ' || unsigned(hdr.author[0] - '0') <= 9);
  GmeFile::copyField(out->author, &hdr.author[offset], sizeof hdr.author - offset);

  GME_COPY_FIELD(hdr, out, song);
  GME_COPY_FIELD(hdr, out, game);
  GME_COPY_FIELD(hdr, out, dumper);
  GME_COPY_FIELD(hdr, out, comment);

  if (xid6_size)
    get_spc_xid6(xid6, xid6_size, out);
}

blargg_err_t SpcEmu::mGetTrackInfo(track_info_t *out, int) const {
  get_spc_info(m_header(), this->m_trailer(), this->m_trailerSize(), out);
  return 0;
}

blargg_err_t RsnEmu::mGetTrackInfo(track_info_t *out, int track) const {
  get_spc_info(header(track), m_trailer(track), m_trailerSize(track), out);
  return 0;
}

static blargg_err_t check_spc_header(void const *header) {
  if (memcmp(header, "SNES-SPC700 Sound File Data", 27))
    return gme_wrong_file_type;
  return 0;
}

struct SpcFile : GmeInfo {
  SpcEmu::header_t header;
  blargg_vector<uint8_t> xid6;

  SpcFile(gme_type_t type) { m_setType(type); }
  SpcFile() : SpcFile(gme_spc_type) {}
  static MusicEmu *createSpcFile() { return BLARGG_NEW SpcFile; }

  blargg_err_t mLoad(DataReader &src) override {
    if (this->m_isArchive)
      return 0;
    long fileSize = src.remain();
    if (fileSize < SnesSpc::SPC_MIN_FILE_SIZE)
      return gme_wrong_file_type;
    RETURN_ERR(src.read(&header, HEAD_SIZE));
    RETURN_ERR(check_spc_header(header.tag));
    long xid6_size = fileSize - SPC_SIZE;
    if (xid6_size > 0) {
      RETURN_ERR(xid6.resize(xid6_size));
      RETURN_ERR(src.skip(SPC_SIZE - HEAD_SIZE));
      RETURN_ERR(src.read(xid6.begin(), xid6.size()));
    }
    return 0;
  }

  blargg_err_t mGetTrackInfo(track_info_t *out, int) const override {
    get_spc_info(header, xid6.begin(), xid6.size(), out);
    return 0;
  }
};

#ifdef RARDLL
static int CALLBACK call_rsn(UINT msg, LPARAM UserData, LPARAM P1, LPARAM P2) {
  uint8_t **bp = (uint8_t **) UserData;
  unsigned char *addr = (unsigned char *) P1;
  memcpy(*bp, addr, P2);
  *bp += P2;
  return 0;
}
#endif

struct RsnFile : SpcFile {
  blargg_vector<uint8_t *> m_spc;

  RsnFile() : SpcFile(gme_rsn_type) { m_isArchive = true; }
  static MusicEmu *createRsnFile() { return BLARGG_NEW RsnFile; }

  blargg_err_t loadArchive(const char *path) {
#ifdef RARDLL
    struct RAROpenArchiveData data = {.ArcName = (char *) path,
                                      .OpenMode = RAR_OM_LIST,
                                      .OpenResult = 0,
                                      .CmtBuf = 0,
                                      .CmtBufSize = 0,
                                      .CmtSize = 0,
                                      .CmtState = 0};

    // get the size of all unpacked headers combined
    long pos = 0;
    int count = 0;
    unsigned biggest = 0;
    blargg_vector<uint8_t> temp;
    HANDLE PASCAL rar = RAROpenArchive(&data);
    struct RARHeaderData head;
    for (; RARReadHeader(rar, &head) == ERAR_SUCCESS; count++) {
      RARProcessFile(rar, RAR_SKIP, 0, 0);
      long xid6_size = head.UnpSize - SPC_SIZE;
      if (xid6_size > 0)
        pos += xid6_size;
      pos += HEAD_SIZE;
      biggest = std::max(biggest, head.UnpSize);
    }
    xid6.resize(pos);
    m_spc.resize(count);
    temp.resize(biggest);
    RARCloseArchive(rar);

    // copy the headers/xid6 and index them
    uint8_t *bp;
    data.OpenMode = RAR_OM_EXTRACT;
    rar = RAROpenArchive(&data);
    RARSetCallback(rar, call_rsn, (intptr_t) &bp);
    for (count = 0, pos = 0; RARReadHeader(rar, &head) == ERAR_SUCCESS;) {
      bp = &temp[0];
      RARProcessFile(rar, RAR_TEST, 0, 0);
      if (!check_spc_header(bp - head.UnpSize)) {
        m_spc[count++] = &xid6[pos];
        memcpy(&xid6[pos], &temp[0], HEAD_SIZE);
        pos += HEAD_SIZE;
        long xid6_size = head.UnpSize - SPC_SIZE;
        if (xid6_size > 0) {
          memcpy(&xid6[pos], &temp[SPC_SIZE], xid6_size);
          pos += xid6_size;
        }
      }
    }
    m_spc[count] = &xid6[pos];
    m_setTrackNum(count);
    RARCloseArchive(rar);

    return 0;
#else
    (void) path;
    return gme_wrong_file_type;
#endif
  }

  blargg_err_t mGetTrackInfo(track_info_t *out, int track) const {
    if (static_cast<size_t>(track) >= m_spc.size())
      return "Invalid track";
    long xid6_size = m_spc[track + 1] - (m_spc[track] + HEAD_SIZE);
    get_spc_info(*(SpcEmu::header_t const *) m_spc[track], m_spc[track] + HEAD_SIZE, xid6_size, out);
    return 0;
  }
};

// Setup

blargg_err_t SpcEmu::mSetSampleRate(long sample_rate) {
  RETURN_ERR(m_apu.init());
  setAccuracy(false);
  if (sample_rate != NATIVE_SAMPLE_RATE) {
    RETURN_ERR(m_resampler.setBufferSize(NATIVE_SAMPLE_RATE / 20 * 2));
    m_resampler.setTimeRatio((double) NATIVE_SAMPLE_RATE / sample_rate, 0.9965);
  }
  return 0;
}

void SpcEmu::m_setAccuracy(bool b) {
  MusicEmu::m_setAccuracy(b);
  m_filter.SetEnable(b);
}

void SpcEmu::mMuteChannel(int m) {
  MusicEmu::mMuteChannel(m);
  m_apu.mute_voices(m);
}

blargg_err_t SpcEmu::mLoad(uint8_t const *in, long size) {
  assert(offsetof(header_t, unused2[46]) == HEADER_SIZE);
  m_fileData = in;
  m_fileSize = size;
  m_setChannelsNumber(SnesSpc::CHANNELS_NUM);
  if (m_isArchive)
    return 0;
  if (size < SnesSpc::SPC_MIN_FILE_SIZE)
    return gme_wrong_file_type;
  return check_spc_header(in);
}

// Emulation

void SpcEmu::mSetTempo(double t) { m_apu.set_tempo((int) (t * m_apu.TEMPO_UNIT)); }

blargg_err_t SpcEmu::mStartTrack(int track) {
  RETURN_ERR(MusicEmu::mStartTrack(track));
  m_resampler.clear();
  m_filter.Clear();
  RETURN_ERR(m_apu.load_spc(m_fileData, m_fileSize));
  m_filter.SetGain((int) (m_getGain() * SpcFilter::GAIN_UNIT));
  m_apu.clear_echo();
  track_info_t spc_info;
  RETURN_ERR(mGetTrackInfo(&spc_info, track));

  // Set a default track length, need a non-zero fadeout
  if (autoloadPlaybackLimit() && (spc_info.length > 0))
    setFade(spc_info.length, 50);
  return 0;
}

blargg_err_t SpcEmu::m_playAndFilter(long count, sample_t out[]) {
  RETURN_ERR(m_apu.play(count, out));
  m_filter.Run(out, count);
  return 0;
}

blargg_err_t SpcEmu::m_skip(long count) {
  if (getSampleRate() != NATIVE_SAMPLE_RATE) {
    count = long(count * m_resampler.getRatio()) & ~1;
    count -= m_resampler.skipInput(count);
  }

  // TODO: shouldn't skip be adjusted for the 64 samples read afterwards?

  if (count > 0) {
    RETURN_ERR(m_apu.skip(count));
    m_filter.Clear();
  }

  // eliminate pop due to resampler
  const int resampler_latency = 64;
  sample_t buf[resampler_latency];
  return mPlay(resampler_latency, buf);
}

blargg_err_t SpcEmu::mPlay(long count, sample_t *out) {
  if (getSampleRate() == NATIVE_SAMPLE_RATE)
    return m_playAndFilter(count, out);

  long remain = count;
  while (remain > 0) {
    remain -= m_resampler.read(&out[count - remain], remain);
    if (remain > 0) {
      long n = m_resampler.getMaxWrite();
      RETURN_ERR(m_playAndFilter(n, m_resampler.buffer()));
      m_resampler.write(n);
    }
  }
  check(remain == 0);
  return 0;
}

blargg_err_t RsnEmu::loadArchive(const char *path) {
#ifdef RARDLL
  struct RAROpenArchiveData data = {.ArcName = (char *) path,
                                    .OpenMode = RAR_OM_LIST,
                                    .OpenResult = 0,
                                    .CmtBuf = 0,
                                    .CmtBufSize = 0,
                                    .CmtSize = 0,
                                    .CmtState = 0};

  // get the file count and unpacked size
  long pos = 0;
  int count = 0;
  HANDLE PASCAL rar = RAROpenArchive(&data);
  struct RARHeaderData head;
  for (; RARReadHeader(rar, &head) == ERAR_SUCCESS; count++) {
    RARProcessFile(rar, RAR_SKIP, 0, 0);
    pos += head.UnpSize;
  }
  m_rsn.resize(pos);
  m_spc.resize(count);
  RARCloseArchive(rar);

  // copy the stream and index the tracks
  uint8_t *bp = &m_rsn[0];
  data.OpenMode = RAR_OM_EXTRACT;
  rar = RAROpenArchive(&data);
  RARSetCallback(rar, call_rsn, (intptr_t) &bp);
  for (count = 0, pos = 0; RARReadHeader(rar, &head) == ERAR_SUCCESS;) {
    RARProcessFile(rar, RAR_TEST, 0, 0);
    if (!check_spc_header(bp - head.UnpSize))
      m_spc[count++] = &m_rsn[pos];
    pos += head.UnpSize;
  }
  m_spc[count] = &m_rsn[pos];
  m_setTrackNum(count);
  RARCloseArchive(rar);

  return 0;
#else
  (void) path;
  return gme_wrong_file_type;
#endif
}

blargg_err_t RsnEmu::mStartTrack(int track) {
  if (static_cast<size_t>(track) >= m_spc.size())
    return "Invalid track requested";
  m_fileData = m_spc[track];
  m_fileSize = m_spc[track + 1] - m_spc[track];
  return SpcEmu::mStartTrack(track);
}

RsnEmu::~RsnEmu() {}

}  // namespace snes
}  // namespace emu
}  // namespace gme

static gme_type_t_ const gme_spc_type_ = {
    "Super Nintendo", 1, &gme::emu::snes::SpcEmu::createSpcEmu, &gme::emu::snes::SpcFile::createSpcFile, "SPC", 0};
extern gme_type_t const gme_spc_type = &gme_spc_type_;

static gme_type_t_ const gme_rsn_type_ = {
    "Super Nintendo", 0, &gme::emu::snes::RsnEmu::createRsnEmu, &gme::emu::snes::RsnFile::createRsnFile, "RSN", 0};
extern gme_type_t const gme_rsn_type = &gme_rsn_type_;
