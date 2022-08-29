// Band-limited sound synthesis buffer

// BlipBuffer 0.4.1
#pragma once
#include <array>
#include <cstdint>

typedef int32_t blip_long_t;
typedef uint32_t blip_ulong_t;

// Time unit at source clock rate
typedef blip_long_t blip_time_t;

// Output samples are 16-bit signed, with a range of -32768 to 32767
typedef int16_t blip_sample_t;
// enum { BLIP_SAMPLE_MAX = 32767 };

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

// RU: Количество бит в доле передискретизации. Более высокие значения дают более точное соотношение, но уменьшают
//     максимальный размер буфера.
// EN: Number of bits in resample ratio fraction. Higher values give a more accurate ratio but reduce maximum buffer
//     size.
#ifndef BLIP_BUFFER_ACCURACY
#define BLIP_BUFFER_ACCURACY 16
#endif

// RU: Число бит смещения фазы. Менее 6 бит (64 сдвига фазы) приводит к заметному широкополосному
//     шуму при синтезе высокочастотных прямоугольных волн. Влияет на размер объектов BlipSynth,
//     поскольку они напрямую хранят форму сигнала.
// EN: Number bits in phase offset. Fewer than 6 bits (64 phase offsets) results in noticeable
//     broadband noise when synthesizing high frequency square waves. Affects size of BlipSynth
//     objects since they store the waveform directly.
#ifndef BLIP_PHASE_BITS
#define BLIP_PHASE_BITS 8
#endif

// Internal
typedef uint32_t blip_resampled_time_t;
typedef uint32_t blip_sample_time_t;
typedef uint32_t blip_clk_time_t;
typedef uint32_t blip_ms_time_t;
static const int BLIP_WIDEST_IMPULSE = 16;
static const int BLIP_BUFFER_EXTRA = BLIP_WIDEST_IMPULSE + 2;
static const int BLIP_RES = 1 << BLIP_PHASE_BITS;
static const int BLIP_SAMPLE_BITS = 30;
// Quality level. Start with BLIP_GOOD_QUALITY.
static const int BLIP_MED_QUALITY = 8;
static const int BLIP_GOOD_QUALITY = 12;
static const int BLIP_HIGH_QUALITY = 16;

class BlipEq;

class BlipBuffer {
 public:
  BlipBuffer();
  ~BlipBuffer();
  BlipBuffer(const BlipBuffer &) = delete;
  BlipBuffer &operator=(const BlipBuffer &) = delete;

  typedef const char *blargg_err_t;

  // Set output sample rate and buffer length in milliseconds (1/1000 sec,
  // defaults to 1/4 second), then clear buffer. Returns NULL on success,
  // otherwise if there isn't enough memory, returns error without affecting
  // current buffer setup.
  blargg_err_t SetSampleRate(long rate, blip_ms_time_t ms = 1000 / 4);

  // Set number of source time units per second
  void SetClockRate(long clk_rate) { mFactor = this->ClockRateFactor(mClockRate = clk_rate); }

  // End current time frame of specified duration and make its samples
  // available (along with any still-unread samples) for reading with
  // ReadSamples(). Begins a new time frame at the end of the current frame.
  void EndFrame(blip_clk_time_t time);

  // Read at most 'max_samples' out of buffer into 'dest', removing them from
  // from the buffer. Returns number of samples actually read and removed. If
  // stereo is true, increments 'dest' one extra time after writing each
  // sample, to allow easy interleving of two channels into a stereo output
  // buffer.
  long ReadSamples(blip_sample_t *dest, long max_samples, bool stereo = false);

  // Additional optional features

  // Current output sample rate
  long GetSampleRate() const { return mSampleRate; }

  // Number of source time units per second
  long GetClockRate() const { return mClockRate; }

  // Length of buffer, in milliseconds
  blip_ms_time_t GetLength() const { return mLength; }

  // Set frequency high-pass filter frequency, where higher values reduce bass more
  void SetBassFrequency(int frequency);

  // Number of samples delay from synthesis to samples read out
  int GetOutputLatency() const { return BLIP_WIDEST_IMPULSE / 2; }

  // Remove all available samples and clear buffer to silence. If
  // 'entire_buffer' is false, just clears out any samples waiting rather than
  // the entire buffer.
  void Clear(bool entire_buffer = true);

  // Number of samples available for reading with ReadSamples()
  long SamplesAvailable() const { return (long) (mOffset >> BLIP_BUFFER_ACCURACY); }

  // Remove 'count' samples from those waiting to be read
  void RemoveSamples(long count);

  // Experimental features

  // Count number of clocks needed until 'count' samples will be available.
  // If buffer can't even hold 'count' samples, returns number of clocks until
  // buffer becomes full.
  blip_clk_time_t CountClocks(blip_sample_time_t count) const;

