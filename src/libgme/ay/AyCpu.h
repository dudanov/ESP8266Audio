// Z80 CPU emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once
#include "../blargg_endian.h"

namespace gme {
namespace emu {
namespace ay {

typedef blargg_long cpu_time_t;

// must be defined by caller
void ay_cpu_out(class AyCpu *, cpu_time_t, unsigned addr, int data);
int ay_cpu_in(class AyCpu *, unsigned addr);

class AyCpu {
 public:
  // Clear all registers and keep pointer to 64K memory passed in
  void Reset(void *mem_64k);

  // Run until specified time is reached. Returns true if
  // suspicious/unsupported instruction was encountered at any point during
  // run.
  bool Run(cpu_time_t end_time);

  // Time of beginning of next instruction
  cpu_time_t Time() const { return state->time + state->base; }

  // Alter current time. Not supported during run() call.
  void SetTime(cpu_time_t t) { state->time = t - state->base; }
  void AdjustTime(int delta) { state->time += delta; }

#if BLARGG_BIG_ENDIAN
  struct regs_t {
    uint8_t b, c, d, e, h, l, flags, a;
  };
#else
  struct regs_t {
    uint8_t c, b, e, d, l, h, a, flags;
  };
#endif
  static_assert(sizeof(regs_t) == 8, "Invalid register size, padding issue?");

  struct pairs_t {
    uint16_t bc, de, hl, fa;
  };

  // Registers are not updated until run() returns
  struct registers_t {
    uint16_t pc;
    uint16_t sp;
    uint16_t ix;
    uint16_t iy;
    union {
      regs_t b;   //  b.b, b.c, b.d, b.e, b.h, b.l, b.flags, b.a
      pairs_t w;  // w.bc, w.de, w.hl. w.fa
    };
    union {
      regs_t b;
      pairs_t w;
    } alt;
    uint8_t iff1;
    uint8_t iff2;
    uint8_t r;
    uint8_t i;
    uint8_t im;
  };
  // registers_t r; (below for efficiency)

  // can read this far past end of memory
  enum { cpu_padding = 0x100 };

 public:
  AyCpu();

 private:
  uint8_t szpc[0x200];
  uint8_t *mem;
  cpu_time_t end_time_;
  struct state_t {
    cpu_time_t base;
    cpu_time_t time;
  };
  state_t *state;  // points to state_ or a local copy within run()
  state_t state_;
  void set_end_time(cpu_time_t t) {
    cpu_time_t delta = state->base - t;
    state->base = t;
    state->time += delta;
  }

 public:
  registers_t r;
};

}  // namespace ay
}  // namespace emu
}  // namespace gme
