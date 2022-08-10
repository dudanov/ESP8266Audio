/*
  AudioGeneratorGme
  Audio output generator that reads 8 and 16-bit WAV files

  Copyright (C) 2017  Earle F. Philhower, III

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

#include "AudioGeneratorGme.h"
#include "libgme/MusicEmu.h"

bool AudioGeneratorGme::begin(AudioFileSource *source, AudioOutput *output) {
  if (source == nullptr || output == nullptr)
    return false;
  this->m_reader.set_source(source);
  this->output = output;
  this->file = source;
  return true;
}

bool AudioGeneratorGme::loop() {
  while (this->m_isPlaying) {
    for (; this->m_pos != this->m_buffer.size(); this->m_pos += 2)
      if (!this->output->ConsumeSample(&this->m_buffer[this->m_pos]))
        break;
    if (this->m_pos != this->m_buffer.size())
      break;
    if (this->m_emu->isTrackEnded()) {
      this->m_isPlaying = false;
      this->cb.st(0, "Stop");
      break;
    }
    this->m_pos = 0;
    gme_err_t err =
        this->m_emu->play(this->m_buffer.size(), this->m_buffer.data());
    if (err != nullptr) {
      this->cb.st(-1, err);
      this->m_isPlaying = false;
      break;
    }
  }
  this->file->loop();
  this->output->loop();
  return this->m_isPlaying;
}

bool AudioGeneratorGme::stop() {
  this->m_isPlaying = false;
  this->output->stop();
  this->m_pos = this->m_buffer.size();
  gme_delete(this->m_emu);
  this->m_emu = nullptr;
  return true;
}

void AudioGeneratorGme::m_cbInfo(const char *name, const char *value) {
  if (strlen(value))
    this->cb.md(name, false, value);
}

void AudioGeneratorGme::m_cbInfo(const char *name, long value) {
  if (value < 0)
    return;
  char buf[32];
  this->cb.md(name, false, ltoa(value, buf, 10));
}

void AudioGeneratorGme::m_cbTrackInfo() {
  track_info_t info;
  gme_err_t err = this->m_emu->getTrackInfo(&info);
  if (err != nullptr) {
    this->cb.st(-1, err);
    return;
  }
  this->m_cbInfo("Tracks", info.track_count);
  this->m_cbInfo("Length(ms)", info.length);
  this->m_cbInfo("Intro(ms)", info.intro_length);
  this->m_cbInfo("Loop(ms)", info.loop_length);
  this->m_cbInfo("Fade(ms)", info.fade_length);
  this->m_cbInfo("System", info.system);
  this->m_cbInfo("Game", info.game);
  this->m_cbInfo("Song", info.song);
  this->m_cbInfo("Author", info.author);
  this->m_cbInfo("Copyright", info.copyright);
  this->m_cbInfo("Comment", info.comment);
  this->m_cbInfo("Dumper", info.dumper);
}

bool AudioGeneratorGme::startTrack(int num) {
  if (this->m_emu == nullptr && !this->m_load(this->output->GetRate()))
    return false;
  blargg_err_t err = this->m_emu->startTrack(num);
  if (err != nullptr) {
    this->cb.st(-1, err);
    return false;
  }
  this->m_isPlaying = true;
  this->m_cbTrackInfo();
  return true;
}

bool AudioGeneratorGme::m_load(int sample_rate) {
  char header[4];

  this->m_reader.reset();
  this->m_reader.read_avail(header, sizeof(header));
  gme_type_t file_type = gme_identify_extension(gme_identify_header(header));

  if (file_type == nullptr) {
    this->cb.st(-1, gme_wrong_file_type);
    return false;
  }

  this->m_emu = gme_new_emu(file_type, sample_rate);
  if (this->m_emu == nullptr) {
    this->cb.st(-1, "Failed to create emulator");
    return false;
  }

  this->m_reader.reset();
  gme_err_t err = this->m_emu->load(this->m_reader);

  if (err != nullptr) {
    this->cb.st(-1, err);
    delete this->m_emu;
    this->m_emu = nullptr;
    return false;
  }

  return true;
}
