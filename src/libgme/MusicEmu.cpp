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

MusicEmu::equalizer_t const MusicEmu::tv_eq = MusicEmu::make_equalizer(-8.0, 180);

void MusicEmu::m_clearTrackVars() {
  this->m_currentTrack = -1;
  this->m_outTime = 0;
  this->m_emuTime = 0;
  this->m_emuTrackEnded = true;
  this->m_trackEnded = true;
  this->m_fadeStart = INT_MAX / 2 + 1;
  this->m_fadeStep = 1;
  this->m_silenceTime = 0;
  this->m_silenceCount = 0;
  this->m_bufRemain = 0;
  this->warning();  // clear warning
}

void MusicEmu::m_unload() {
  this->m_channelsNum = 0;
  this->m_clearTrackVars();
  GmeFile::m_unload();
}

MusicEmu::MusicEmu() {
  this->m_effectsBuffer = 0;
  this->m_multiChannel = false;
  this->m_sampleRate = 0;
  this->m_muteMask = 0;
  this->m_tempo = 1.0;
  this->m_gain = 1.0;

  // defaults
  this->m_maxInitSilence = 2;
  this->m_silenceLookahead = 3;
  this->m_ignoreSilence = false;
  this->m_equalizer.treble = -1.0;
  this->m_equalizer.bass = 60;

  this->m_emuAutoloadPlaybackLimit = true;

  static const char *const CHANNELS_NAMES[] = {"Channel 1", "Channel 2", "Channel 3", "Channel 4",
                                               "Channel 5", "Channel 6", "Channel 7", "Channel 8"};
  this->m_setChannelsNames(CHANNELS_NAMES);
  MusicEmu::m_unload();  // non-virtual
}

MusicEmu::~MusicEmu() { delete m_effectsBuffer; }

blargg_err_t MusicEmu::setSampleRate(long rate) {
  require(!this->getSampleRate());  // sample rate can't be changed once set
  RETURN_ERR(this->m_setSampleRate(rate));
  RETURN_ERR(this->m_samplesBuffer.resize(BUF_SIZE));
  this->m_sampleRate = rate;
  return 0;
}

void MusicEmu::m_preLoad() {
  require(this->getSampleRate());  // setSampleRate() must be called before loading a file
  GmeFile::m_preLoad();
}

void MusicEmu::set_equalizer(equalizer_t const &eq) {
  this->m_equalizer = eq;
  this->m_setEqualizer(eq);
}

blargg_err_t MusicEmu::setMultiChannel(bool) {
  // by default not supported, derived may override this
  return "unsupported for this emulator type";
}

blargg_err_t MusicEmu::m_setMultiChannel(bool isEnabled) {
  // multi channel support must be set at the very beginning
  require(!this->getSampleRate());
  this->m_multiChannel = isEnabled;
  return 0;
}

void MusicEmu::muteChannel(int idx, bool mute) {
  require((unsigned) idx < (unsigned) getChannelsNum());
  int bit = 1 << idx;
  int mask = this->m_muteMask | bit;
  if (!mute)
    mask ^= bit;
  this->muteChannels(mask);
}

void MusicEmu::muteChannels(int mask) {
  require(this->getSampleRate());  // sample rate must be set first
  this->m_muteMask = mask;
  this->m_muteChannels(mask);
}

void MusicEmu::setTempo(double t) {
  require(this->getSampleRate());  // sample rate must be set first
  const double min = 0.02;
  const double max = 4.00;
  if (t < min)
    t = min;
  if (t > max)
    t = max;
  this->m_tempo = t;
  this->m_setTempo(t);
}

void MusicEmu::m_postLoad() {
  this->setTempo(this->m_tempo);
  this->m_remuteChannels();
}

