// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "MusicEmu.h"
#include "MultiBuffer.h"
#include <algorithm>
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

int const SILENCE_MAX = 6;  // seconds
int const SILENCE_THRESHOLD = 0x10;
long const FADE_BLOCK_SIZE = 512;
int const FADE_SHIFT = 8;  // fade ends with gain at 1.0 / (1 << FADE_SHIFT)

MusicEmu::equalizer_t const MusicEmu::tv_eq = MusicEmu::MakeEqualizer(-8.0, 180);

void MusicEmu::mClearTrackVars() {
  mCurrentTrack = -1;
  mOutTime = 0;
  mEmuTime = 0;
  mEmuTrackEnded = true;
  mIsTrackEnded = true;
  mFadeStart = INT_MAX / 2 + 1;
  mFadeStep = 1;
  mSilenceTime = 0;
  mSilenceCount = 0;
  mBufRemain = 0;
  this->warning();  // clear warning
}

void MusicEmu::mUnload() {
  mChannelsNum = 0;
  mClearTrackVars();
  GmeFile::mUnload();
}

MusicEmu::MusicEmu() {
  // mEffectsBuffer = 0;
  mIsMultiChannel = false;
  mSampleRate = 0;
  mMuteMask = 0;
  mTempo = 1.0;
  mGain = 1.0;

  // defaults
  mMaxInitSilence = 2;
  mSilenceLookahead = 3;
  mIgnoreSilence = false;
  mEqualizer.treble = -1.0;
  mEqualizer.bass = 60;

  mEmuAutoloadPlaybackLimit = true;

  static const char *const CHANNELS_NAMES[] = {"Channel 1", "Channel 2", "Channel 3", "Channel 4",
                                               "Channel 5", "Channel 6", "Channel 7", "Channel 8"};
  mSetChannelsNames(CHANNELS_NAMES);
  MusicEmu::mUnload();  // non-virtual
}

MusicEmu::~MusicEmu() {}  // delete mEffectsBuffer; }

blargg_err_t MusicEmu::SetSampleRate(long rate) {
  require(!this->GetSampleRate());  // sample rate can't be changed once set
  RETURN_ERR(mSetSampleRate(rate));
  // RETURN_ERR(mSamplesBuffer.resize(BUF_SIZE));
  mSampleRate = rate;
  return 0;
}

void MusicEmu::mPreLoad() {
  require(this->GetSampleRate());  // SetSampleRate() must be called before loading a file
  GmeFile::mPreLoad();
}

void MusicEmu::SetEqualizer(equalizer_t const &eq) {
  mEqualizer = eq;
  mSetEqualizer(eq);
}

blargg_err_t MusicEmu::SetMultiChannel(bool) {
  // by default not supported, derived may override this
  return "unsupported for this emulator type";
}

blargg_err_t MusicEmu::mSetMultiChannel(bool isEnabled) {
  // multi channel support must be set at the very beginning
  require(!this->GetSampleRate());
  mIsMultiChannel = isEnabled;
  return 0;
}

void MusicEmu::MuteChannel(int idx, bool mute) {
  require((unsigned) idx < (unsigned) GetChannelsNum());
  int bit = 1 << idx;
  int mask = mMuteMask | bit;
  if (!mute)
    mask ^= bit;
  this->MuteChannels(mask);
}

void MusicEmu::MuteChannels(int mask) {
  require(this->GetSampleRate());  // sample rate must be set first
  mMuteMask = mask;
  mMuteChannel(mask);
}

void MusicEmu::SetTempo(double t) {
  require(this->GetSampleRate());  // sample rate must be set first
  const double min = 0.02;
  const double max = 4.00;
  if (t < min)
    t = min;
  if (t > max)
    t = max;
  mTempo = t;
  mSetTempo(t);
}

void MusicEmu::mPostLoad() {
  this->SetTempo(mTempo);
  mRemuteChannels();
}

