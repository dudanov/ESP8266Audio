// Multi-channel sound buffer interface, and basic mono and stereo buffers

// Blip_Buffer 0.4.1
#pragma once
#include "BlipBuffer.h"
#include "blargg_common.h"

// Interface to one or more Blip_Buffers mapped to one or more channels
// consisting of left, center, and right buffers.
class MultiBuffer {
 public:
  MultiBuffer(int samplesPerFrame) : m_samplesPerFrame(samplesPerFrame) {}
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
  virtual blargg_err_t setSampleRate(long rate, int msec) {
    m_sampleRate = rate;
    m_length = msec;
    return 0;
  }
  virtual void setClockRate(long) = 0;
  virtual void setBassFreq(int) = 0;
  virtual void clear() = 0;
  long getSampleRate() const { return m_sampleRate; }

  // Length of buffer, in milliseconds
  int getLength() const { return m_length; }

  // See Blip_Buffer.h
  virtual void endFrame(blip_time_t) = 0;

  // Number of samples per output frame (1 = mono, 2 = stereo)
  int getSamplesPerFrame() const { return m_samplesPerFrame; }

  // Count of changes to channel configuration. Incremented whenever
  // a change is made to any of the Blip_Buffers for any channel.
  unsigned getChangedChannelsNumber() { return this->m_changedChannelsNumber; }

  // See Blip_Buffer.h
  virtual long readSamples(blip_sample_t *, long) = 0;
  virtual long samplesAvailable() const = 0;

 public:
  BLARGG_DISABLE_NOTHROW
 protected:
  void m_channelChanged() { this->m_changedChannelsNumber++; }

 private:
  // noncopyable
  MultiBuffer(const MultiBuffer &);
  MultiBuffer &operator=(const MultiBuffer &);

  const int m_samplesPerFrame;
  unsigned m_changedChannelsNumber{1};
  long m_sampleRate{0};
  int m_length{0};
};

// Uses a single buffer and outputs mono samples.
class MonoBuffer : public MultiBuffer {
  BlipBuffer m_buf;
  Channel m_chan;

 public:
  // Buffer used for all channels
  BlipBuffer *center() { return &m_buf; }

 public:
  MonoBuffer() : MultiBuffer(1) {
    m_chan.center = &m_buf;
    m_chan.left = &m_buf;
    m_chan.right = &m_buf;
  }
  ~MonoBuffer() {}
  blargg_err_t setSampleRate(long rate, int msec = BLIP_DEFAULT_LENGTH) override;
  void setClockRate(long rate) override { this->m_buf.setClockRate(rate); }
  void setBassFreq(int freq) override { this->m_buf.setBassFrequency(freq); }
  void clear() override { this->m_buf.clear(); }
  long samplesAvailable() const override { return this->m_buf.samplesAvailable(); }
  long readSamples(blip_sample_t *p, long s) override { return this->m_buf.readSamples(p, s); }
  const Channel &getChannelBuffers(int, int) const override { return this->m_chan; }
  void endFrame(blip_time_t t) override { this->m_buf.end_frame(t); }
};

// Uses three buffers (one for center) and outputs stereo sample pairs.
class StereoBuffer : public MultiBuffer {
 public:
  // Buffers used for all channels
  BlipBuffer *center() { return &this->m_bufs[0]; }
  BlipBuffer *left() { return &this->m_bufs[1]; }
  BlipBuffer *right() { return &this->m_bufs[2]; }

 public:
  StereoBuffer() : MultiBuffer(2) {
    m_chan.center = &m_bufs[0];
    m_chan.left = &m_bufs[1];
    m_chan.right = &m_bufs[2];
  }
  ~StereoBuffer() {}
  blargg_err_t setSampleRate(long, int msec = BLIP_DEFAULT_LENGTH) override;
  void setClockRate(long) override;
  void setBassFreq(int) override;
  void clear() override;
  const Channel &getChannelBuffers(int, int) const override { return this->m_chan; }
  void endFrame(blip_time_t) override;
  long samplesAvailable() const override { return this->m_bufs[0].samplesAvailable() * 2; }
  long readSamples(blip_sample_t *, long) override;

 private:
  enum { BUFS_NUM = 3 };
  std::array<BlipBuffer, BUFS_NUM> m_bufs;
  Channel m_chan;
  int m_stereoAdded;
  int m_wasStereo;

  void m_mixStereoNoCenter(blip_sample_t *, blargg_long);
  void m_mixStereo(blip_sample_t *, blargg_long);
  void m_mixMono(blip_sample_t *, blargg_long);
};

// SilentBuffer generates no samples, useful where no sound is wanted
class SilentBuffer : public MultiBuffer {
  Channel m_chan;

 public:
  SilentBuffer()
      : MultiBuffer(1)  // 0 channels would probably confuse
  {
    // TODO: better to use empty BlipBuffer so caller never has to check for
    // NULL?
    m_chan.left = 0;
    m_chan.center = 0;
    m_chan.right = 0;
  }
  blargg_err_t setSampleRate(long rate, int msec = BLIP_DEFAULT_LENGTH) {
    return MultiBuffer::setSampleRate(rate, msec);
  }
  void setClockRate(long) override {}
  void setBassFreq(int) override {}
  void clear() override {}
  const Channel &getChannelBuffers(int, int) const override { return m_chan; }
  void endFrame(blip_time_t) override {}
  long samplesAvailable() const override { return 0; }
  long readSamples(blip_sample_t *, long) override { return 0; }
};
