/*
  AudioGeneratorGme
  Audio output generator that plays various retro game music using GME

  Copyright (C) 2022  Sergey V. DUDANOV <sergey.dudanov@gmail.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <array>
#include "AudioGenerator.h"
#include "libgme/gme.h"
#include "libgme/DataReader.h"

class AudioGeneratorGme : public AudioGenerator {
public:
  AudioGeneratorGme() : mPos(this->mBuf.size()) {}
  ~AudioGeneratorGme() {}
  bool begin(AudioFileSource *source, AudioOutput *output) override;
  bool isRunning() override { return this->running; }
  bool loop() override;
  bool stop() override;
  bool PlayTrack(int num);

protected:
  // AudioFileSource to GME DataReader adapter
  class AudioSourceReader : public DataReader {
  public:
    void set_source(AudioFileSource *src) { this->mSource = src; }
    long read_avail(void *dst, long size) override;
    blargg_err_t read(void *dst, long size) override;
    long remain() const override;
    blargg_err_t skip(long count) override;

  private:
    AudioFileSource *mSource;
  } mReader;
  gme_type_t mType{nullptr};
  MusicEmu *mEmu{nullptr};
  std::array<int16_t, 1024> mBuf;
  unsigned mPos;
  bool mLoad(int sample_rate);
  void mCbInfo(const char *name, const char *value);
  void mCbInfo(const char *name, long value);
  void mCbTrackInfo();
};
