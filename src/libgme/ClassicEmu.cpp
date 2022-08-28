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
  // avoid inconsistency in our duplicated constants
  assert((int) WAVE_TYPE == (int) MultiBuffer::WAVE_TYPE);
  assert((int) NOISE_TYPE == (int) MultiBuffer::NOISE_TYPE);
  assert((int) MIXED_TYPE == (int) MultiBuffer::MIXED_TYPE);
}

ClassicEmu::~ClassicEmu() { delete mStereoBuf; }

void ClassicEmu::mSetEqualizer(equalizer_t const &eq) {
  MusicEmu::mSetEqualizer(eq);
  mUpdateEq(eq.treble);
  if (mBuf)
    mBuf->SetBassFreq((int) GetEqualizer().bass);
}

blargg_err_t ClassicEmu::mSetSampleRate(long rate) {
  if (mBuf == nullptr) {
    if (mStereoBuf == nullptr)
      CHECK_ALLOC(mStereoBuf = BLARGG_NEW MonoBuffer);
    mBuf = mStereoBuf;
  }
  return mBuf->SetSampleRate(rate, 1000 / 20);
}

blargg_err_t ClassicEmu::SetMultiChannel(bool enable) {
  RETURN_ERR(MusicEmu::mSetMultiChannel(enable));
  return 0;
}

void ClassicEmu::mMuteChannel(int mask) {
  MusicEmu::mMuteChannel(mask);
  for (int i = 0; i < this->GetChannelsNum(); i++, mask >>= 1) {
    if (mask & 1) {
      mSetChannel(i, nullptr, nullptr, nullptr);
    } else {
      auto &ch = mBuf->GetChannelBuffers(i, mGetChannelType(i));
      assert((ch.center && ch.left && ch.right) || (!ch.center && !ch.left && !ch.right));  // all or nothing
      mSetChannel(i, ch.center, ch.left, ch.right);
    }
  }
}

void ClassicEmu::mChangeClockRate(long rate) {
  mClockRate = rate;
  mBuf->SetClockRate(rate);
}

blargg_err_t ClassicEmu::mSetupBuffer(long rate) {
  mChangeClockRate(rate);
  RETURN_ERR(mBuf->SetChannelCount(GetChannelsNum()));
  SetEqualizer(GetEqualizer());
  mBufChangedNum = mBuf->GetChangedChannelsNumber();
  return 0;
}

blargg_err_t ClassicEmu::mStartTrack(int track) {
  RETURN_ERR(MusicEmu::mStartTrack(track));
  mBuf->Clear();
  return 0;
}

blargg_err_t ClassicEmu::mPlay(const long count, sample_t *out) {
  long remain = count;
  while (remain) {
    remain -= mBuf->ReadSamples(&out[count - remain], remain);
    if (remain == 0)
      return nullptr;
    if (mBufChangedNum != mBuf->GetChangedChannelsNumber()) {
      mBufChangedNum = mBuf->GetChangedChannelsNumber();
      mRemuteChannels();
    }
    blip_clk_time_t emu_clks = mBuf->GetLength() * mClockRate / 1000;
    RETURN_ERR(mRunClocks(emu_clks));
    assert(emu_clks);
    mBuf->EndFrame(emu_clks);
  }
  return nullptr;
}

// RomData

blargg_err_t RomDataImpl::mLoadRomData(DataReader &src, int header_size, void *header_out, int fill, long pad_size) {
  long file_offset = pad_size - header_size;

  mRomAddr = 0;
  mMask = 0;
  mSize = 0;
  mRom.clear();

  mFileSize = src.remain();

  if (mFileSize <= header_size)  // <= because there must be data after header
    return gme_wrong_file_type;

  blargg_err_t err = mRom.resize(file_offset + mFileSize + pad_size);

  if (err == nullptr)
    err = src.read(mRom.begin() + file_offset, mFileSize);

  if (err != nullptr) {
    mRom.clear();
    return err;
  }

  mFileSize -= header_size;

  memcpy(header_out, &mRom[file_offset], header_size);
  memset(mRom.begin(), fill, pad_size);
  memset(mRom.end() - pad_size, fill, pad_size);

  return nullptr;
}

void RomDataImpl::mSetAddr(long addr, int unit) {
  mRomAddr = addr - unit - PAD_EXTRA;

  long rounded = (addr + mFileSize + unit - 1) / unit * unit;
  if (rounded <= 0) {
    rounded = 0;
  } else {
    int shift = 0;
    unsigned long max_addr = (unsigned long) (rounded - 1);
    while (max_addr >> shift)
      shift++;
    mMask = (1L << shift) - 1;
  }

  if (addr < 0)
    addr = 0;
  mSize = rounded;
  if (mRom.resize(rounded - mRomAddr + PAD_EXTRA)) {
  }  // OK if shrink fails

  if (0) {
    debug_printf("addr: %X\n", addr);
    debug_printf("file_size: %d\n", mFileSize);
    debug_printf("rounded: %d\n", rounded);
    debug_printf("mask: $%X\n", mMask);
  }
}
