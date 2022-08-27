// SNES SPC-700 APU emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once

#include "SpcDsp.h"
#include "blargg_endian.h"

#include <stdint.h>

namespace gme {
namespace emu {
namespace snes {

struct SnesSpc {
 public:
  // Must be called once before using
  blargg_err_t init();

  // Sample pairs generated per second
  enum { SAMPLE_RATE = 32000 };

  // Emulator use

  // Sets IPL ROM data. Library does not include ROM data. Most SPC music
  // files don't need ROM, but a full emulator must provide this.
  enum { ROM_SIZE = 0x40 };
  void init_rom(uint8_t const rom[ROM_SIZE]);

  // Sets destination for output samples
  typedef short sample_t;
  void set_output(sample_t *out, int out_size);

  // Number of samples written to output since last set
  int sample_count() const { return (m.extra_clocks >> 5) * 2; }

  // Resets SPC to power-on state. This resets your output buffer, so you must
  // call set_output() after this.
  void reset();

  // Emulates pressing reset switch on SNES. This resets your output buffer,
  // so you must call set_output() after this.
  void soft_reset();

  // 1024000 SPC clocks per second, sample pair every 32 clocks
  typedef int time_t;
  enum { CLOCK_RATE = 1024000 };
  enum { CLOCKS_PER_SAMPLE = 32 };

  // Emulated port read/write at specified time
  enum { PORT_NUM = 4 };
  int read_port(time_t t, int port) {
    assert((unsigned) port < PORT_NUM);
    return run_until_(t)[port];
  }
  void write_port(time_t t, int port, int data) {
    assert((unsigned) port < PORT_NUM);
    run_until_(t)[0x10 + port] = data;
  }

  // Runs SPC to end_time and starts a new time frame at 0
  void end_frame(time_t end_time);

  // Sound control

  // Mutes voices corresponding to non-zero bits in mask (issues repeated KOFF
  // events). Reduces emulation accuracy.
  enum { CHANNELS_NUM = 8 };
  void mute_voices(int mask) { dsp.mute_voices(mask); }

  // If true, prevents channels and global volumes from being phase-negated.
  // Only supported by fast DSP.
  void DisableSurround(bool disable = true) { dsp.DisableSurround(disable); }

  // Sets tempo, where TEMPO_UNIT = normal, TEMPO_UNIT / 2 = half speed, etc.
  enum { TEMPO_UNIT = 0x100 };
  void set_tempo(int);

  // SPC music files

  // Loads SPC data into emulator
  enum { SPC_MIN_FILE_SIZE = 0x10180 };
  enum { SPC_FILE_SIZE = 0x10200 };
  blargg_err_t load_spc(void const *in, long size);

  // Clears echo region. Useful after loading an SPC as many have garbage in
  // echo.
  void clear_echo();

  // Plays for count samples and write samples to out. Discards samples if out
  // is NULL. Count must be a multiple of 2 since output is stereo.
  blargg_err_t Play(int count, sample_t *out);

  // Skips count samples. Several times faster than Play() when using fast
  // DSP.
  blargg_err_t SkipSamples(int count);

  // State save/load (only available with accurate DSP)

#if !SPC_NO_COPY_STATE_FUNCS
  // Saves/loads state
  enum { state_size = 67 * 1024L };  // maximum space needed when saving
  typedef SpcDsp::copy_func_t copy_func_t;
  void copy_state(unsigned char **io, copy_func_t);

  // Writes minimal header to spc_out
  static void init_header(void *spc_out);

  // Saves emulator state as SPC file data. Writes SPC_FILE_SIZE bytes to
  // spc_out. Does not set up SPC header; use init_header() for that.
  void save_spc(void *spc_out);

  // Returns true if new key-on events occurred since last check. Useful for
  // trimming silence while saving an SPC.
  bool check_kon() { return dsp.check_kon(); }
#endif

 public:
  // TODO: document
  struct regs_t {
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t psw;
    uint8_t sp;
  };
  regs_t &smp_regs() { return m.cpu_regs; }

  uint8_t *smp_ram() { return m.ram.ram; }

  void mRunUntil(time_t t) { run_until_(t); }