blargg_err_t MusicEmu::startTrack(int track) {
  this->m_clearTrackVars();

  int remapped = track;
  RETURN_ERR(remapTrack(&remapped));
  this->m_currentTrack = track;
  RETURN_ERR(this->m_startTrack(remapped));

  this->m_emuTrackEnded = false;
  this->m_trackEnded = false;

  if (!this->m_ignoreSilence) {
    // play until non-silence or end of track
    for (long end = this->m_maxInitSilence * m_getOutputChannels() * this->getSampleRate(); this->m_emuTime < end;) {
      this->m_fillBuf();
      if (this->m_bufRemain | (int) this->m_emuTrackEnded)
        break;
    }

    this->m_emuTime = this->m_bufRemain;
    this->m_outTime = 0;
    this->m_silenceTime = 0;
    this->m_silenceCount = 0;
  }
  return this->isTrackEnded() ? this->warning() : nullptr;
}

void MusicEmu::m_endTrackIfError(blargg_err_t err) {
  if (err != nullptr) {
    this->m_emuTrackEnded = true;
    this->m_setWarning(err);
  }
}

// Tell/Seek

uint32_t MusicEmu::m_msToSamples(blargg_long ms) const {
  return ms * this->getSampleRate() / 1000 * this->m_getOutputChannels();
}

long MusicEmu::tellSamples() const { return this->m_outTime; }

long MusicEmu::tell() const {
  blargg_long rate = this->getSampleRate() * this->m_getOutputChannels();
  blargg_long sec = this->m_outTime / rate;
  return sec * 1000 + (this->m_outTime - sec * rate) * 1000 / rate;
}

blargg_err_t MusicEmu::seekSamples(long time) {
  if (time < this->m_outTime)
    RETURN_ERR(startTrack(this->m_currentTrack));
  return skip(time - this->m_outTime);
}

blargg_err_t MusicEmu::skip(long count) {
  require(this->getCurrentTrack() >= 0);  // start_track() must have been called already
  this->m_outTime += count;
  // remove from silence and buf first
  {
    long n = std::min(count, this->m_silenceCount);
    this->m_silenceCount -= n;
    count -= n;

    n = std::min(count, this->m_bufRemain);
    this->m_bufRemain -= n;
    count -= n;
  }

  if (count && !this->m_emuTrackEnded) {
    this->m_emuTime += count;
    this->m_endTrackIfError(this->m_skip(count));
  }

  if (!(this->m_silenceCount | this->m_bufRemain))  // caught up to emulator, so update track ended
    this->m_trackEnded |= this->m_emuTrackEnded;

  return 0;
}

blargg_err_t MusicEmu::m_skip(long count) {
  // for long skip, mute sound
  static const long THRESHOLD = 30000;
  if (count > THRESHOLD) {
    int saved_mute = this->m_muteMask;
    this->m_muteChannels(~0);

    while (count > THRESHOLD / 2 && !this->m_emuTrackEnded) {
      RETURN_ERR(this->m_play(BUF_SIZE, this->m_samplesBuffer.begin()));
      count -= BUF_SIZE;
    }

    this->m_muteChannels(saved_mute);
  }

  while (count && !this->m_emuTrackEnded) {
    long n = BUF_SIZE;
    if (n > count)
      n = count;
    count -= n;
    RETURN_ERR(this->m_play(n, this->m_samplesBuffer.begin()));
  }
  return 0;
}

// Fading

void MusicEmu::setFade(long startMs, long lengthMs) {
  this->m_fadeStep =
      this->getSampleRate() * lengthMs / (FADE_BLOCK_SIZE * FADE_SHIFT * 1000 / this->m_getOutputChannels());
  this->m_fadeStart = m_msToSamples(startMs);
}

// unit / pow( 2.0, (double) x / step )
static int int_log(blargg_long x, int step, int unit) {
  int shift = x / step;
  int fraction = (x - shift * step) * unit / step;
  return ((unit - fraction) + (fraction >> 1)) >> shift;
}

