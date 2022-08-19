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

#include "AudioGeneratorGme.h"
#include "libgme/MusicEmu.h"

inline void
AudioGeneratorGme::AudioFileReader::set_source(AudioFileSource *src) {
  mSource = src;
  mSource->seek(0, SEEK_SET);
}

inline long AudioGeneratorGme::AudioFileReader::remain() const {
  return mSource->getSize() - mSource->getPos();
}

blargg_err_t AudioGeneratorGme::AudioFileReader::skip(long count) {
  return mSource->seek(count, SEEK_CUR) ? nullptr : eof_error;
}

blargg_err_t AudioGeneratorGme::AudioFileReader::read(void *dst, long size) {
  uint8_t *p = static_cast<uint8_t *>(dst);
  while (size > 0) {
    auto len = mSource->read(p, size);
    if (len == 0)
      return eof_error;
    p += len;
    size -= len;
  }
  return nullptr;
}

long AudioGeneratorGme::AudioFileReader::read_avail(void *dst, long size) {
  if (size > this->remain())
    size = this->remain();
  return this->read(dst, size) ? 0 : size;
}

AudioGeneratorGme::~AudioGeneratorGme() {
  this->stop();
  this->output->stop();
  this->file->close();
  mEmuDestroy();
}

bool AudioGeneratorGme::begin(AudioFileSource *src, AudioOutput *out) {
  if (src == nullptr || out == nullptr)
    return false;
  this->file = src;
  this->output = out;
  this->output->begin();
  mReader.set_source(src);
  mLoad();
  return true;
}

bool AudioGeneratorGme::loop() {
  while (this->running) {
    for (; mPos != mBuf.size(); mPos += 2)
      if (!this->output->ConsumeSample(&mBuf[mPos]))
        break;
    if (mPos != mBuf.size())
      break;
    if (mEmu->IsTrackEnded()) {
      this->running = false;
      this->cb.st(0, "Stop");
      break;
    }
    mPos = 0;
    gme_err_t err = mEmu->Play(mBuf.size(), mBuf.data());
    if (err != nullptr) {
      this->cb.st(-1, err);
      this->running = false;
      break;
    }
  }
  this->file->loop();
  this->output->loop();
  return this->running;
}

bool AudioGeneratorGme::mLoad() {
  char header[4];

  mReader.read(header, sizeof(header));
  gme_type_t file_type = gme_identify_extension(gme_identify_header(header));

  if (file_type == nullptr) {
    this->cb.st(-1, gme_wrong_file_type);
    return false;
  }

  this->cb.st(0, file_type->system);

  if (!mEmuCreate(file_type)) {
    this->cb.st(-1, "Failed to create emulator");
    return false;
  }

  RemainingReader reader(header, sizeof(header), &mReader);
  gme_err_t err = mEmu->load(reader);

  if (err != nullptr) {
    mEmuDestroy();
    this->cb.st(-1, err);
    return false;
  }

  return true;
}

bool AudioGeneratorGme::mEmuCreate(gme_type_t file_type) {
  if (mEmu != nullptr) {
    if (mType == file_type)
      return true;
    mEmuDestroy();
  }
  const int current = this->output->GetRate();
  int rate = file_type->sample_rate;
  if (rate > 0) {
    if (rate != current)
      this->output->SetRate(rate);
  } else {
    rate = current;
  }
  mEmu = gme_new_emu(file_type, rate);
  if (mEmu != nullptr) {
    mType = file_type;
    return true;
  }
  return false;
}

void AudioGeneratorGme::mEmuDestroy() {
  this->running = false;
  gme_delete(mEmu);
  mEmu = nullptr;
  mType = nullptr;
}

bool AudioGeneratorGme::playTrack(unsigned num) {
  if (mEmu == nullptr || num >= mEmu->getTrackCount())
    return false;
  blargg_err_t err = mEmu->StartTrack(num);
  if (err != nullptr) {
    this->cb.st(-1, err);
    this->running = false;
    return false;
  }
  mCbTrackInfo();
  this->running = true;
  return true;
}

bool AudioGeneratorGme::stop() {
  this->running = false;
  mPos = mBuf.size();
  return true;
}

void AudioGeneratorGme::mCbTrackInfo() {
  track_info_t info;
  gme_err_t err = mEmu->GetTrackInfo(&info);
  if (err != nullptr) {
    this->cb.st(-1, err);
    return;
  }
  mCbInfo("Tracks", info.track_count);
  mCbInfo("Length(ms)", info.length);
  mCbInfo("Intro(ms)", info.intro_length);
  mCbInfo("Loop(ms)", info.loop_length);
  mCbInfo("Fade(ms)", info.fade_length);
  mCbInfo("System", info.system);
  mCbInfo("Game", info.game);
  mCbInfo("Song", info.song);
  mCbInfo("Author", info.author);
  mCbInfo("Copyright", info.copyright);
  mCbInfo("Comment", info.comment);
  mCbInfo("Dumper", info.dumper);
}

void AudioGeneratorGme::mCbInfo(const char *name, const char *value) {
  if (strlen(value))
    this->cb.md(name, false, value);
}

void AudioGeneratorGme::mCbInfo(const char *name, long value) {
  char buf[32];
  if (value >= 0)
    this->cb.md(name, false, ltoa(value, buf, 10));
}
