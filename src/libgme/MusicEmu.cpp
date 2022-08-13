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
  this->mCurrentTrack = -1;
  this->mOutTime = 0;
  this->mEmuTime = 0;
  this->mEmuTrackEnded = true;
  this->mIsTrackEnded = true;
  this->mFadeStart = INT_MAX / 2 + 1;
  this->mFadeStep = 1;
  this->mSilenceTime = 0;
  this->mSilenceCount = 0;
  this->mBufRemain = 0;
  this->warning();  // clear warning
}

void MusicEmu::mUnload() {
  this->mChannelsNum = 0;
  this->mClearTrackVars();
  GmeFile::mUnload();
}

MusicEmu::MusicEmu() {
  this->mEffectsBuffer = 0;
  this->mIsMultiChannel = false;
  this->mSampleRate = 0;
  this->mMuteMask = 0;
  this->mTempo = 1.0;
  this->mGain = 1.0;

  // defaults
  this->mMaxInitSilence = 2;
  this->mSilenceLookahead = 3;
  this->mIgnoreSilence = false;
  this->mEqualizer.treble = -1.0;
  this->mEqualizer.bass = 60;

  this->mEmuAutoloadPlaybackLimit = true;

  static const char *const CHANNELS_NAMES[] = {"Channel 1", "Channel 2", "Channel 3", "Channel 4",
                                               "Channel 5", "Channel 6", "Channel 7", "Channel 8"};
  this->mSetChannelsNames(CHANNELS_NAMES);
  MusicEmu::mUnload();  // non-virtual
}

MusicEmu::~MusicEmu() { delete mEffectsBuffer; }

blargg_err_t MusicEmu::SetSampleRate(long rate) {
  require(!this->GetSampleRate());  // sample rate can't be changed once set
  RETURN_ERR(this->mSetSampleRate(rate));
  RETURN_ERR(this->mSamplesBuffer.resize(BUF_SIZE));
  this->mSampleRate = rate;
  return 0;
}

void MusicEmu::mPreLoad() {
  require(this->GetSampleRate());  // SetSampleRate() must be called before loading a file
  GmeFile::mPreLoad();
}

void MusicEmu::SetEqualizer(equalizer_t const &eq) {
  this->mEqualizer = eq;
  this->mSetEqualizer(eq);
}

blargg_err_t MusicEmu::SetMultiChannel(bool) {
  // by default not supported, derived may override this
  return "unsupported for this emulator type";
}

blargg_err_t MusicEmu::mSetMultiChannel(bool isEnabled) {
  // multi channel support must be set at the very beginning
  require(!this->GetSampleRate());
  this->mIsMultiChannel = isEnabled;
  return 0;
}

void MusicEmu::MuteChannel(int idx, bool mute) {
  require((unsigned) idx < (unsigned) GetChannelsNum());
  int bit = 1 << idx;
  int mask = this->mMuteMask | bit;
  if (!mute)
    mask ^= bit;
  this->MuteChannels(mask);
}

void MusicEmu::MuteChannels(int mask) {
  require(this->GetSampleRate());  // sample rate must be set first
  this->mMuteMask = mask;
  this->mMuteChannel(mask);
}

void MusicEmu::SetTempo(double t) {
  require(this->GetSampleRate());  // sample rate must be set first
  const double min = 0.02;
  const double max = 4.00;
  if (t < min)
    t = min;
  if (t > max)
    t = max;
  this->mTempo = t;
  this->mSetTempo(t);
}

void MusicEmu::mPostLoad() {
  this->SetTempo(this->mTempo);
  this->mRemuteChannels();
}