  // Mix 'count' samples from 'buf' into buffer.
  void MixSamples(const blip_sample_t *buf, long count);

  // not documented yet
  void SetModified() { mModified = true; }
  bool ClearModified() {
    bool b = mModified;
    mModified = false;
    return b;
  }
  void RemoveSilence(long count);
  // SampleDuration
  blip_resampled_time_t ResampledDuration(blip_clk_time_t clk_time) const { return clk_time * mFactor; }
  // Number of raw samples that can be mixed within frame of specified duration.
  blip_sample_time_t ClocksToSamples(blip_clk_time_t clk_time) const {
    return ResampledDuration(clk_time) >> BLIP_BUFFER_ACCURACY;
  }
  // Calculate number of main clocks in specified frame rate.
  blip_clk_time_t GetRateClocks(blip_clk_time_t rate) const { return (mClockRate + rate / 2) / rate; }
  // SampleTime * 65536
  blip_resampled_time_t ResampledTime(blip_clk_time_t clk_time) const { return ResampledDuration(clk_time) + mOffset; }
  // SamplesPerClock * 65536
  blip_resampled_time_t ClockRateFactor(long clock_rate) const;

 public:
  // samples per clock * 65536
  blip_resampled_time_t mFactor;
  // current_samples * 65536 in buffer
  blip_resampled_time_t mOffset;
  int32_t *mBuffer;
  blip_sample_time_t mBufferSize;
  blip_sample_time_t mReaderAccum;
  int mBassShift;

 private:
  // friend class BlipReader;
  long mSampleRate;
  long mClockRate;
  int mBassFreq;
  blip_ms_time_t mLength;
  bool mModified{false};
};

class BlipSynthImpl {
 public:
  void SetVolumeUnit(double);
  void SetTrebleEq(const BlipEq &) {}

  BlipBuffer *mBuf{nullptr};
  int mLastAmp{0};
  int32_t mDeltaFactor{0};
};

// RU: range - максимальная амплитуда сигнала. Разница между максимальной и минимальной амплитудой.
// EN: Range specifies the greatest expected change in amplitude. Calculate it by finding the difference between the
// maximum and minimum expected amplitudes (max - min).
template<int quality, int range> class BlipSynth {
 public:
  BlipSynth() {}
  BlipSynth<quality, range>(const BlipSynth<quality, range> &) = delete;
  BlipSynth<quality, range>(BlipSynth<quality, range> &&) = delete;
  BlipSynth<quality, range> &operator=(const BlipSynth<quality, range> &) = delete;

  // Set overall volume of waveform
  void SetVolume(double v) { mImpl.SetVolumeUnit(v * (1.0 / (range < 0 ? -range : range))); }

  // Configure low-pass filter (see blip_buffer.txt)
  void SetTrebleEq(const BlipEq &eq) { mImpl.SetTrebleEq(eq); }

  // Get/set BlipBuffer used for output
  BlipBuffer *GetOutput() const { return mImpl.mBuf; }
  void SetOutput(BlipBuffer *buf) {
    mImpl.mBuf = buf;
    mImpl.mLastAmp = 0;
  }

  // Update amplitude of waveform at given time. Using this requires a separate BlipSynth for each
  // waveform.
  void Update(blip_time_t tm, int amp) {
    int delta = amp - mImpl.mLastAmp;
    mImpl.mLastAmp = amp;
    this->Offset(tm, delta);
  }

  // Low-level interface

  // Add an amplitude transition of specified delta, optionally into specified
  // buffer rather than the one set with output(). Delta can be positive or
  // negative. The actual change in amplitude is delta * (volume / range)
  void Offset(BlipBuffer *dst, blip_clk_time_t clk_time, int delta) const {
    OffsetResampled(dst, dst->ResampledTime(clk_time), delta);
  }
  void Offset(blip_clk_time_t clk_time, int delta) const { Offset(mImpl.mBuf, clk_time, delta); }

  // Works directly in terms of fractional output samples. Contact author for
  // more info.
  void OffsetResampled(BlipBuffer *dst, blip_resampled_time_t time, int delta) const;

 private:
  BlipSynthImpl mImpl;
};

// Low-pass equalization parameters
class BlipEq {
 public:
  // Logarithmic rolloff to treble dB at half sampling rate. Negative values
  // reduce treble, small positive values (0 to 5.0) increase treble.
  BlipEq(double treble_db = 0) : mTreble(treble_db), mCutoffFreq(0), mRolloffFreq(0), mSampleRate(44100) {}
  // See blip_buffer.txt
  BlipEq(double treble, long rolloff_freq, long sample_rate, long cutoff_freq = 0)
      : mTreble(treble), mCutoffFreq(cutoff_freq), mRolloffFreq(rolloff_freq), mSampleRate(sample_rate) {}

