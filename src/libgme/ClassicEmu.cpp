// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "ClassicEmu.h"
#include "MultiBuffer.h"
#include <cstring>

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

ClassicEmu::ClassicEmu() {
  this->mBuf = 0;
  this->mStereoBuf = 0;
  this->mChannelTypes = 0;

  // avoid inconsistency in our duplicated constants
  assert((int) WAVE_TYPE == (int) MultiBuffer::WAVE_TYPE);
  assert((int) NOISE_TYPE == (int) MultiBuffer::NOISE_TYPE);
  assert((int) MIXED_TYPE == (int) MultiBuffer::MIXED_TYPE);
}

ClassicEmu::~ClassicEmu() { delete this->mStereoBuf; }

void ClassicEmu::mSetEqualizer(equalizer_t const &eq) {
  MusicEmu::mSetEqualizer(eq);
  mUpdateEq(eq.treble);
  if (this->mBuf)
    this->mBuf->setBassFreq((int) GetEqualizer().bass);
}

blargg_err_t ClassicEmu::mSetSampleRate(long rate) {
  if (this->mBuf == nullptr) {
    if (this->mStereoBuf == nullptr)
      CHECK_ALLOC(this->mStereoBuf = BLARGG_NEW StereoBuffer);
    this->mBuf = this->mStereoBuf;
  }
  return this->mBuf->SetSampleRate(rate, 1000 / 20);
}

blargg_err_t ClassicEmu::SetMultiChannel(bool enable) {
  RETURN_ERR(MusicEmu::mSetMultiChannel(enable));
  return 0;
}

void ClassicEmu::mMuteChannel(int mask) {
  MusicEmu::mMuteChannel(mask);
  for (int i = 0; i < this->GetChannelsNum(); i++, mask >>= 1) {
    if (mask & 1) {
      this->mSetChannel(i, nullptr, nullptr, nullptr);
    } else {
      auto &ch = this->mBuf->getChannelBuffers(i, this->mGetChannelType(i));
      assert((ch.center && ch.left && ch.right) || (!ch.center && !ch.left && !ch.right));  // all or nothing
      this->mSetChannel(i, ch.center, ch.left, ch.right);
    }
  }
}

void ClassicEmu::mChangeClockRate(long rate) {
  this->mClockRate = rate;
  this->mBuf->setClockRate(rate);
}

blargg_err_t ClassicEmu::mSetupBuffer(long rate) {
  this->mChangeClockRate(rate);
  RETURN_ERR(this->mBuf->setChannelCount(GetChannelsNum()));
  SetEqualizer(GetEqualizer());
  this->mBufChangedNum = this->mBuf->getChangedChannelsNumber();
  return 0;
}

blargg_err_t ClassicEmu::mStartTrack(int track) {
  RETURN_ERR(MusicEmu::mStartTrack(track));
  this->mBuf->clear();
  return 0;
}

blargg_err_t ClassicEmu::mPlay(long count, sample_t *out) {
  long remain = count;
  while (remain) {
    remain -= this->mBuf->readSamples(&out[count - remain], remain);
    if (remain) {
      if (this->mBufChangedNum != this->mBuf->getChangedChannelsNumber()) {
        this->mBufChangedNum = this->mBuf->getChangedChannelsNumber();
        this->mRemuteChannels();
      }
      int msec = this->mBuf->getLength();
      blip_time_t clocks_emulated = (blargg_long) msec * this->mClockRate / 1000;
      RETURN_ERR(mRunClocks(clocks_emulated, msec));
      assert(clocks_emulated);
      this->mBuf->EndFrame(clocks_emulated);
    }
  }
  return 0;
}

// RomData

blargg_err_t RomDataImpl::m_loadRomData(DataReader &src, int header_size, void *header_out, int fill, long pad_size) {
  long file_offset = pad_size - header_size;

  this->m_romAddr = 0;
  this->m_mask = 0;
  this->m_size = 0;
  this->rom.clear();

  this->m_fileSize = src.remain();
  if (this->m_fileSize <= header_size)  // <= because there must be data after header
    return gme_wrong_file_type;
  blargg_err_t err = this->rom.resize(file_offset + this->m_fileSize + pad_size);
  if (!err)
    err = src.read(this->rom.begin() + file_offset, this->m_fileSize);
  if (err) {
    this->rom.clear();
    return err;
  }

  this->m_fileSize -= header_size;
  memcpy(header_out, &this->rom[file_offset], header_size);

  memset(this->rom.begin(), fill, pad_size);
  memset(this->rom.end() - pad_size, fill, pad_size);

  return 0;
}

void RomDataImpl::m_setAddr(long addr, int unit) {
  this->m_romAddr = addr - unit - PAD_EXTRA;

  long rounded = (addr + this->m_fileSize + unit - 1) / unit * unit;
  if (rounded <= 0) {
    rounded = 0;
  } else {
    int shift = 0;
    unsigned long max_addr = (unsigned long) (rounded - 1);
    while (max_addr >> shift)
      shift++;
    this->m_mask = (1L << shift) - 1;
  }

  if (addr < 0)
    addr = 0;
  this->m_size = rounded;
  if (this->rom.resize(rounded - this->m_romAddr + PAD_EXTRA)) {
  }  // OK if shrink fails

  if (0) {
    debug_printf("addr: %X\n", addr);
    debug_printf("file_size: %d\n", this->m_fileSize);
    debug_printf("rounded: %d\n", rounded);
    debug_printf("mask: $%X\n", this->m_mask);
  }
}
