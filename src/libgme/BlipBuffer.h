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
#define BLIP_BUFFER_FAST 1

// RU: Количество бит в доле передискретизации. Более высокие значения дают более точное
// соотношение, но уменьшают максимальный размер буфера. EN: Number of bits in resample ratio
// fraction. Higher values give a more accurate ratio but reduce maximum buffer
//     size.
#ifndef BLIP_BUFFER_ACCURACY
#define BLIP_BUFFER_ACCURACY 16
#endif

// EN: Number bits in phase offset. Fewer than 6 bits (64 phase offsets) results in noticeable
// broadband noise when
//     synthesizing high frequency square waves. Affects size of BlipSynth objects since they store
//     the waveform directly.
// RU: Число бит смещения фазы. Менее 6 бит (64 сдвига фазы) приводит к заметному широкополосному
// шуму при синтезе
//     высокочастотных прямоугольных волн. Влияет на размер объектов BlipSynth, поскольку они
//     напрямую хранят форму сигнала.
#ifndef BLIP_PHASE_BITS
#if BLIP_BUFFER_FAST
#define BLIP_PHASE_BITS 8
#else
#define BLIP_PHASE_BITS 6
#endif
#endif

// Internal
typedef blip_ulong_t blip_resampled_time_t;
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
  typedef const char *blargg_err_t;

  // Set output sample rate and buffer length in milliseconds (1/1000 sec,
  // defaults to 1/4 second), then clear buffer. Returns NULL on success,
  // otherwise if there isn't enough memory, returns error without affecting
  // current buffer setup.
  blargg_err_t SetSampleRate(long samples_per_sec, int msec_length = 1000 / 4);

  // Set number of source time units per second
  void SetClockRate(long cps) { this->m_factor = this->clockRateFactor(this->m_clockRate = cps); }

  // End current time frame of specified duration and make its samples
  // available (along with any still-unread samples) for reading with
  // ReadSamples(). Begins a new time frame at the end of the current frame.
  void EndFrame(blip_time_t time);

  // Read at most 'max_samples' out of buffer into 'dest', removing them from
  // from the buffer. Returns number of samples actually read and removed. If
  // stereo is true, increments 'dest' one extra time after writing each
  // sample, to allow easy interleving of two channels into a stereo output
  // buffer.
  long ReadSamples(blip_sample_t *dest, long max_samples, int stereo = 0);

  // Additional optional features

  // Current output sample rate
  long GetSampleRate() const { return this->m_sampleRate; }

  // Length of buffer, in milliseconds
  int GetLength() const { return this->m_length; }

  // Number of source time units per second
  long GetClockRate() const { return this->m_clockRate; }

  // Set frequency high-pass filter frequency, where higher values reduce bass
  // more
  void SetBassFrequency(int frequency);

  // Number of samples delay from synthesis to samples read out
  int GetOutputLatency() const { return BLIP_WIDEST_IMPULSE / 2; }

  // Remove all available samples and clear buffer to silence. If
  // 'entire_buffer' is false, just clears out any samples waiting rather than
  // the entire buffer.
  void Clear(bool entire_buffer = true);

  // Number of samples available for reading with ReadSamples()
  long SamplesAvailable() const { return (long) (this->m_offset >> BLIP_BUFFER_ACCURACY); }

  // Remove 'count' samples from those waiting to be read
  void RemoveSamples(long count);

  // Experimental features

  // Count number of clocks needed until 'count' samples will be available.
  // If buffer can't even hold 'count' samples, returns number of clocks until
  // buffer becomes full.
  blip_time_t CountClocks(long count) const;

  // Number of raw samples that can be mixed within frame of specified
  // duration.
  long countSamples(blip_time_t duration) const;

  // Mix 'count' samples from 'buf' into buffer.
  void mixSamples(const blip_sample_t *buf, long count);

  // not documented yet
  void setModified() { this->m_modified = true; }
  bool clearModified() {
    bool b = this->m_modified;
    this->m_modified = false;
    return b;
  }
  typedef blip_ulong_t blip_resampled_time_t;
  void removeSilence(long count);
  blip_resampled_time_t resampledDuration(int t) const { return t * this->m_factor; }
  blip_resampled_time_t resampledTime(blip_time_t t) const { return t * this->m_factor + this->m_offset; }
  blip_resampled_time_t clockRateFactor(long clock_rate) const;

 public:
  BlipBuffer();
  ~BlipBuffer();

 private:
  // noncopyable
  BlipBuffer(const BlipBuffer &) = delete;
  BlipBuffer &operator=(const BlipBuffer &) = delete;

 public:
  typedef blip_time_t buf_t_;
  blip_ulong_t m_factor;
  blip_resampled_time_t m_offset;
  buf_t_ *m_buffer;
  blip_long_t m_bufferSize;
  blip_long_t m_readerAccum;
  int m_bassShift;

 private:
  // friend class BlipReader;
  long m_sampleRate;
  long m_clockRate;
  int m_bassFreq;
  int m_length;
  bool m_modified{false};
};

