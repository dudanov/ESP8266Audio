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
  int last_time;
  short *out;
  enum { disabled_time = -1 };

 public:
  YmEmu() : last_time(disabled_time), out(NULL) {}
  void enable(bool b) { last_time = b ? 0 : disabled_time; }
  bool enabled() const { return last_time != disabled_time; }
  void begin_frame(short *p);
  int run_until(int time);
};

class VgmEmuImpl : public ClassicEmu, private DualResampler {
 public:
  typedef ClassicEmu::sample_t sample_t;

 protected:
  enum { stereo = 2 };

  typedef int vgm_time_t;

  enum { fm_time_bits = 12 };
  typedef int fm_time_t;
  long fm_time_offset;
  int fm_time_factor;
  fm_time_t to_fm_time(vgm_time_t) const;

  enum { blip_time_bits = 12 };
  int blip_time_factor;
  blip_time_t to_blip_time(vgm_time_t) const;

  uint8_t const *data;
  uint8_t const *loop_begin;
  uint8_t const *data_end;
  void update_fm_rates(long *ym2413_rate, long *ym2612_rate) const;

  vgm_time_t vgm_time;
  uint8_t const *pos;
  blip_time_t run_commands(vgm_time_t);
  int mPlayFrame(blip_time_t blip_time, int sample_count, sample_t *buf) override;

  uint8_t const *pcm_data;
  uint8_t const *pcm_pos;
  int dac_amp;
  int dac_disabled;  // -1 if disabled
  void write_pcm(vgm_time_t, int amp);

  YmEmu<Ym2612Emu> ym2612[2];
  YmEmu<Ym2413Emu> ym2413[2];

  BlipBuffer blip_buf;
  gme::emu::sms::SmsApu psg[2];
  bool psg_dual{};
  bool psg_t6w28;
  BlipSynth<BLIP_MED_QUALITY, 1> dac_synth;

  friend class VgmEmu;
};

}  // namespace vgm
}  // namespace emu
}  // namespace gme
