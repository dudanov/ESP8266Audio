#include "Pt2Emu.h"
#include "../blargg_endian.h"
#include "../blargg_source.h"

#include <cstring>
#include <pgmspace.h>

/*
  Copyright (C) 2022 Sergey Dudanov. This module is free software; you
  can redistribute it and/or modify it under the terms of the GNU Lesser
  General Public License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version. This
  module is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
  details. You should have received a copy of the GNU Lesser General Public
  License along with this module; if not, write to the Free Software Foundation,
  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

namespace gme {
namespace emu {
namespace ay {
namespace pt2 {

static const auto CLOCK_RATE = CLK_SPECTRUM;
static const auto FRAME_RATE = FRAMERATE_SPECTRUM;

const int16_t PT2Module::NOTE_TABLE[96] PROGMEM = {
    0xEF8, 0xE10, 0xD60, 0xC80, 0xBD8, 0xB28, 0xA88, 0x9F0, 0x960, 0x8E0, 0x858, 0x7E0, 0x77C, 0x708, 0x6B0, 0x640,
    0x5EC, 0x594, 0x544, 0x4F8, 0x4B0, 0x470, 0x42C, 0x3FD, 0x3BE, 0x384, 0x358, 0x320, 0x2F6, 0x2CA, 0x2A2, 0x27C,
    0x258, 0x238, 0x216, 0x1F8, 0x1DF, 0x1C2, 0x1AC, 0x190, 0x17B, 0x165, 0x151, 0x13E, 0x12C, 0x11C, 0x10A, 0x0FC,
    0x0EF, 0x0E1, 0x0D6, 0x0C8, 0x0BD, 0x0B2, 0x0A8, 0x09F, 0x096, 0x08E, 0x085, 0x07E, 0x077, 0x070, 0x06B, 0x064,
    0x05E, 0x059, 0x054, 0x04F, 0x04B, 0x047, 0x042, 0x03F, 0x03B, 0x038, 0x035, 0x032, 0x02F, 0x02C, 0x02A, 0x027,
    0x025, 0x023, 0x021, 0x01F, 0x01D, 0x01C, 0x01A, 0x019, 0x017, 0x016, 0x015, 0x013, 0x012, 0x011, 0x010, 0x00F,
};

/* PT2 PLAYER */

static inline int16_t sGetNotePeriod(uint8_t tone) {
  return pgm_read_word(PT2Module::NOTE_TABLE + ((tone >= 95) ? 95 : tone));
}

static inline uint8_t sGetAmplitude(uint8_t volume, uint8_t amplitude) {
  return (volume * 17 + (volume > 7)) * amplitude / 256;
}

void Player::mInit() {
  mApu.Reset();
  mDelay.Init(mModule->GetDelay());
  mPositionIt = mModule->GetPositionBegin();
  memset(&mChannels, 0, sizeof(mChannels));
  auto pattern = mModule->GetPattern(mPositionIt);
  for (uint8_t idx = 0; idx != mChannels.size(); ++idx) {
    Channel &c = mChannels[idx];
    c.SetVolume(15);
    c.SetPatternData(mModule->GetPatternData(pattern, idx));
    c.SetSample(mModule->GetSample(1));
    c.SetOrnament(mModule->GetOrnament(0));
    c.SetSkipLocations(1);
  }
}

inline void Player::mAdvancePosition() {
  if (*++mPositionIt == 0xFF)
    mPositionIt = mModule->GetPositionLoop();
  auto pattern = mModule->GetPattern(mPositionIt);
  for (uint8_t idx = 0; idx != mChannels.size(); ++idx)
    mChannels[idx].SetPatternData(mModule->GetPatternData(pattern, idx));
}