blargg_err_t MusicEmu::StartTrack(int track) {
  mClearTrackVars();

  int remapped = track;
  RETURN_ERR(remapTrack(&remapped));
  mCurrentTrack = track;
  RETURN_ERR(mStartTrack(remapped));

  mEmuTrackEnded = false;
  mIsTrackEnded = false;

  if (!mIgnoreSilence) {
    // play until non-silence or end of track
    for (long end = mMaxInitSilence * mGetOutputChannels() * this->GetSampleRate(); mEmuTime < end;) {
      mFillBuf();
      if (mBufRemain | (int) mEmuTrackEnded)
        break;
    }

    mEmuTime = mBufRemain;
    mOutTime = 0;
    mSilenceTime = 0;
    mSilenceCount = 0;
  }
  return this->IsTrackEnded() ? this->warning() : nullptr;
}

void MusicEmu::mEndTrackIfError(blargg_err_t err) {
  if (err != nullptr) {
    mEmuTrackEnded = true;
    m_setWarning(err);
  }
}

// Tell/Seek

uint32_t MusicEmu::mMsToSamples(blargg_long ms) const {
  return ms * this->GetSampleRate() / 1000 * mGetOutputChannels();
}

long MusicEmu::TellSamples() const { return mOutTime; }

long MusicEmu::TellMs() const {
  blargg_long rate = this->GetSampleRate() * mGetOutputChannels();
  blargg_long sec = mOutTime / rate;
  return sec * 1000 + (mOutTime - sec * rate) * 1000 / rate;
}

blargg_err_t MusicEmu::SeekSamples(long time) {
  if (time < mOutTime)
    RETURN_ERR(StartTrack(mCurrentTrack));
  return SkipSamples(time - mOutTime);
}

blargg_err_t MusicEmu::SkipSamples(long count) {
  require(this->GetCurrentTrack() >= 0);  // start_track() must have been called already
  mOutTime += count;
  // remove from silence and buf first
  {
    long n = std::min(count, mSilenceCount);
    mSilenceCount -= n;
    count -= n;

    n = std::min(count, mBufRemain);
    mBufRemain -= n;
    count -= n;
  }

  if (count && !mEmuTrackEnded) {
    mEmuTime += count;
    mEndTrackIfError(mSkipSamples(count));
  }

  if (!(mSilenceCount | mBufRemain))  // caught up to emulator, so update track ended
    mIsTrackEnded |= mEmuTrackEnded;

  return 0;
}

blargg_err_t MusicEmu::mSkipSamples(long count) {
  // for long skip, mute sound
  static const long THRESHOLD = 30000;
  if (count > THRESHOLD) {
    int saved_mute = mMuteMask;
    mMuteChannel(~0);
    while (count > THRESHOLD / 2 && !mEmuTrackEnded) {
      RETURN_ERR(mPlay(mSamplesBuffer.size(), mSamplesBuffer.data()));
      count -= mSamplesBuffer.size();
    }
    mMuteChannel(saved_mute);
  }

  while (count && !mEmuTrackEnded) {
    long n = std::min<long>(mSamplesBuffer.size(), count);
    count -= n;
    RETURN_ERR(mPlay(n, mSamplesBuffer.data()));
  }
  return nullptr;
}

// Fading

void MusicEmu::SetFadeMs(long startMs, long lengthMs) {
  mFadeStep = this->GetSampleRate() * lengthMs / (FADE_BLOCK_SIZE * FADE_SHIFT * 1000 / mGetOutputChannels());
  mFadeStart = mMsToSamples(startMs);
}

// unit / pow( 2.0, (double) x / step )
static int int_log(blargg_long x, int step, int unit) {
  int shift = x / step;
  int fraction = (x - shift * step) * unit / step;
  return ((unit - fraction) + (fraction >> 1)) >> shift;
}

void MusicEmu::mHandleFade(long out_count, sample_t *out) {
  for (int i = 0; i < out_count; i += FADE_BLOCK_SIZE) {
    static const int SHIFT = 14;
    int const UNIT = 1 << SHIFT;
    int gain = int_log((mOutTime + i - mFadeStart) / FADE_BLOCK_SIZE, mFadeStep, UNIT);
    if (gain < (UNIT >> FADE_SHIFT))
      mIsTrackEnded = mEmuTrackEnded = true;

    sample_t *io = &out[i];
    for (int count = std::min(FADE_BLOCK_SIZE, out_count - i); count; --count, ++io)
      *io = sample_t((*io * gain) >> SHIFT);
  }
}