 public:
  // Time relative to m_spc_time. Speeds up code a bit by eliminating need to
  // constantly add m_spc_time to time from CPU. CPU uses time that ends at
  // 0 to eliminate reloading end time every instruction. It pays off.
  typedef int rel_time_t;

  struct Timer {
    rel_time_t next_time;  // time of next event
    int prescaler;
    int period;
    int divider;
    int enabled;
    int counter;
  };

  enum { REG_NUM = 0x10 };
  enum { TIMER_NUM = 3 };
  enum { EXTRA_SIZE = SpcDsp::EXTRA_SIZE };
  enum { SIGNATURE_SIZE = 35 };

 private:
  SpcDsp dsp;

#if SPC_LESS_ACCURATE
  static signed char const reg_times_[256];
  signed char reg_times[256];
#endif

  struct state_t {
    Timer timers[TIMER_NUM];

    uint8_t smp_regs[2][REG_NUM];

    regs_t cpu_regs;

    rel_time_t dsp_time;
    time_t spc_time;
    bool echo_accessed;

    int tempo;
    int skipped_kon;
    int skipped_koff;
    const char *cpu_error;

    int extra_clocks;
    sample_t *buf_begin;
    sample_t const *buf_end;
    sample_t *extra_pos;
    sample_t extra_buf[EXTRA_SIZE];

    int rom_enabled;
    uint8_t rom[ROM_SIZE];
    uint8_t hi_ram[ROM_SIZE];

    unsigned char cycle_table[256];

    struct {
      // padding to neutralize address overflow -- but this is
      // still undefined behavior! TODO: remove and instead properly
      // guard usage of emulated memory
      uint8_t padding1[0x100];
      alignas(uint16_t) uint8_t ram[0x10000 + 0x100];
    } ram;
  } m;

  enum { ROM_ADDR = 0xFFC0 };
  enum { SKIPPING_TIME = 127 };
  // Value that padding should be filled with
  enum { CPU_PAD_FILL = 0xFF };

  enum {
    R_TEST = 0x0,
    R_CONTROL = 0x1,
    R_DSPADDR = 0x2,
    R_DSPDATA = 0x3,
    R_CPUIO0 = 0x4,
    R_CPUIO1 = 0x5,
    R_CPUIO2 = 0x6,
    R_CPUIO3 = 0x7,
    R_F8 = 0x8,
    R_F9 = 0x9,
    R_T0TARGET = 0xA,
    R_T1TARGET = 0xB,
    R_T2TARGET = 0xC,
    R_T0OUT = 0xD,
    R_T1OUT = 0xE,
    R_T2OUT = 0xF
  };

  void timers_loaded();
  void enable_rom(int enable);
  void reset_buf();
  void save_extra();
  void load_regs(uint8_t const in[REG_NUM]);
  void ram_loaded();
  void regs_loaded();
  void reset_time_regs();
  void reset_common(int timer_counter_init);

  Timer *run_timer_(Timer *t, rel_time_t);
  Timer *run_timer(Timer *t, rel_time_t);
  int dsp_read(rel_time_t);
  void dsp_write(int data, rel_time_t);
  void cpu_write_smp_reg_(int data, rel_time_t, uint16_t addr);
  void cpu_write_smp_reg(int data, rel_time_t, uint16_t addr);
  void cpu_write_high(int data, uint8_t i);
  void cpu_write(int data, uint16_t addr, rel_time_t);
  int cpu_read_smp_reg(int i, rel_time_t);
  int cpu_read(uint16_t addr, rel_time_t);
  unsigned CPU_mem_bit(uint16_t pc, rel_time_t);

  bool check_echo_access(int addr);
  uint8_t *run_until_(time_t end_time);

  struct spc_file_t {
    char signature[SIGNATURE_SIZE];
    uint8_t has_id666;
    uint8_t version;
    uint8_t pcl, pch;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t psw;
    uint8_t sp;
    char text[212];
    uint8_t ram[0x10000];
    uint8_t dsp[128];
    uint8_t unused[0x40];
    uint8_t ipl_rom[0x40];
  };

  static char const signature[SIGNATURE_SIZE + 1];

  void save_regs(uint8_t out[REG_NUM]);
};

}  // namespace snes
}  // namespace emu
}  // namespace gme