 private:
  friend class BlipSynthImpl;
  void mGenerate(float *out, int count) const;
  double mTreble;
  long mCutoffFreq;
  long mRolloffFreq;
  long mSampleRate;
};

// Dummy BlipBuffer to direct sound output to, for easy muting without
// having to stop sound code.
class SilentBlipBuffer : public BlipBuffer {
 public:
  SilentBlipBuffer();
  // The following cannot be used (an assertion will fail if attempted):
  blargg_err_t SetSampleRate(long samples_per_sec, int msec_length);
  blip_time_t CountClocks(long count) const;
  void MixSamples(const blip_sample_t *buf, long count);

 private:
  int32_t mBuf[BLIP_BUFFER_EXTRA + 1];
};

// Optimized reading from BlipBuffer, for use in custom sample output

// Begin reading from buffer. Name should be unique to the current block.
#define BLIP_READER_BEGIN(name, blip_buffer) \
  const int32_t *name##ReaderBuf = (blip_buffer).mBuffer; \
  blip_long_t name##ReaderAccum = (blip_buffer).mReaderAccum

// Get value to pass to BLIP_READER_NEXT()
#define BLIP_READER_BASS(blip_buffer) ((blip_buffer).mBassShift)

// Constant value to use instead of BLIP_READER_BASS(), for slightly more
// optimal code at the cost of having no bass control
const int BLIP_READER_DEFAULT_BASS = 9;

// Current sample
#define BLIP_READER_READ(name) (name##ReaderAccum >> (BLIP_SAMPLE_BITS - 16))

// Current raw sample in full internal resolution
#define BLIP_READER_READ_RAW(name) (name##ReaderAccum)

// Advance to next sample
#define BLIP_READER_NEXT(name, bass) (void) (name##ReaderAccum += *name##ReaderBuf++ - (name##ReaderAccum >> (bass)))

// End reading samples from buffer. The number of samples read must now be
// removed using BlipBuffer::RemoveSamples().
#define BLIP_READER_END(name, blip_buffer) (void) ((blip_buffer).mReaderAccum = name##ReaderAccum)

// Compatibility with older version
// const long BLIP_UNSCALED = 65535;
// const int BLIP_LOW_QUALITY = BLIP_MED_QUALITY;
// const int BLIP_BEST_QUALITY = BLIP_HIGH_QUALITY;

// Deprecated; use BLIP_READER macros as follows:
// BlipReader r; r.begin( buf ); -> BLIP_READER_BEGIN( r, buf );
// int bass = r.begin( buf )      -> BLIP_READER_BEGIN( r, buf ); int bass =
// BLIP_READER_BASS( buf ); r.read()                       -> BLIP_READER_READ(
// r ) r.read_raw()                   -> BLIP_READER_READ_RAW( r ) r.next( bass
// )                 -> BLIP_READER_NEXT( r, bass ) r.next() ->
// BLIP_READER_NEXT( r, BLIP_READER_DEFAULT_BASS ) r.end( buf ) ->
// BLIP_READER_END( r, buf )
#if 0
class BlipReader {
  public:
    int begin(BlipBuffer &buf) {
        mBuf = buf.mBuffer;
        m_accum = buf.mReaderAccum;
        return buf.mBassShift;
    }
    blip_long_t read() const { return m_accum >> (BLIP_SAMPLE_BITS - 16); }
    blip_long_t read_raw() const { return m_accum; }
    void next(int bass_shift = 9) {
        m_accum += *mBuf++ - (m_accum >> bass_shift);
    }
    void end(BlipBuffer &b) { b.mReaderAccum = m_accum; }

  private:
    const BlipBuffer::int32_t *mBuf;
    blip_long_t m_accum;
};
#endif

// End of public interface

#include <assert.h>

template<int quality, int range>
void BlipSynth<quality, range>::OffsetResampled(BlipBuffer *dst, blip_resampled_time_t time, int delta) const {
  // Fails if time is beyond end of BlipBuffer, due to a bug in caller code
  // or the need for a longer buffer as set by SetSampleRate().
  assert((time >> BLIP_BUFFER_ACCURACY) < dst->mBufferSize);
  delta *= mImpl.mDeltaFactor;
  int32_t *buf = dst->mBuffer + (time >> BLIP_BUFFER_ACCURACY);
  int phase = (int) (time >> (BLIP_BUFFER_ACCURACY - BLIP_PHASE_BITS) & (BLIP_RES - 1));

  int32_t left = buf[0] + delta;

  // Kind of crappy, but doing shift after multiply results in overflow.
  // Alternate way of delaying multiply by mDeltaFactor results in worse
  // sub-sample resolution.
  int32_t right = (delta >> BLIP_PHASE_BITS) * phase;
  left -= right;
  right += buf[1];

  buf[0] = left;
  buf[1] = right;
}

static const int BLIP_MAX_LENGTH = 0;
static const int BLIP_DEFAULT_LENGTH = 250;