void Player::mPlayPattern(const blip_clk_time_t time) {
  for (Channel &c : mChannels) {
    if (c.IsEmptyLocation())
      continue;
    const uint8_t prevNote = c.GetNote();
    const int16_t prevSliding = c.GetToneSlide();
    for (;;) {
      const uint8_t val = c.PatternCode();
      if (val >= 0xE1) {
        // Set sample.
        c.SetSample(mModule->GetSample(val - 0xE0));
      } else if (val == 0xE0) {
        // Pause. End position.
        c.Disable();
        c.Reset();
        break;
      } else if (val >= 0x80) {
        // Set note in semitones. End position.
        c.SetNote(val - 0x80);
        c.Reset();
        c.Enable();
        break;
      } else if (val == 0x7F) {
        // Disable envelope.
        c.EnvelopeDisable();
      } else if (val >= 0x71) {
        // Set envelope.
        c.EnvelopeEnable();
        mApu.Write(time, AyApu::AY_ENV_SHAPE, val - 0x70);
        mApu.Write(time, AyApu::AY_ENV_FINE, c.PatternCode());
        mApu.Write(time, AyApu::AY_ENV_COARSE, c.PatternCode());
      } else if (val == 0x70) {
        // Empty location. End position.
        break;
      } else if (val >= 0x60) {
        // Set ornament.
        c.SetOrnament(mModule->GetOrnament(val - 0x60));
      } else if (val >= 0x20) {
        // Set number of empty locations after the subsequent code.
        c.SetSkipLocations(val - 0x1F);
      } else if (val >= 0x10) {
        // Set volume.
        c.SetVolume(val - 0x10);
      } else if (val == 0x0F) {
        // Song Delay
        mDelay.Set(c.PatternCode());
      } else if (val == 0x0E) {
        // Gliss Effect
        c.SetupGliss(this);
      } else if (val == 0x0D) {
        // Portamento Effect
        c.SetupPortamento(this, prevNote, prevSliding);
      } else if (val == 0x0C) {
        // Gliss disable.
        ;
      } else if (val != 0x00) {
        // Set noise offset (occurs only in channel B).
        mNoiseBase = c.PatternCode();
      } else {
        mAdvancePosition();
      }
    }
  }
}

void Player::mPlaySamples(const blip_clk_time_t time) {
  uint8_t mixer = 0;
  for (uint8_t idx = 0; idx != mChannels.size(); ++idx, mixer >>= 1) {
    Channel &c = mChannels[idx];
    uint8_t amplitude = 0;

    if (c.IsEnabled()) {
      const SampleData &s = c.GetSampleData();
      amplitude = sGetAmplitude(c.GetVolume(), s.Amplitude());

      if (c.IsEnvelopeEnabled())
        amplitude |= 16;

      if (!s.NoiseMask())
        mApu.Write(time, AyApu::AY_NOISE_PERIOD, (mNoiseBase + s.Noise()) % 32);

      mixer |= 64 * s.NoiseMask() | 8 * s.ToneMask();

      const uint16_t tone = c.PlayTone();
      mApu.Write(time, AyApu::AY_CHNL_A_FINE + idx * 2, tone % 256);
      mApu.Write(time, AyApu::AY_CHNL_A_COARSE + idx * 2, tone / 256);

      c.Advance();
    }

    mApu.Write(time, AyApu::AY_CHNL_A_VOL + idx, amplitude);

    if (amplitude == 0)
      mixer |= 64 | 8;
  }
  mApu.Write(time, AyApu::AY_MIXER, mixer);
}

/* PT2 CHANNEL */

void Channel::Reset() {
  SetSamplePosition(0);
  SetOrnamentPosition(0);
  mToneSlide.Reset();
}

void Channel::SetupGliss(const Player *player) {
  mPortamento = false;
  uint8_t delay = PatternCode();
  if ((delay == 0) && (player->GetSubVersion() >= 7))
    delay++;
  mToneSlide.Enable(delay);
  mToneSlide.SetStep(PatternCodeLE16());
}

void Channel::SetupPortamento(const Player *player, const uint8_t prevNote, const int16_t prevSliding) {
  mPortamento = true;
  mToneSlide.Enable(PatternCode());
  mSkipPatternCode(2);
  int16_t step = PatternCodeLE16();
  if (step < 0)
    step = -step;
  mNoteSlide = mNote;
  mNote = prevNote;
  mToneDelta = sGetNotePeriod(mNoteSlide) - sGetNotePeriod(mNote);
  if (mToneDelta < mToneSlide.GetValue())
    step = -step;
  mToneSlide.SetStep(step);
}

inline uint8_t Channel::SlideNoise() {
  auto &sample = GetSampleData();
  const uint8_t value = sample.Noise() + mNoiseSlideStore;
  if (sample.NoiseEnvelopeStore())
    mNoiseSlideStore = value;
  return value;
}