class BlipSynthFastImpl {
 public:
  void setVolumeUnit(double);
  void setTrebleEq(const BlipEq &) {}

  BlipBuffer *pBuf{nullptr};
  int lastAmp{0};
  int deltaFactor{0};
};

class BlipSynthImpl {
 public:
  BlipSynthImpl(short *impulses, int width) : m_impulses(impulses), m_width(width) {}
  void setVolumeUnit(double);
  void setTrebleEq(const BlipEq &);

  BlipBuffer *pBuf{nullptr};
  int lastAmp{0};
  int deltaFactor{0};

 private:
  double m_volumeUnit{0};
  blip_long_t m_kernelUnit{0};
  short *const m_impulses;
  const int m_width;
  int m_impulsesSize() const { return BLIP_RES / 2 * this->m_width + 1; }
  void adjust_impulse();
};

// RU: range: максимальное ожидаемое изменение амплитуды. Разница между максимальной и минимальной
// амплитудой. EN: Range specifies the greatest expected change in amplitude. Calculate it by
// finding the difference
//     between the maximum and minimum expected amplitudes (max - min).
template<int quality, int range> class BlipSynth {
 public:
#if BLIP_BUFFER_FAST
  BlipSynth() {}
#else
  BlipSynth() : m_impl(this->m_impulses.data(), quality) {}
#endif
  // Set overall volume of waveform
  void setVolume(double v) { this->m_impl.setVolumeUnit(v * (1.0 / (range < 0 ? -range : range))); }

  // Configure low-pass filter (see blip_buffer.txt)
  void setTrebleEq(const BlipEq &eq) { this->m_impl.setTrebleEq(eq); }

  // Get/set BlipBuffer used for output
  BlipBuffer *getOutput() const { return this->m_impl.pBuf; }
  void setOutput(BlipBuffer *buf) {
    this->m_impl.pBuf = buf;
    this->m_impl.lastAmp = 0;
  }

  // Update amplitude of waveform at given time. Using this requires a separate BlipSynth for each
  // waveform.
  void update(blip_time_t tm, int amp) {
    int delta = amp - this->m_impl.lastAmp;
    this->m_impl.lastAmp = amp;
    this->offset(tm, delta);
  }

  // Low-level interface

  // Add an amplitude transition of specified delta, optionally into specified
  // buffer rather than the one set with output(). Delta can be positive or
  // negative. The actual change in amplitude is delta * (volume / range)
  void offset(blip_time_t t, int delta, BlipBuffer *buf) const {
    offsetResampled(t * buf->m_factor + buf->m_offset, delta, buf);
  }
  void offset(blip_time_t t, int delta) const { this->offset(t, delta, this->m_impl.pBuf); }

  // Works directly in terms of fractional output samples. Contact author for
  // more info.
  void offsetResampled(blip_resampled_time_t, int delta, BlipBuffer *) const;

 private:
  // disable broken defaulted constructors, BlipSynthImpl isn't safe to move/copy
  BlipSynth<quality, range>(const BlipSynth<quality, range> &) = delete;
  BlipSynth<quality, range>(BlipSynth<quality, range> &&) = delete;
  BlipSynth<quality, range> &operator=(const BlipSynth<quality, range> &) = delete;
#if BLIP_BUFFER_FAST
  BlipSynthFastImpl m_impl;
#else
  BlipSynthImpl m_impl;
  typedef int16_t imp_t;
  std::array<imp_t, BLIP_RES *(quality / 2) + 1> m_impulses;
#endif
};

