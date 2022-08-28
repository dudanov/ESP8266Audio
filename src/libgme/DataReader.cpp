// File_Extractor 0.4.0. http://www.slack.net/~ant/

#include "DataReader.h"

#include "blargg_endian.h"
#include <algorithm>
#include <assert.h>
#include <stdio.h>
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

#ifdef HAVE_ZLIB_H
#include <errno.h>
#include <stdlib.h>
#include <zlib.h>
static const unsigned char gz_magic[2] = {0x1f, 0x8b}; /* gzip magic header */
#endif                                                 /* HAVE_ZLIB_H */

const char DataReader::eof_error[] = "Unexpected end of file";

#define RETURN_VALIDITY_CHECK(cond) \
  do { \
    if (unlikely(!(cond))) \
      return "Corrupt file"; \
  } while (0)

blargg_err_t DataReader::read(void *p, long s) {
  RETURN_VALIDITY_CHECK(s > 0);

  long result = read_avail(p, s);
  if (result != s) {
    if (result >= 0 && result < s)
      return eof_error;

    return "Read error";
  }

  return 0;
}

blargg_err_t DataReader::skip(long count) {
  RETURN_VALIDITY_CHECK(count >= 0);

  char buf[512];
  while (count) {
    long n = sizeof buf;
    if (n > count)
      n = count;
    count -= n;
    RETURN_ERR(read(buf, n));
  }
  return 0;
}

long FileReader::remain() const { return size() - tell(); }

blargg_err_t FileReader::skip(long n) {
  RETURN_VALIDITY_CHECK(n >= 0);

  if (!n)
    return 0;
  return seek(tell() + n);
}

// SubsetReader

SubsetReader::SubsetReader(DataReader *dr, long size) {
  in = dr;
  remain_ = dr->remain();
  if (remain_ > size)
    remain_ = std::max(0l, size);
}

long SubsetReader::remain() const { return remain_; }

long SubsetReader::read_avail(void *p, long s) {
  s = std::max(0l, s);
  if (s > remain_)
    s = remain_;
  remain_ -= s;
  return in->read_avail(p, s);
}

// RemainingReader

RemainingReader::RemainingReader(void const *h, long size, DataReader *r) {
  header = (char const *) h;
  header_end = header + std::max(0l, size);
  in = r;
}

long RemainingReader::remain() const { return header_end - header + in->remain(); }

long RemainingReader::read_first(void *out, long count) {
  count = std::max(0l, count);
  long first = header_end - header;
  if (first) {
    if (first > count || first < 0)
      first = count;
    void const *old = header;
    header += first;
    memcpy(out, old, (size_t) first);
  }
  return first;
}

long RemainingReader::read_avail(void *out, long count) {
  count = std::max(0l, count);
  long first = read_first(out, count);
  long second = std::max(0l, count - first);
  if (second) {
    second = in->read_avail((char *) out + first, second);
    if (second <= 0)
      return second;
  }
  return first + second;
}

blargg_err_t RemainingReader::read(void *out, long count) {
  count = std::max(0l, count);
  long first = read_first(out, count);
  long second = std::max(0l, count - first);
  if (!second)
    return 0;
  return in->read((char *) out + first, second);
}

// MemFileReader

MemFileReader::MemFileReader(const void *p, long s) : m_begin((const char *) p), m_size(std::max(0l, s)), m_pos(0l) {
#ifdef HAVE_ZLIB_H
  if (!m_begin)
    return;

  if (gz_decompress()) {
    debug_printf("Loaded compressed data\n");
    m_ownedPtr = true;
  }
#endif /* HAVE_ZLIB_H */
}

#ifdef HAVE_ZLIB_H
MemFileReader::~MemFileReader() {
  if (m_ownedPtr)
    free(const_cast<char *>(m_begin));  // see gz_compress for the malloc
}
#endif

long MemFileReader::size() const { return m_size; }

long MemFileReader::read_avail(void *p, long s) {
  long r = remain();
  if (s > r || s < 0)
    s = r;
  memcpy(p, m_begin + m_pos, static_cast<size_t>(s));
  m_pos += s;
  return s;
}

long MemFileReader::tell() const { return m_pos; }

blargg_err_t MemFileReader::seek(long n) {
  RETURN_VALIDITY_CHECK(n >= 0);
  if (n > m_size)
    return eof_error;
  m_pos = n;
  return 0;
}

#ifdef HAVE_ZLIB_H

bool MemFileReader::gz_decompress() {
  if (m_size >= 2 && memcmp(m_begin, gz_magic, 2) != 0) {
    /* Don't try to decompress non-GZ files, just assign input pointer */
    return false;
  }

  using vec_size = size_t;
  const vec_size full_length = static_cast<vec_size>(m_size);
  const vec_size half_length = static_cast<vec_size>(m_size / 2);

  // We use malloc/friends here so we can realloc to grow buffer if needed
  char *raw_data = reinterpret_cast<char *>(malloc(full_length));
  size_t raw_data_size = full_length;
  if (!raw_data)
    return false;

  z_stream strm;
  strm.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(m_begin));
  strm.avail_in = static_cast<uInt>(m_size);
  strm.total_out = 0;
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;

  bool done = false;

  // Adding 16 sets bit 4, which enables zlib to auto-detect the
  // header.
  if (inflateInit2(&strm, (16 + MAX_WBITS)) != Z_OK) {
    free(raw_data);
    return false;
  }

  while (!done) {
    /* If our output buffer is too small */
    if (strm.total_out >= raw_data_size) {
      raw_data_size += half_length;
      raw_data = reinterpret_cast<char *>(realloc(raw_data, raw_data_size));
      if (!raw_data) {
        return false;
      }
    }

    strm.next_out = reinterpret_cast<Bytef *>(raw_data + strm.total_out);
    strm.avail_out = static_cast<uInt>(static_cast<uLong>(raw_data_size) - strm.total_out);

    /* Inflate another chunk. */
    int err = inflate(&strm, Z_SYNC_FLUSH);
    if (err == Z_STREAM_END)
      done = true;
    else if (err != Z_OK)
      break;
  }

  if (inflateEnd(&strm) != Z_OK) {
    free(raw_data);
    return false;
  }

  m_begin = raw_data;
  m_size = static_cast<long>(strm.total_out);

  return true;
}

#endif /* HAVE_ZLIB_H */

// CallbackReader

CallbackReader::CallbackReader(callback_t c, long size, void *d) : callback(c), data(d) { remain_ = std::max(0l, size); }

long CallbackReader::remain() const { return remain_; }

long CallbackReader::read_avail(void *out, long count) {
  if (count > remain_)
    count = remain_;
  if (count < 0 || CallbackReader::read(out, count))
    count = -1;
  return count;
}

blargg_err_t CallbackReader::read(void *out, long count) {
  RETURN_VALIDITY_CHECK(count >= 0);
  if (count > remain_)
    return eof_error;
  return callback(data, out, (int) count);
}