void MusicEmu::m_handleFade(long out_count, sample_t *out) {
  for (int i = 0; i < out_count; i += FADE_BLOCK_SIZE) {
    static const int SHIFT = 14;
    int const UNIT = 1 << SHIFT;
    int gain = int_log((this->m_outTime + i - this->m_fadeStart) / FADE_BLOCK_SIZE, this->m_fadeStep, UNIT);
    if (gain < (UNIT >> FADE_SHIFT))
      this->m_trackEnded = this->m_emuTrackEnded = true;

    sample_t *io = &out[i];
    for (int count = std::min(FADE_BLOCK_SIZE, out_count - i); count; --count, ++io)
      *io = sample_t((*io * gain) >> SHIFT);
  }
}

// Silence detection

void MusicEmu::m_emuPlay(long count, sample_t *out) {
  check(this->m_currentTrack >= 0);
  this->m_emuTime += count;
  if (this->m_currentTrack >= 0 && !this->m_emuTrackEnded)
    this->m_endTrackIfError(this->m_play(count, out));
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
void MusicEmu::m_fillBuf() {
  assert(!this->m_bufRemain);
  if (!this->m_emuTrackEnded) {
    this->m_emuPlay(BUF_SIZE, this->m_samplesBuffer.begin());
    long silence = count_silence(this->m_samplesBuffer.begin(), BUF_SIZE);
    if (silence < BUF_SIZE) {
      this->m_silenceTime = this->m_emuTime - silence;
      this->m_bufRemain = BUF_SIZE;
      return;
    }
  }
  this->m_silenceCount += BUF_SIZE;
}

blargg_err_t MusicEmu::play(long out_count, sample_t *out) {
  if (this->m_trackEnded) {
    memset(out, 0, out_count * sizeof(*out));
  } else {
    require(this->getCurrentTrack() >= 0);
    require(out_count % m_getOutputChannels() == 0);
    assert(this->m_emuTime >= this->m_outTime);

    // prints nifty graph of how far ahead we are when searching for silence
    // debug_printf( "%*s \n", int ((emu_time - out_time) * 7 /
    // sample_rate()), "*" );

    long pos = 0;
    if (this->m_silenceCount) {
      // during a run of silence, run emulator at >=2x speed so it gets ahead
      long ahead_time =
          this->m_silenceLookahead * (this->m_outTime + out_count - this->m_silenceTime) + this->m_silenceTime;
      while (this->m_emuTime < ahead_time && !(this->m_bufRemain | this->m_emuTrackEnded))
        this->m_fillBuf();

      // fill with silence
      pos = std::min(this->m_silenceCount, out_count);
      memset(out, 0, pos * sizeof(*out));
      this->m_silenceCount -= pos;

      if (this->m_emuTime - this->m_silenceTime > SILENCE_MAX * this->m_getOutputChannels() * this->getSampleRate()) {
        this->m_trackEnded = this->m_emuTrackEnded = true;
        this->m_silenceCount = 0;
        this->m_bufRemain = 0;
      }
    }

    if (this->m_bufRemain) {
      // empty silence buf
      long n = std::min(this->m_bufRemain, out_count - pos);
      memcpy(&out[pos], m_samplesBuffer.begin() + (BUF_SIZE - this->m_bufRemain), n * sizeof(*out));
      this->m_bufRemain -= n;
      pos += n;
    }

    // generate remaining samples normally
    long remain = out_count - pos;
    if (remain) {
      this->m_emuPlay(remain, out + pos);
      this->m_trackEnded |= this->m_emuTrackEnded;

      if (!this->m_ignoreSilence || this->m_outTime > this->m_fadeStart) {
        // check end for a new run of silence
        long silence = count_silence(out + pos, remain);
        if (silence < remain)
          this->m_silenceTime = this->m_emuTime - silence;

        if (this->m_emuTime - this->m_silenceTime >= BUF_SIZE)
          this->m_fillBuf();  // cause silence detection on next play()
      }
    }

    if (this->m_fadeStart >= 0 && this->m_outTime > this->m_fadeStart)
      this->m_handleFade(out_count, out);
  }
  this->m_outTime += out_count;
  return 0;
}

// GmeInfo

blargg_err_t GmeInfo::m_startTrack(int) { return "Use full emulator for playback"; }
blargg_err_t GmeInfo::m_play(long, sample_t *) { return "Use full emulator for playback"; }
