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

#include "AudioGenerator.h"
#include "libgme/DataReader.h"
#include "libgme/gme.h"
#include <array>

class AudioGeneratorGme : public AudioGenerator {
public:
  AudioGeneratorGme() : m_pos(this->m_buffer.size()) {}
  ~AudioGeneratorGme() {}
  bool begin(AudioFileSource *source, AudioOutput *output) override;
  bool isRunning() override { return this->m_isPlaying; }
  bool loop() override;
  bool stop() override;
  bool startTrack(int num);

protected:
  // AudioFileSource to GME DataReader adapter
  class AudioOutputReader : public DataReader {
  public:
    void set_source(AudioFileSource *src) { this->m_src = src; }
    long read_avail(void *dst, long size) override {
      return this->m_src->read(dst, size);
    }
    long remain() const override {
      return this->m_src->getSize() - this->m_src->getPos();
    }
    blargg_err_t skip(long count) override {
      return this->m_src->seek(count, SEEK_CUR) ? nullptr : eof_error;
    }
    void reset() { this->m_src->seek(0, SEEK_SET); }

  private:
    AudioFileSource *m_src;
  } m_reader;
  MusicEmu *m_emu{nullptr};
  std::array<int16_t, 1024> m_buffer;
  unsigned m_pos;
  bool m_isPlaying{false};
  bool m_load(int sample_rate);
  void m_cbInfo(const char *name, const char *value);
  void m_cbInfo(const char *name, long value);
  void m_cbTrackInfo();
};
