// Nintendo Game Boy CPU emulator
// Treats every instruction as taking 4 cycles

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once
#include "blargg_common.h"
#include "blargg_endian.h"
#include <array>

namespace gme {
namespace emu {
namespace gb {

typedef uint16_t gb_addr_t;  // 16-bit CPU address

class GbCpu {
  static const int CLOCKS_PER_INSTRUCTION = 4;

 public:
  // Clear registers and map all pages to unmapped
  void reset(void *unmapped = 0);

  // Map code memory (memory accessed via the program counter). Start and size
  // must be multiple of PAGE_SIZE.
  static const unsigned PAGE_SIZE = 0x2000;
  void map_code(gb_addr_t start, unsigned size, void *code);

  uint8_t *get_code(gb_addr_t);

  // Push a byte on the stack
  void push_byte(int);

  // Game Boy Z80 registers. *Not* kept updated during a call to run().
  struct core_regs_t {
#if BLARGG_BIG_ENDIAN
    uint8_t b, c, d, e, h, l, flags, a;
#else
    uint8_t c, b, e, d, l, h, a, flags;
#endif
  };

  struct registers_t : core_regs_t {
    long pc;  // more than 16 bits to allow overflow detection
    uint16_t sp;
  } r;

  // Interrupt enable flag set by EI and cleared by DI
  // bool interrupts_enabled; // unused

  // Base address for RST vectors (normally 0)
  gb_addr_t rst_base;

  // If CPU executes opcode 0xFF at this address, it treats as illegal
  // instruction
  static const gb_addr_t IDLE_ADDR = 0xF00D;

  // Run CPU for at least 'count' cycles and return false, or return true if
  // illegal instruction is encountered.
  bool run(blargg_long count);

  // Number of clock cycles remaining for most recent run() call
  blargg_long remain() const { return m_pState->remain * CLOCKS_PER_INSTRUCTION; }

  // Can read this many bytes past end of a page
  static const int CPU_PADDING = 8;

 public:
  GbCpu() : rst_base(0) { m_pState = &m_state; }
  static const int PAGE_SHIFT = 13;
  static const int PAGE_COUNT = 0x10000 >> PAGE_SHIFT;

 private:
  GbCpu(const GbCpu &) = delete;
  GbCpu &operator=(const GbCpu &) = delete;

  struct state_t {
    std::array<uint8_t *, PAGE_COUNT + 1> code_map;
    blargg_long remain;
  };

  state_t *m_pState;  // points to state_ or a local copy within run()
  state_t m_state;

  void m_setCodePage(int i, uint8_t *p);
};

inline uint8_t *GbCpu::get_code(gb_addr_t addr) {
  return m_pState->code_map[addr >> PAGE_SHIFT] + addr
#if !BLARGG_NONPORTABLE
                                                      % (unsigned) PAGE_SIZE
#endif
      ;
}

}  // namespace gb
}  // namespace emu
}  // namespace gme
