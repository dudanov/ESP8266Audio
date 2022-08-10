// Finite impulse response (FIR) resampler with adjustable FIR size

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once
#include "blargg_common.h"
#include <cstring>

class FirResamplerImpl {
 public:
  // Use FirResampler<width> (below)

  // Set input/output resampling ratio and optionally low-pass rolloff and
  // gain. Returns actual ratio used (rounded to internal precision).
  double setTimeRatio(double factor, double rolloff = 0.999, double gain = 1.0);

  // Current input/output ratio
  double getRatio() const { return this->m_ratio; }

  // Input

  typedef int16_t sample_t;

  // Resize and clear input buffer
  blargg_err_t setBufferSize(int);

  // Clear input buffer. At least two output samples will be available after
  // two input samples are written.
  void clear();

  // Number of input samples that can be written
  int getMaxWrite() const { return this->m_buf.end() - this->m_writePtr; }

  // Pointer to place to write input samples
  sample_t *buffer() { return this->m_writePtr; }

  // Notify resampler that 'count' input samples have been written
  void write(long count) {
    this->m_writePtr += count;
    assert(this->m_writePtr <= this->m_buf.end());
  }

  // Number of input samples in buffer
  int written() const { return this->m_writePtr - &this->m_buf[m_writeOffset]; }

  // Skip 'count' input samples. Returns number of samples actually skipped.
  int skipInput(long count);

  // Output

  // Number of extra input samples needed until 'count' output samples are
  // available
  int input_needed(blargg_long count) const;

  // Number of output samples available
  int available() const { return this->m_available(this->m_writePtr - &this->m_buf[this->m_width * STEREO]); }

 public:
  ~FirResamplerImpl() {}

 protected:
  FirResamplerImpl(int width, sample_t *impulses)
      : m_width(width), m_writeOffset(width * STEREO - STEREO), m_impulses(impulses) {}

  const int m_width;
  const int m_writeOffset;
  sample_t *m_impulses;

  enum { STEREO = 2 };
  enum { MAX_RES = 32 };
  blargg_vector<sample_t> m_buf;
  sample_t *m_writePtr{nullptr};
  int m_res{1};
  int m_impPhase{0};
  blargg_ulong m_skipBits{0};
  int m_step{STEREO};
  int m_inputPerCycle{1};
  double m_ratio{1.0};

  int m_available(blargg_long input_count) const;
};

// Width is number of points in FIR. Must be even and 4 or more. More points
// give better quality and rolloff effectiveness, and take longer to calculate.
template<int width> class FirResampler : public FirResamplerImpl {
  static_assert(width >= 4 && width % 2 == 0, "FIR width must be even and have 4 or more points");
  sample_t m_samplesBuffer[MAX_RES][width];

 public:
  FirResampler() : FirResamplerImpl(width, m_samplesBuffer[0]) {}

  // Read at most 'num' samples. Returns number of samples actually read.
  int read(sample_t *dst, blargg_long num) {
    sample_t *out = dst;
    const sample_t *in = this->m_buf.begin();
    sample_t *end_pos = this->m_writePtr;
    blargg_ulong skip = this->m_skipBits >> this->m_impPhase;
    sample_t const *imp = this->m_samplesBuffer[this->m_impPhase];
    int remain = this->m_res - this->m_impPhase;
    int const step = this->m_step;

    num >>= 1;

    // Resampling can add noise so don't actually do it if we've matched sample
    // rate
    const double ratio1 = this->getRatio() - 1.0;
    const bool needResample = (ratio1 >= 0 ? ratio1 : -ratio1) >= 0.00001;

    if (end_pos - in >= width * STEREO) {
      end_pos -= width * STEREO;
      do {
        if (--num < 0)
          break;
        if (!needResample) {
          out[0] = static_cast<sample_t>(in[0]);
          out[1] = static_cast<sample_t>(in[1]);
        } else {
          // accumulate in extended precision
          blargg_long l = 0;
          blargg_long r = 0;

          const sample_t *i = in;

          for (int n = width / 2; n; --n) {
            int pt0 = imp[0];
            l += pt0 * i[0];
            r += pt0 * i[1];
            int pt1 = imp[1];
            imp += 2;
            l += pt1 * i[2];
            r += pt1 * i[3];
            i += 4;
          }

          remain--;

          l >>= 15;
          r >>= 15;

          in += (skip * STEREO) & STEREO;
          skip >>= 1;

          if (!remain) {
            imp = m_samplesBuffer[0];
            skip = m_skipBits;
            remain = m_res;
          }

          out[0] = (sample_t) l;
          out[1] = (sample_t) r;
        }

        in += step;
        out += 2;
      } while (in <= end_pos);
    }

    this->m_impPhase = this->m_res - remain;

    int left = this->m_writePtr - in;
    this->m_writePtr = &this->m_buf[left];
    memmove(this->m_buf.begin(), in, left * sizeof(*in));

    return out - dst;
  }
};
