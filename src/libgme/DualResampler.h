// Combination of FirResampler and BlipBuffer mixing. Used by Sega FM
// emulators.

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once
#include "BlipBuffer.h"
#include "FirResampler.h"

class DualResampler {
 public:
  virtual ~DualResampler() {}

  typedef int16_t dsample_t;

  double setup(double oversample, double rolloff, double gain) {
    return this->m_resampler.setTimeRatio(oversample, rolloff, gain * 0.5);
  }
  blargg_err_t reset(int max_pairs);
  void resize(int pairs_per_frame);
  void clear() {
    this->m_bufPosition = this->m_sampleBufSize;
    this->m_resampler.clear();
  }

  void dualPlay(long count, dsample_t *out, BlipBuffer &);

 protected:
  virtual int mPlayFrame(blip_time_t, int pcm_count, dsample_t *pcm_out) = 0;

 private:
  blargg_vector<dsample_t> m_sampleBuf;
  int m_sampleBufSize{0};
  int m_oversamplesPerFrame{-1};
  int m_bufPosition{-1};
  int m_resamplerSize{0};

  FirResampler<12> m_resampler;
  void m_mixSamples(BlipBuffer &, dsample_t *);
  void mPlayFrame(BlipBuffer &, dsample_t *);
};