inline void Channel::SlideEnvelope(int8_t &value) {
  auto &sample = GetSampleData();
  const int8_t tmp = sample.EnvelopeSlide() + mEnvelopeSlideStore;
  value += tmp;
  if (sample.NoiseEnvelopeStore())
    mEnvelopeSlideStore = tmp;
}

inline void Channel::mRunPortamento() {
  if (((mToneSlide.GetStep() < 0) && (mToneSlide.GetValue() <= mToneDelta)) ||
      ((mToneSlide.GetStep() >= 0) && (mToneSlide.GetValue() >= mToneDelta))) {
    mToneSlide.Reset();
    mNote = mNoteSlide;
  }
}

uint16_t Channel::PlayTone() {
  auto &s = GetSampleData();
  const int16_t tone = sGetNotePeriod(mNote + mOrnamentPlayer.GetData()) + s.Transposition() + mToneSlide.GetValue();
  if (mToneSlide.Run() && mPortamento)
    mRunPortamento();
  return tone & 0xFFF;
}

/* PT2 MODULE */

const PT2Module *PT2Module::GetModule(const uint8_t *data, const size_t size) {
  if (size <= sizeof(PT2Module))
    return nullptr;
  if (!memcmp_P(data, PT_SIGNATURE, sizeof(PT_SIGNATURE)) || !memcmp_P(data, VT_SIGNATURE, sizeof(VT_SIGNATURE)))
    return reinterpret_cast<const PT2Module *>(data);
  return nullptr;
}

unsigned PT2Module::CountSongLengthMs(unsigned &loop) const {
  const unsigned length = CountSongLength(loop) * 1000 / FRAME_RATE;
  loop = loop * 1000 / FRAME_RATE;
  return length;
}

unsigned PT2Module::LengthCounter::CountSongLength(const PT2Module *module, unsigned &loop) {
  unsigned frame = 0;

  // Init.
  mDelay = module->GetDelay();
  for (auto &c : mChannels)
    c.delay.Init(1);

  for (auto it = module->GetPositionBegin(); it != module->GetPositionEnd(); ++it) {
    // Store loop frame count.
    if (it == module->GetPositionLoop())
      loop = frame;

    // Update pattern data pointers.
    auto pattern = module->GetPattern(it);
    for (uint8_t idx = 0; idx != mChannels.size(); ++idx)
      mChannels[idx].data = module->GetPatternData(pattern, idx);

    // Count current position frames.
    frame += mCountPositionLength();
  }

  return frame;
}

unsigned PT2Module::LengthCounter::mCountPositionLength() {
  for (unsigned frames = 0;; frames += mDelay) {
    for (auto &c : mChannels) {
      if (!c.delay.Tick())
        continue;
      for (;;) {
        const uint8_t val = *c.data++;
        if (val == 0x00)
          return frames;
        if (val == 0x70 || (val >= 0x80 && val <= 0xE0))
          break;
        if (val == 0x0F)
          mDelay = *c.data++;
        else if (val == 0x0D)
          c.data += 3;
        else if (val >= 0x20 && val <= 0x5f)
          c.delay.Set(val - 0x1F);
        else if (val >= 0x71 && val <= 0x7e)
          c.data += 2;
        else if (val <= 0x0B || val == 0x0E)
          c.data += 1;
      }
    }
  }
}

/* PT2 FILE */

struct Pt2File : GmeInfo {
  const PT2Module *mModule;
  bool mHasTS;
  Pt2File() { mSetType(gme_pt2_type); }
  static MusicEmu *createPt2File() { return new Pt2File; }

  blargg_err_t mLoad(const uint8_t *data, const long size) override {
    mModule = PT2Module::GetModule(data, size);
    if (mModule == nullptr)
      return gme_wrong_file_type;
    mHasTS = PT2Module::FindTSModule(data, size) != nullptr;
    mSetTrackNum(1);
    return nullptr;
  }

  blargg_err_t mGetTrackInfo(track_info_t *out, const int track) const override {
    mModule->GetName(out->song);
    unsigned loop;
    out->length = mModule->CountSongLengthMs(loop);
    out->loop_length = loop;
    if (mHasTS)
      strcpy_P(out->comment, PSTR("6-ch TurboSound (TS)"));
    return nullptr;
  }
};

/* PT2 EMULATOR */