// Silence detection

void MusicEmu::mEmuPlay(long count, sample_t *out) {
  check(mCurrentTrack >= 0);
  mEmuTime += count;
  if (mCurrentTrack >= 0 && !mEmuTrackEnded)
    mEndTrackIfError(mPlay(count, out));
  else
    memset(out, 0, count * sizeof(*out));
}

// number of consecutive silent samples at end
static long sCountSilence(MusicEmu::sample_t *begin, long size) {
  MusicEmu::sample_t first = *begin;
  *begin = SILENCE_THRESHOLD;  // sentinel
  MusicEmu::sample_t *p = begin + size;
  while ((unsigned) (*--p + SILENCE_THRESHOLD / 2) <= (unsigned) SILENCE_THRESHOLD) {
  }
  *begin = first;
  return size - (p - begin);
}

// fill internal buffer and check it for silence
void MusicEmu::mFillBuf() {
  assert(!mBufRemain);
  if (!mEmuTrackEnded) {
    mEmuPlay(mSamplesBuffer.size(), mSamplesBuffer.begin());
    long silence = sCountSilence(mSamplesBuffer.begin(), mSamplesBuffer.size());
    if (silence < mSamplesBuffer.size()) {
      mSilenceTime = mEmuTime - silence;
      mBufRemain = mSamplesBuffer.size();
      return;
    }
  }
  mSilenceCount += mSamplesBuffer.size();
}

blargg_err_t MusicEmu::Play(long out_count, sample_t *out) {
  if (mIsTrackEnded) {
    memset(out, 0, out_count * sizeof(*out));
  } else {
    require(this->GetCurrentTrack() >= 0);
    require(out_count % mGetOutputChannels() == 0);
    assert(mEmuTime >= mOutTime);

    // prints nifty graph of how far ahead we are when searching for silence
    // debug_printf( "%*s \n", int ((emu_time - out_time) * 7 /
    // sample_rate()), "*" );

    long pos = 0;
    if (mSilenceCount) {
      // during a run of silence, run emulator at >=2x speed so it gets ahead
      long ahead_time = mSilenceLookahead * (mOutTime + out_count - mSilenceTime) + mSilenceTime;
      while (mEmuTime < ahead_time && !(mBufRemain | mEmuTrackEnded))
        mFillBuf();

      // fill with silence
      pos = std::min(mSilenceCount, out_count);
      memset(out, 0, pos * sizeof(*out));
      mSilenceCount -= pos;

      if (mEmuTime - mSilenceTime > SILENCE_MAX * mGetOutputChannels() * this->GetSampleRate()) {
        mIsTrackEnded = mEmuTrackEnded = true;
        mSilenceCount = 0;
        mBufRemain = 0;
      }
    }

    if (mBufRemain) {
      // empty silence buf
      long n = std::min(mBufRemain, out_count - pos);
      memcpy(&out[pos], mSamplesBuffer.begin() + (mSamplesBuffer.size() - mBufRemain), n * sizeof(*out));
      mBufRemain -= n;
      pos += n;
    }

    // generate remaining samples normally
    long remain = out_count - pos;
    if (remain) {
      mEmuPlay(remain, out + pos);
      mIsTrackEnded |= mEmuTrackEnded;

      if (!mIgnoreSilence || mOutTime > mFadeStart) {
        // check end for a new run of silence
        long silence = sCountSilence(out + pos, remain);
        if (silence < remain)
          mSilenceTime = mEmuTime - silence;

        if (mEmuTime - mSilenceTime >= mSamplesBuffer.size())
          mFillBuf();  // cause silence detection on next Play()
      }
    }

    if (mFadeStart >= 0 && mOutTime > mFadeStart)
      mHandleFade(out_count, out);
  }
  mOutTime += out_count;
  return 0;
}

// GmeInfo

blargg_err_t GmeInfo::mStartTrack(int) { return "Use full emulator for playback"; }
blargg_err_t GmeInfo::mPlay(long, sample_t *) { return "Use full emulator for playback"; }