blargg_err_t MusicEmu::StartTrack(int track) {
  this->mClearTrackVars();

  int remapped = track;
  RETURN_ERR(remapTrack(&remapped));
  this->mCurrentTrack = track;
  RETURN_ERR(this->mStartTrack(remapped));

  this->mEmuTrackEnded = false;
  this->mIsTrackEnded = false;

  if (!this->mIgnoreSilence) {
    // play until non-silence or end of track
    for (long end = this->mMaxInitSilence * mGetOutputChannels() * this->GetSampleRate(); this->mEmuTime < end;) {
      this->mFillBuf();
      if (this->mBufRemain | (int) this->mEmuTrackEnded)
        break;
    }

    this->mEmuTime = this->mBufRemain;
    this->mOutTime = 0;
    this->mSilenceTime = 0;
    this->mSilenceCount = 0;
  }
  return this->IsTrackEnded() ? this->warning() : nullptr;
}

void MusicEmu::mEndTrackIfError(blargg_err_t err) {
  if (err != nullptr) {
    this->mEmuTrackEnded = true;
    this->m_setWarning(err);
  }
}

// Tell/Seek

uint32_t MusicEmu::mMsToSamples(blargg_long ms) const {
  return ms * this->GetSampleRate() / 1000 * this->mGetOutputChannels();
}

long MusicEmu::TellSamples() const { return this->mOutTime; }

long MusicEmu::TellMs() const {
  blargg_long rate = this->GetSampleRate() * this->mGetOutputChannels();
  blargg_long sec = this->mOutTime / rate;
  return sec * 1000 + (this->mOutTime - sec * rate) * 1000 / rate;
}

blargg_err_t MusicEmu::SeekSamples(long time) {
  if (time < this->mOutTime)
    RETURN_ERR(StartTrack(this->mCurrentTrack));
  return SkipSamples(time - this->mOutTime);
}

blargg_err_t MusicEmu::SkipSamples(long count) {
  require(this->GetCurrentTrack() >= 0);  // start_track() must have been called already
  this->mOutTime += count;
  // remove from silence and buf first
  {
    long n = std::min(count, this->mSilenceCount);
    this->mSilenceCount -= n;
    count -= n;

    n = std::min(count, this->mBufRemain);
    this->mBufRemain -= n;
    count -= n;
  }

  if (count && !this->mEmuTrackEnded) {
    this->mEmuTime += count;
    this->mEndTrackIfError(this->mSkipSamples(count));
  }

  if (!(this->mSilenceCount | this->mBufRemain))  // caught up to emulator, so update track ended
    this->mIsTrackEnded |= this->mEmuTrackEnded;

  return 0;
}

blargg_err_t MusicEmu::mSkipSamples(long count) {
  // for long skip, mute sound
  static const long THRESHOLD = 30000;
  if (count > THRESHOLD) {
    int saved_mute = this->mMuteMask;
    this->mMuteChannel(~0);

    while (count > THRESHOLD / 2 && !this->mEmuTrackEnded) {
      RETURN_ERR(this->mPlay(BUF_SIZE, this->mSamplesBuffer.begin()));
      count -= BUF_SIZE;
    }

    this->mMuteChannel(saved_mute);
  }

  while (count && !this->mEmuTrackEnded) {
    long n = BUF_SIZE;
    if (n > count)
      n = count;
    count -= n;
    RETURN_ERR(this->mPlay(n, this->mSamplesBuffer.begin()));
  }
  return 0;
}

// Fading

void MusicEmu::SetFadeMs(long startMs, long lengthMs) {
  this->mFadeStep =
      this->GetSampleRate() * lengthMs / (FADE_BLOCK_SIZE * FADE_SHIFT * 1000 / this->mGetOutputChannels());
  this->mFadeStart = mMsToSamples(startMs);
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
    int gain = int_log((this->mOutTime + i - this->mFadeStart) / FADE_BLOCK_SIZE, this->mFadeStep, UNIT);
    if (gain < (UNIT >> FADE_SHIFT))
      this->mIsTrackEnded = this->mEmuTrackEnded = true;

    sample_t *io = &out[i];
    for (int count = std::min(FADE_BLOCK_SIZE, out_count - i); count; --count, ++io)
      *io = sample_t((*io * gain) >> SHIFT);
  }
}

