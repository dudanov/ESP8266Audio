// Multi-channel sound buffer interface, and basic mono and stereo buffers

// Blip_Buffer 0.4.1
#pragma once
#include "BlipBuffer.h"
#include "blargg_common.h"

// Interface to one or more Blip_Buffers mapped to one or more channels
// consisting of left, center, and right buffers.
class MultiBuffer {
 public:
  MultiBuffer(int samplesPerFrame) : mSamplesPerFrame(samplesPerFrame) {}
  virtual ~MultiBuffer() {}

  // Set the number of channels available
  virtual blargg_err_t setChannelCount(int) { return nullptr; }

  // Get indexed channel, from 0 to channel count - 1
  struct Channel {
    BlipBuffer *center;
    BlipBuffer *left;
    BlipBuffer *right;
  };
  enum { TYPE_INDEX_MASK = 0xFF };
  enum { WAVE_TYPE = 0x100, NOISE_TYPE = 0x200, MIXED_TYPE = WAVE_TYPE | NOISE_TYPE };
  virtual const Channel &getChannelBuffers(int index, int type) const = 0;

  // See Blip_Buffer.h
  virtual blargg_err_t SetSampleRate(long rate, int msec) {
    mSampleRate = rate;
    mLength = msec;
    return 0;
  }
  virtual void setClockRate(long) = 0;
  virtual void setBassFreq(int) = 0;
  virtual void clear() = 0;
  long GetSampleRate() const { return mSampleRate; }

  // Length of buffer, in milliseconds
  int getLength() const { return mLength; }

  // See Blip_Buffer.h
  virtual void EndFrame(blip_time_t) = 0;

  // Number of samples per output frame (1 = mono, 2 = stereo)
  int getSamplesPerFrame() const { return mSamplesPerFrame; }

  // Count of changes to channel configuration. Incremented whenever
  // a change is made to any of the Blip_Buffers for any channel.
  unsigned getChangedChannelsNumber() { return mChangedChannelsNumber; }

  // See Blip_Buffer.h
  virtual long readSamples(blip_sample_t *, long) = 0;
  virtual long samplesAvailable() const = 0;

 protected:
  void m_channelChanged() { mChangedChannelsNumber++; }

 private:
  // noncopyable
  MultiBuffer(const MultiBuffer &);
  MultiBuffer &operator=(const MultiBuffer &);

  const int mSamplesPerFrame;
  unsigned mChangedChannelsNumber{1};
  long mSampleRate{0};
  int mLength{0};
};

// Uses a single buffer and outputs mono samples.
class MonoBuffer : public MultiBuffer {
  BlipBuffer mBuf;
  Channel mChan;

 public:
  // Buffer used for all channels
  BlipBuffer *center() { return &mBuf; }

 public:
  MonoBuffer() : MultiBuffer(1) {
    mChan.center = &mBuf;
    mChan.left = &mBuf;
    mChan.right = &mBuf;
  }
  ~MonoBuffer() {}
  blargg_err_t SetSampleRate(long rate, int msec = BLIP_DEFAULT_LENGTH) override;
  void setClockRate(long rate) override { mBuf.SetClockRate(rate); }
  void setBassFreq(int freq) override { mBuf.SetBassFrequency(freq); }
  void clear() override { mBuf.Clear(); }
  long samplesAvailable() const override { return mBuf.SamplesAvailable(); }
  long readSamples(blip_sample_t *p, long s) override { return mBuf.ReadSamples(p, s); }
  const Channel &getChannelBuffers(int, int) const override { return mChan; }
  void EndFrame(blip_time_t t) override { mBuf.EndFrame(t); }
};

// Uses three buffers (one for center) and outputs stereo sample pairs.
class StereoBuffer : public MultiBuffer {
 public:
  // Buffers used for all channels
  BlipBuffer *center() { return &mBufs[0]; }
  BlipBuffer *left() { return &mBufs[1]; }
  BlipBuffer *right() { return &mBufs[2]; }

 public:
  StereoBuffer() : MultiBuffer(2) {
    mChan.center = &mBufs[0];
    mChan.left = &mBufs[1];
    mChan.right = &mBufs[2];
  }
  ~StereoBuffer() {}
  blargg_err_t SetSampleRate(long, int msec = BLIP_DEFAULT_LENGTH) override;
  void setClockRate(long) override;
  void setBassFreq(int) override;
  void clear() override;
  const Channel &getChannelBuffers(int, int) const override { return mChan; }
  void EndFrame(blip_time_t) override;
  long samplesAvailable() const override { return mBufs[0].SamplesAvailable() * 2; }
  long readSamples(blip_sample_t *, long) override;

 private:
  enum { BUFS_NUM = 3 };
  std::array<BlipBuffer, BUFS_NUM> mBufs;
  Channel mChan;
  int mStereoAdded;
  int mWasStereo;

  void mMixStereoNoCenter(blip_sample_t *, blargg_long);
  void mMixStereo(blip_sample_t *, blargg_long);
  void mMixMono(blip_sample_t *, blargg_long);
};

// SilentBuffer generates no samples, useful where no sound is wanted
class SilentBuffer : public MultiBuffer {
  Channel mChan;

 public:
  SilentBuffer()
      : MultiBuffer(1)  // 0 channels would probably confuse
  {
    // TODO: better to use empty BlipBuffer so caller never has to check for
    // NULL?
    mChan.left = 0;
    mChan.center = 0;
    mChan.right = 0;
  }
  blargg_err_t SetSampleRate(long rate, int msec = BLIP_DEFAULT_LENGTH) {
    return MultiBuffer::SetSampleRate(rate, msec);
  }
  void setClockRate(long) override {}
  void setBassFreq(int) override {}
  void clear() override {}
  const Channel &getChannelBuffers(int, int) const override { return mChan; }
  void EndFrame(blip_time_t) override {}
  long samplesAvailable() const override { return 0; }
  long readSamples(blip_sample_t *, long) override { return 0; }
};