// Low-pass equalization parameters
class BlipEq {
 public:
  // Logarithmic rolloff to treble dB at half sampling rate. Negative values
  // reduce treble, small positive values (0 to 5.0) increase treble.
  BlipEq(double treble_db = 0) : m_treble(treble_db), m_rolloffFreq(0), m_sampleRate(44100), m_cutoffFreq(0) {}
  // See blip_buffer.txt
  BlipEq(double treble, long rolloff_freq, long sample_rate, long cutoff_freq = 0)
      : m_treble(treble), m_rolloffFreq(rolloff_freq), m_sampleRate(sample_rate), m_cutoffFreq(cutoff_freq) {}

 private:
  friend class BlipSynthImpl;
  void m_generate(float *out, int count) const;
  double m_treble;
  long m_rolloffFreq;
  long m_sampleRate;
  long m_cutoffFreq;
};

// Dummy BlipBuffer to direct sound output to, for easy muting without
// having to stop sound code.
class SilentBlipBuffer : public BlipBuffer {
 public:
  SilentBlipBuffer();
  // The following cannot be used (an assertion will fail if attempted):
  blargg_err_t setSampleRate(long samples_per_sec, int msec_length);
  blip_time_t CountClocks(long count) const;
  void mix_samples(const blip_sample_t *buf, long count);

 private:
  buf_t_ m_buf[BLIP_BUFFER_EXTRA + 1];
};

// Optimized reading from BlipBuffer, for use in custom sample output

// Begin reading from buffer. Name should be unique to the current block.
#define BLIP_READER_BEGIN(name, blip_buffer) \
  const BlipBuffer::buf_t_ *name##ReaderBuf = (blip_buffer).m_buffer; \
  blip_long_t name##ReaderAccum = (blip_buffer).m_readerAccum

// Get value to pass to BLIP_READER_NEXT()
#define BLIP_READER_BASS(blip_buffer) ((blip_buffer).m_bassShift)

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
#define BLIP_READER_END(name, blip_buffer) (void) ((blip_buffer).m_readerAccum = name##ReaderAccum)

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
        this->m_buf = buf.m_buffer;
        this->m_accum = buf.m_readerAccum;
        return buf.m_bassShift;
    }
    blip_long_t read() const { return this->m_accum >> (BLIP_SAMPLE_BITS - 16); }
    blip_long_t read_raw() const { return this->m_accum; }
    void next(int bass_shift = 9) {
        this->m_accum += *this->m_buf++ - (this->m_accum >> bass_shift);
    }
    void end(BlipBuffer &b) { b.m_readerAccum = this->m_accum; }

  private:
    const BlipBuffer::buf_t_ *m_buf;
    blip_long_t m_accum;
};
#endif

// End of public interface

#include <assert.h>