// Silence detection

void MusicEmu::mEmuPlay(long count, sample_t *out) {
  check(this->mCurrentTrack >= 0);
  this->mEmuTime += count;
  if (this->mCurrentTrack >= 0 && !this->mEmuTrackEnded)
    this->mEndTrackIfError(this->mPlay(count, out));
  else
    memset(out, 0, count * sizeof(*out));
}

// number of consecutive silent samples at end
static long count_silence(MusicEmu::sample_t *begin, long size) {
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
  assert(!this->mBufRemain);
  if (!this->mEmuTrackEnded) {
    this->mEmuPlay(BUF_SIZE, this->mSamplesBuffer.begin());
    long silence = count_silence(this->mSamplesBuffer.begin(), BUF_SIZE);
    if (silence < BUF_SIZE) {
      this->mSilenceTime = this->mEmuTime - silence;
      this->mBufRemain = BUF_SIZE;
      return;
    }
  }
  this->mSilenceCount += BUF_SIZE;
}

blargg_err_t MusicEmu::Play(long out_count, sample_t *out) {
  if (this->mIsTrackEnded) {
    memset(out, 0, out_count * sizeof(*out));
  } else {
    require(this->GetCurrentTrack() >= 0);
    require(out_count % mGetOutputChannels() == 0);
    assert(this->mEmuTime >= this->mOutTime);

    // prints nifty graph of how far ahead we are when searching for silence
    // debug_printf( "%*s \n", int ((emu_time - out_time) * 7 /
    // sample_rate()), "*" );

    long pos = 0;
    if (this->mSilenceCount) {
      // during a run of silence, run emulator at >=2x speed so it gets ahead
      long ahead_time =
          this->mSilenceLookahead * (this->mOutTime + out_count - this->mSilenceTime) + this->mSilenceTime;
      while (this->mEmuTime < ahead_time && !(this->mBufRemain | this->mEmuTrackEnded))
        this->mFillBuf();

      // fill with silence
      pos = std::min(this->mSilenceCount, out_count);
      memset(out, 0, pos * sizeof(*out));
      this->mSilenceCount -= pos;

      if (this->mEmuTime - this->mSilenceTime > SILENCE_MAX * this->mGetOutputChannels() * this->GetSampleRate()) {
        this->mIsTrackEnded = this->mEmuTrackEnded = true;
        this->mSilenceCount = 0;
        this->mBufRemain = 0;
      }
    }

    if (this->mBufRemain) {
      // empty silence buf
      long n = std::min(this->mBufRemain, out_count - pos);
      memcpy(&out[pos], mSamplesBuffer.begin() + (BUF_SIZE - this->mBufRemain), n * sizeof(*out));
      this->mBufRemain -= n;
      pos += n;
    }

    // generate remaining samples normally
    long remain = out_count - pos;
    if (remain) {
      this->mEmuPlay(remain, out + pos);
      this->mIsTrackEnded |= this->mEmuTrackEnded;

      if (!this->mIgnoreSilence || this->mOutTime > this->mFadeStart) {
        // check end for a new run of silence
        long silence = count_silence(out + pos, remain);
        if (silence < remain)
          this->mSilenceTime = this->mEmuTime - silence;

        if (this->mEmuTime - this->mSilenceTime >= BUF_SIZE)
          this->mFillBuf();  // cause silence detection on next Play()
      }
    }

    if (this->mFadeStart >= 0 && this->mOutTime > this->mFadeStart)
      this->mHandleFade(out_count, out);
  }
  this->mOutTime += out_count;
  return 0;
}

// GmeInfo

blargg_err_t GmeInfo::mStartTrack(int) { return "Use full emulator for playback"; }
blargg_err_t GmeInfo::mPlay(long, sample_t *) { return "Use full emulator for playback"; }
