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

inline long AudioGeneratorGme::size() const { return this->file->getSize(); }

inline long AudioGeneratorGme::tell() const { return this->file->getPos(); }

inline blargg_err_t AudioGeneratorGme::seek(long pos) { return this->file->seek(pos, SEEK_SET) ? nullptr : eof_error; }

blargg_err_t AudioGeneratorGme::read(void *dst, long size) {
  if (this->remain() < size)
    return eof_error;
  uint8_t *p = static_cast<uint8_t *>(dst);
  while (size > 0) {
    auto len = this->file->read(p, size);
    if (len == 0)
      return eof_error;
    p += len;
    size -= len;
  }
  return nullptr;
}

long AudioGeneratorGme::read_avail(void *dst, long size) {
  size = std::min(size, this->remain());
  return this->read(dst, size) ? 0 : size;
}

AudioGeneratorGme::~AudioGeneratorGme() {
  mEmuDestroy();
  this->output->stop();
  this->file->close();
}

bool AudioGeneratorGme::loop() {
  while (this->running) {
    for (; mPos != mBuf.end(); mPos++) {  // += 2)
      int16_t b[2];
      b[0] = b[1] = *mPos;
      if (!this->output->ConsumeSample(b))
        break;
    }
    if (mPos != mBuf.end())
      break;
    if (mEmu->IsTrackEnded()) {
      this->running = false;
      this->cb.st(0, "Stop");
      break;
    }
    mPos = mBuf.begin();
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

bool AudioGeneratorGme::begin(AudioFileSource *src, AudioOutput *out) {
  if (src == nullptr || out == nullptr)
    return false;
  this->file = src;
  this->output = out;
  this->output->begin();
  return mLoad();
}

bool AudioGeneratorGme::mLoad() {
  char header[4];

  this->seek(0);
  this->read(header, sizeof(header));
  gme_type_t file_type = gme_identify_extension("STC");// gme_identify_header(header));

  if (file_type == nullptr) {
    this->cb.st(-1, gme_wrong_file_type);
    return false;
  }

  this->cb.st(0, file_type->system);

  if (!mEmuCreate(file_type)) {
    this->cb.st(-1, "Failed to create emulator");
    return false;
  }

  RemainingReader reader(header, sizeof(header), this);
  gme_err_t err = mEmu->Load(reader);

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
  this->stop();
  gme_delete(mEmu);
  mEmu = nullptr;
  mType = nullptr;
}

bool AudioGeneratorGme::playTrack(unsigned num) {
  if (mEmu == nullptr || num >= mEmu->GetTrackCount())
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
  mPos = mBuf.end();
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
  if (*value)
    this->cb.md(name, false, value);
}

void AudioGeneratorGme::mCbInfo(const char *name, long value) {
  char buf[32];
  if (value >= 0)
    this->cb.md(name, false, ltoa(value, buf, 10));
}
