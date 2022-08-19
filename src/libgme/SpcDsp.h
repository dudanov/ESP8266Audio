// Fast SNES SPC-700 DSP emulator (about 3x speed of accurate one)

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once

#include <array>
#include <pgmspace.h>
#include "blargg_common.h"

namespace gme {
namespace emu {
namespace snes {

struct SpcDsp {
 public:
  // Setup

  // Initializes DSP and has it use the 64K RAM provided
  void init(void *ram_64k);

  // Sets destination for output samples. If out is NULL or out_size is 0,
  // doesn't generate any.
  typedef short sample_t;
  void set_output(sample_t *out, int out_size);

  // Number of samples written to output since it was last set, always
  // a multiple of 2. Undefined if more samples were generated than
  // output buffer could hold.
  int sample_count() const { return m.out - m.out_begin; }

  // Emulation

  // Resets DSP to power-on state
  void reset();

  // Emulates pressing reset switch on SNES
  void soft_reset();

  // Reads/writes DSP registers. For accuracy, you must first call
  // spc_run_dsp() to catch the DSP up to present.
  int read(int addr) const {
    assert((unsigned) addr < REGISTERS_NUM);
    return m.mRegs[addr];
  }
  void write(int addr, int data) {
    assert((unsigned) addr < REGISTERS_NUM);

    m.mRegs[addr] = (uint8_t) data;
    int low = addr & 0x0F;
    if (low < 0x2)  // voice volumes
    {
      update_voice_vol(low ^ addr);
    } else if (low == 0xC) {
      if (addr == R_KON)
        m.new_kon = (uint8_t) data;

      if (addr == R_ENDX)  // always cleared, regardless of data written
        m.mRegs[R_ENDX] = 0;
    }
  }

  // Runs DSP for specified number of clocks (~1024000 per second). Every 32
  // clocks a pair of samples is be generated.
  void run(int clock_count);

  // Sound control

  // Mutes voices corresponding to non-zero bits in mask (overrides VxVOL with
  // 0). Reduces emulation accuracy.
  enum { VOICES_NUM = 8 };
  void mute_voices(int mask);

  // If true, prevents channels and global volumes from being phase-negated
  void disableSurround(bool disable = true) { m.surround_threshold = disable ? 0 : -0x4000; }

  // State

  // Resets DSP and uses supplied values to initialize registers
  enum { REGISTERS_NUM = 128 };
  void load(const uint8_t *regs);
  void mLoad();

  // DSP register addresses

  // Global registers
  enum {
    R_MVOLL = 0x0C,
    R_MVOLR = 0x1C,
    R_EVOLL = 0x2C,
    R_EVOLR = 0x3C,
    R_KON = 0x4C,
    R_KOFF = 0x5C,
    R_FLG = 0x6C,
    R_ENDX = 0x7C,
    R_EFB = 0x0D,
    R_PMON = 0x2D,
    R_NON = 0x3D,
    R_EON = 0x4D,
    R_DIR = 0x5D,
    R_ESA = 0x6D,
    R_EDL = 0x7D,
    R_FIR = 0x0F  // 8 coefficients at 0x0F, 0x1F ... 0x7F
  };

  // Voice registers
  enum {
    V_VOLL = 0x00,
    V_VOLR = 0x01,
    V_PITCHL = 0x02,
    V_PITCHH = 0x03,
    V_SRCN = 0x04,
    V_ADSR0 = 0x05,
    V_ADSR1 = 0x06,
    V_GAIN = 0x07,
    V_ENVX = 0x08,
    V_OUTX = 0x09
  };

 public:
  enum { EXTRA_SIZE = 16 };
  sample_t *extra() { return m.extra; }
  sample_t const *out_pos() const { return m.out; }

 public:
  enum { ECHO_HIST_SIZE = 8 };

  enum env_mode_t { ENV_RELEASE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN };
  enum { BRR_BUF_SIZE = 12 };
  struct voice_t {
    int buf[BRR_BUF_SIZE * 2];  // decoded samples (twice the size to
                                // simplify wrap handling)
    int *buf_pos;               // place in buffer where next samples will be decoded
    int interp_pos;             // relative fractional position in sample (0x1000 = 1.0)
    int brr_addr;               // address of current BRR block
    int brr_offset;             // current decoding offset in BRR block
    int kon_delay;              // KON delay/current setup phase
    env_mode_t env_mode;
    int env;         // current envelope level
    int hidden_env;  // used by GAIN mode 7, very obscure quirk
    int volume[2];   // copy of volume from DSP registers, with surround
                     // disabled
    int enabled;     // -1 if enabled, 0 if muted
  };

 private:
  struct state_t {
    struct Regs : std::array<uint8_t, REGISTERS_NUM> {
      void assign(const uint8_t *src) { memcpy(this->data(), src, this->size()); }
      void assign_P(const uint8_t *src) { memcpy_P(this->data(), src, this->size()); }
    } mRegs;
#ifdef SPC_ISOLATED_ECHO_BUFFER
    // Echo buffer, for dodgy SPC rips that were only made to work in dodgy
    // emulators
    uint8_t echo_ram[64 * 1024];
#endif

    // Echo history keeps most recent 8 samples (twice the size to simplify
    // wrap handling)
    int echo_hist[ECHO_HIST_SIZE * 2][2];
    int (*echo_hist_pos)[2];  // &echo_hist [0 to 7]

    int every_other_sample;  // toggles every sample
    int kon;                 // KON value when last checked
    int noise;
    int echo_offset;  // offset from ESA in echo buffer
    int echo_length;  // number of bytes that echo_offset will stop at
    int phase;        // next clock cycle to run (0-31)
    unsigned counters[4];

    int new_kon;
    int t_koff;

    voice_t voices[VOICES_NUM];

    unsigned *counter_select[32];

    // non-emulation state
    uint8_t *ram;  // 64K shared RAM between DSP and SMP
    int mute_mask;
    int surround_threshold;
    sample_t *out;
    sample_t *out_end;
    sample_t *out_begin;
    sample_t extra[EXTRA_SIZE];
  };
  state_t m;

  void init_counter();
  void run_counter(int);
  void soft_reset_common();
  void write_outline(int addr, int data);
  void update_voice_vol(int addr) {
    int l = (int8_t) m.mRegs[addr + V_VOLL];
    int r = (int8_t) m.mRegs[addr + V_VOLR];

    if (l * r < m.surround_threshold) {
      // signs differ, so negate those that are negative
      l ^= l >> 7;
      r ^= r >> 7;
    }

    voice_t &v = m.voices[addr >> 4];
    int enabled = v.enabled;
    v.volume[0] = l & enabled;
    v.volume[1] = r & enabled;
  }
};

#define SPC_NO_COPY_STATE_FUNCS 1

#define SPC_LESS_ACCURATE 1

}  // namespace snes
}  // namespace emu
}  // namespace gme
