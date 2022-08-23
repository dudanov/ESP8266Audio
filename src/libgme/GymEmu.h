// Sega Genesis/Mega Drive GYM music file emulator
// Includes with PCM timing recovery to improve sample quality.

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#ifndef GYM_EMU_H
#define GYM_EMU_H

#include "DualResampler.h"
#include "MusicEmu.h"
#include "SmsApu.h"
#include "Ym2612Emu.h"

class GymEmu : public MusicEmu, private DualResampler {
 public:
  GymEmu();
  ~GymEmu();
  // GYM file header
  enum { header_size = 428 };
  struct header_t {
    char tag[4];
    char song[32];
    char game[32];
    char copyright[32];
    char emulator[32];
    char dumper[32];
    char comment[256];
    uint8_t loop_start[4];  // in 1/60 seconds, 0 if not looped
    uint8_t packed[4];
  };

  // Header for currently loaded file
  header_t const &header() const { return header_; }

  static gme_type_t static_type() { return gme_gym_type; }

 protected:
  blargg_err_t mLoad(uint8_t const *, long) override;
  blargg_err_t mGetTrackInfo(track_info_t *, int track) const override;
  blargg_err_t mSetSampleRate(long sample_rate) override;
  blargg_err_t mStartTrack(int) override;
  blargg_err_t mPlay(long count, sample_t *) override;
  void mMuteChannel(int) override;
  void mSetTempo(double) override;
  int mPlayFrame(blip_time_t blip_time, int sample_count, sample_t *buf) override;

 private:
  // sequence data begin, loop begin, current position, end
  const uint8_t *data;
  const uint8_t *loop_begin;
  const uint8_t *pos;
  const uint8_t *data_end;
  blargg_long loop_remain;  // frames remaining until loop beginning has been located
  header_t header_;
  double fm_sample_rate;
  blargg_long clocks_per_frame;
  void parse_frame();

  // dac (pcm)
  int dac_amp;
  int prev_dac_count;
  bool dac_enabled;
  bool dac_muted;
  void run_dac(int);

  // sound
  BlipBuffer blip_buf;
  Ym2612Emu fm;
  BlipSynth<BLIP_MED_QUALITY, 1> dac_synth;
  gme::emu::sms::SmsApu mApu;
  uint8_t dac_buf[1024];
};

#endif
