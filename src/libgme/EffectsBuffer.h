// Multi-channel effects buffer with panning, echo and reverb

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once
#include "MultiBuffer.h"
#include <vector>

// EffectsBuffer uses several buffers and outputs stereo sample pairs.
class EffectsBuffer : public MultiBuffer {
 public:
  // nVoices indicates the number of voices for which buffers will be
  // allocated to make EffectsBuffer work as "mix everything to one", nVoices
  // will be 1 If center_only is true, only center buffers are created and
  // less memory is used.
  EffectsBuffer(int nVoices = 1, bool center_only = false);

  // Channel  Effect    Center Pan
  // ---------------------------------
  //    0,5    reverb       pan_1
  //    1,6    reverb       pan_2
  //    2,7    echo         -
  //    3      echo         -
  //    4      echo         -

  // Channel configuration
  struct config_t {
    double pan_1{-0.15};  // -1.0 = left, 0.0 = center, 1.0 = right
    double pan_2{0.15};
    double echo_delay{61.0};      // msec
    double echo_level{0.1};       // 0.0 to 1.0
    double reverb_delay{88.0};    // msec
    double delay_variance{18.0};  // difference between left/right delays (msec)
    double reverb_level{0.12};    // 0.0 to 1.0
    bool effects_enabled{false};  // if false, use optimized simple mixer
  };

  // Set configuration of buffer
  virtual void config(const config_t &);
  void setDepth(double);

 public:
  ~EffectsBuffer();
  blargg_err_t setSampleRate(long samples_per_sec, int msec = BLIP_DEFAULT_LENGTH) override;
  void setClockRate(long) override;
  void setBassFreq(int) override;
  void clear() override;
  const Channel &getChannelBuffers(int, int) const override;
  void endFrame(blip_time_t) override;
  long readSamples(blip_sample_t *, long) override;
  long samplesAvailable() const override;

 private:
  typedef long fixed_t;
  int m_maxChannels;
  enum { MAX_BUFS_NUM = 7 };
  long m_bufNum;
  std::vector<BlipBuffer> m_bufs;
  enum { CHAN_TYPES_NUM = 3 };
  std::vector<Channel> m_chanTypes;
  config_t m_config;
  long m_stereoRemain;
  long m_effectRemain;
  bool m_effectsEnabled;

  std::vector<std::vector<blip_sample_t>> m_reverbBuf;
  std::vector<std::vector<blip_sample_t>> m_echoBuf;
  std::vector<int> m_reverbPos;
  std::vector<int> m_echoPos;

  struct {
    fixed_t pan_1_levels[2];
    fixed_t pan_2_levels[2];
    int echo_delay_l;
    int echo_delay_r;
    fixed_t echo_level;
    int reverb_delay_l;
    int reverb_delay_r;
    fixed_t reverb_level;
  } chans;

  void m_mixMono(blip_sample_t *, blargg_long);
  void m_mixStereo(blip_sample_t *, blargg_long);
  void m_mixEnhanced(blip_sample_t *, blargg_long);
  void m_mixMonoEnhanced(blip_sample_t *, blargg_long);
};