template<int quality, int range>
void BlipSynth<quality, range>::offsetResampled(blip_resampled_time_t time, int delta, BlipBuffer *blip_buf) const {
  // Fails if time is beyond end of BlipBuffer, due to a bug in caller code
  // or the need for a longer buffer as set by setSampleRate().
  assert((blip_long_t)(time >> BLIP_BUFFER_ACCURACY) < blip_buf->m_bufferSize);
  delta *= this->m_impl.deltaFactor;
  blip_long_t *buf = blip_buf->m_buffer + (time >> BLIP_BUFFER_ACCURACY);
  int phase = (int) (time >> (BLIP_BUFFER_ACCURACY - BLIP_PHASE_BITS) & (BLIP_RES - 1));

#if BLIP_BUFFER_FAST
  blip_long_t left = buf[0] + delta;

  // Kind of crappy, but doing shift after multiply results in overflow.
  // Alternate way of delaying multiply by deltaFactor results in worse
  // sub-sample resolution.
  blip_long_t right = (delta >> BLIP_PHASE_BITS) * phase;
  left -= right;
  right += buf[1];

  buf[0] = left;
  buf[1] = right;
#else

  static constexpr int FWD = (BLIP_WIDEST_IMPULSE - quality) / 2;
  static constexpr int REV = FWD + quality - 2;
  static constexpr int MID = quality / 2 - 1;

  const imp_t *imp = this->m_impulses.data() + BLIP_RES - phase;

#if defined(_M_IX86) || defined(_M_IA64) || defined(__i486__) || defined(__x86_64__) || defined(__ia64__) || \
    defined(__i386__)

  // straight forward implementation resulted in better code on GCC for x86

#define ADD_IMP(out, in) buf[out] += (blip_long_t) imp[BLIP_RES * (in)] * delta

#define BLIP_FWD(i) \
  { \
    ADD_IMP(FWD + i, i); \
    ADD_IMP(FWD + 1 + i, i + 1); \
  }
#define BLIP_REV(r) \
  { \
    ADD_IMP(REV - r, r + 1); \
    ADD_IMP(REV + 1 - r, r); \
  }

  BLIP_FWD(0)
  if (quality > 8)
    BLIP_FWD(2)
  if (quality > 12)
    BLIP_FWD(4) {
      ADD_IMP(FWD + MID - 1, MID - 1);
      ADD_IMP(FWD + MID, MID);
      imp = this->m_impulses.data() + phase;
    }
  if (quality > 12)
    BLIP_REV(6)
  if (quality > 8)
    BLIP_REV(4)
  BLIP_REV(2)

  ADD_IMP(REV, 1);
  ADD_IMP(REV + 1, 0);

#else

  // for RISC processors, help compiler by reading ahead of writes

#define BLIP_FWD(i) \
  { \
    blip_long_t t0 = i0 * delta + pBuf[FWD + i]; \
    blip_long_t t1 = imp[BLIP_RES * (i + 1)] * delta + pBuf[FWD + 1 + i]; \
    i0 = imp[BLIP_RES * (i + 2)]; \
    pBuf[FWD + i] = t0; \
    pBuf[FWD + 1 + i] = t1; \
  }
#define BLIP_REV(r) \
  { \
    blip_long_t t0 = i0 * delta + pBuf[REV - r]; \
    blip_long_t t1 = imp[BLIP_RES * r] * delta + pBuf[REV + 1 - r]; \
    i0 = imp[BLIP_RES * (r - 1)]; \
    pBuf[REV - r] = t0; \
    pBuf[REV + 1 - r] = t1; \
  }

  blip_long_t i0 = *imp;
  BLIP_FWD(0)
  if (quality > 8)
    BLIP_FWD(2)
  if (quality > 12)
    BLIP_FWD(4) {
      blip_long_t t0 = i0 * delta + pBuf[FWD + MID - 1];
      blip_long_t t1 = imp[BLIP_RES * MID] * delta + pBuf[FWD + MID];
      imp = this->m_impulses.data() + phase;
      i0 = imp[BLIP_RES * MID];
      pBuf[FWD + MID - 1] = t0;
      pBuf[FWD + MID] = t1;
    }
  if (quality > 12)
    BLIP_REV(6)
  if (quality > 8)
    BLIP_REV(4)
  BLIP_REV(2)

  blip_long_t t0 = i0 * delta + pBuf[REV];
  blip_long_t t1 = *imp * delta + pBuf[REV + 1];
  pBuf[REV] = t0;
  pBuf[REV + 1] = t1;
#endif

#endif
}

#undef BLIP_FWD
#undef BLIP_REV

static const int BLIP_MAX_LENGTH = 0;
static const int BLIP_DEFAULT_LENGTH = 250;