Pt2Emu::Pt2Emu() : mTurboSound(nullptr) {
  static const char *const CHANNELS_NAMES[] = {
      "Wave 1", "Wave 2", "Wave 3", "Wave 4", "Wave 5", "Wave 6",
  };
  static int const CHANNELS_TYPES[] = {
      WAVE_TYPE | 0, WAVE_TYPE | 1, WAVE_TYPE | 2, WAVE_TYPE | 3, WAVE_TYPE | 4, WAVE_TYPE | 5,
  };
  mSetType(gme_pt2_type);
  mSetChannelsNames(CHANNELS_NAMES);
  mSetChannelsTypes(CHANNELS_TYPES);
  mSetSilenceLookahead(1);
}

Pt2Emu::~Pt2Emu() { mDestroyTS(); }

blargg_err_t Pt2Emu::mGetTrackInfo(track_info_t *out, const int track) const {
  mPlayer.GetName(out->song);
  unsigned loop;
  out->length = mPlayer.CountSongLengthMs(loop);
  out->loop_length = loop;
  if (mHasTS())
    strcpy_P(out->comment, PSTR("6-ch TurboSound (TS)"));
  return nullptr;
}

bool Pt2Emu::mCreateTS() {
  if (!mHasTS())
    mTurboSound = new Player;
  return mHasTS();
}

void Pt2Emu::mDestroyTS() {
  if (!mHasTS())
    return;
  delete mTurboSound;
  mTurboSound = nullptr;
}

blargg_err_t Pt2Emu::mLoad(const uint8_t *data, const long size) {
  auto module = PT2Module::GetModule(data, size);
  if (module == nullptr)
    return gme_wrong_file_type;
  mSetTrackNum(1);
  mPlayer.Load(module);
  module = PT2Module::FindTSModule(data, size);
  if (module == nullptr) {
    mDestroyTS();
  } else if (mCreateTS()) {
    mTurboSound->Load(module);
    mPlayer.SetVolume(mGetGain());
    mTurboSound->SetVolume(mGetGain());
    mSetChannelsNumber(AyApu::OSCS_NUM * 2);
    return mSetupBuffer(CLOCK_RATE);
  }
  mPlayer.SetVolume(mGetGain());
  mSetChannelsNumber(AyApu::OSCS_NUM);
  return mSetupBuffer(CLOCK_RATE);
}

void Pt2Emu::mUpdateEq(const BlipEq &eq) {}  // mApu.SetTrebleEq(eq); }

void Pt2Emu::mSetChannel(const int idx, BlipBuffer *center, BlipBuffer *, BlipBuffer *) {
  if (idx < AyApu::OSCS_NUM)
    mPlayer.SetOscOutput(idx, center);
  else if (mHasTS())
    mTurboSound->SetOscOutput(idx - AyApu::OSCS_NUM, center);
}

void Pt2Emu::mSetTempo(double temp) {
  mFramePeriod = static_cast<blip_clk_time_t>(mGetClockRate() / FRAME_RATE / temp);
}

blargg_err_t Pt2Emu::mStartTrack(const int track) {
  RETURN_ERR(ClassicEmu::mStartTrack(track));
  mEmuTime = 0;
  mPlayer.Init();
  if (mHasTS())
    mTurboSound->Init();
  SetTempo(mGetTempo());
  return nullptr;
}

blargg_err_t Pt2Emu::mRunClocks(blip_clk_time_t &duration) {
  for (; mEmuTime <= duration; mEmuTime += mFramePeriod) {
    mPlayer.RunUntil(mEmuTime);
    if (mHasTS())
      mTurboSound->RunUntil(mEmuTime);
  }
  mEmuTime -= duration;
  mPlayer.EndFrame(duration);
  if (mHasTS())
    mTurboSound->EndFrame(duration);
  return nullptr;
}

}  // namespace pt2
}  // namespace ay
}  // namespace emu
}  // namespace gme

static const gme_type_t_ gme_pt2_type_ = {
    "ZX Spectrum (PT 2.x)",
    1,
    0,
    &gme::emu::ay::pt2::Pt2Emu::createPt2Emu,
    &gme::emu::ay::pt2::Pt2File::createPt2File,
    "PT2",
    1,
};
extern gme_type_t const gme_pt2_type = &gme_pt2_type_;
