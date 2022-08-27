// Low-level parts of VgmEmu

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once

#include "ClassicEmu.h"
#include "DualResampler.h"
#include "SmsApu.h"
#include "Ym2413Emu.h"
#include "Ym2612Emu.h"

namespace gme {
namespace emu {
namespace vgm {

template<class Emu> class YmEmu : public Emu {
 protected:
  int mLastTime;
  short *mOut;
  enum { DISABLED_TIME = -1 };

 public:
  YmEmu() : mLastTime(DISABLED_TIME), mOut(NULL) {}
  void enable(bool b) { mLastTime = b ? 0 : DISABLED_TIME; }
  bool enabled() const { return mLastTime != DISABLED_TIME; }
  void begin_frame(short *p);
  int mRunUntil(int time);
};

class VgmEmuImpl : public ClassicEmu, private DualResampler {
 public:
  typedef ClassicEmu::sample_t sample_t;

 protected:
  enum { STEREO = 2 };

  typedef int vgm_time_t;

  enum { FM_TIME_BITS = 12 };
  typedef int fm_time_t;
  long mFmTimeOffset;
  int mFmTimeFactor;
  fm_time_t mToFmTime(vgm_time_t) const;

  enum { BLIP_TIME_BITS = 12 };
  int mBlipTimeFactor;
  blip_time_t mToBlipTime(vgm_time_t) const;

  uint8_t const *mData;
  uint8_t const *mLoopBegin;
  uint8_t const *mDataEnd;
  void mUpdateFmRates(long *ym2413_rate, long *ym2612_rate) const;

  blip_clk_time_t mVgmTime;
  const uint8_t *mPos;
  blip_time_t mRunCommands(blip_clk_time_t);
  int mPlayFrame(blip_time_t blip_time, int sample_count, sample_t *buf) override;

  uint8_t const *mPcmData;
  uint8_t const *mPcmPos;
  int mDacAmp;
  int mDacDisabled;  // -1 if disabled
  void mWritePcm(vgm_time_t, int amp);

  YmEmu<Ym2612Emu> mYm2612[2];
  YmEmu<Ym2413Emu> mYm2413[2];

  BlipBuffer mBlipBuf;
  gme::emu::sms::SmsApu mPsg[2];
  bool mPsgDual{};
  bool mPsgT6w28;
  BlipSynth<BLIP_MED_QUALITY, 1> mDacSynth;

  friend class VgmEmu;
};

}  // namespace vgm
}  // namespace emu
}  // namespace gme
