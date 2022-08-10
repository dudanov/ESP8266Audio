// NES 6502 CPU emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once
#include "blargg_common.h"
#include <array>

namespace gme {
namespace emu {
namespace nes {

typedef int32_t nes_time_t;   // clock cycle count
typedef uint16_t nes_addr_t;  // 16-bit address
enum { future_nes_time = INT_MAX / 2 + 1 };

class NesCpu {
 public:
  // Clear registers, map low memory and its three mirrors to address 0,
  // and mirror unmapped_page in remaining memory
  void reset(const uint8_t *unmapped_page = nullptr);

  // Map code memory (memory accessed via the program counter). Start and size
  // must be multiple of PAGE_SIZE. If mirror is true, repeats code page
  // throughout address range.
  enum { PAGE_SIZE = 0x800 };
  void mapCode(nes_addr_t start, unsigned size, const uint8_t *code, bool mirror = false);

  // Access emulated memory as CPU does
  const uint8_t *getCode(nes_addr_t addr) {
    return this->m_pState->code_map[addr >> PAGE_BITS] + addr
#if !BLARGG_NONPORTABLE
                                                             % (unsigned) PAGE_SIZE
#endif
        ;
  }

  // Set end_time and run CPU from current time. Returns true if execution
  // stopped due to encountering BAD_OPCODE.
  bool run(nes_time_t end_time);

  // Time of beginning of next instruction to be executed
  nes_time_t time() const { return this->m_pState->time + this->m_pState->base; }
  void setTime(nes_time_t t) { this->m_pState->time = t - this->m_pState->base; }
  void adjustTime(int delta) { m_pState->time += delta; }

  nes_time_t getIrqTime() const { return this->m_irqTime; }
  void setIrqTime(nes_time_t t) { m_pState->time += m_updateEndTime(m_endTime, (m_irqTime = t)); }

  nes_time_t getEndTime() const { return this->m_endTime; }
  void setEndTime(nes_time_t t) { m_pState->time += m_updateEndTime((m_endTime = t), m_irqTime); }

  // Number of undefined instructions encountered and skipped
  void clearErrors() { this->m_errorsNum = 0; }
  unsigned getErrorsNum() const { return this->m_errorsNum; }

  // CPU invokes bad opcode handler if it encounters this
  enum { BAD_OPCODE = 0xF2 };

 public:
  NesCpu() { m_pState = &m_state; }
  enum { PAGE_BITS = 11 };
  enum { PAGES_NUM = 0x10000 >> PAGE_BITS };
  enum { IRQ_INHIBIT = 0x04 };

 protected:
  // NES 6502 registers. Not kept updated during a call to run().
  struct registers_t {
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t status;
    uint8_t sp;
  };
  registers_t m_regs;
  // 2KB of RAM at address 0
  std::array<uint8_t, 2048> m_lowMem;

 private:
  struct state_t {
    std::array<const uint8_t *, PAGES_NUM + 1> code_map;
    nes_time_t base;
    int time;
  };
  state_t *m_pState;  // points to m_state or a local copy within run()
  state_t m_state;
  nes_time_t m_irqTime;
  nes_time_t m_endTime;
  unsigned m_errorsNum;

  void m_setCodePage(unsigned page, const uint8_t *data) { this->m_pState->code_map[page] = data; }
  nes_time_t m_updateEndTime(nes_time_t t, nes_time_t irq) {
    if (irq < t && !(m_regs.status & IRQ_INHIBIT))
      t = irq;
    int delta = m_pState->base - t;
    m_pState->base = t;
    return delta;
  }
};

}  // namespace nes
}  // namespace emu
}  // namespace gme
