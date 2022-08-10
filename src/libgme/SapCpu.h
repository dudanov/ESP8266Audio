// Atari 6502 CPU emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once

#include "blargg_common.h"

namespace gme {
namespace emu {
namespace sap {

typedef blargg_long sap_time_t;  // clock cycle count
typedef unsigned sap_addr_t;     // 16-bit address
enum { future_sap_time = INT_MAX / 2 + 1 };

class SapCpu {
 public:
  // Clear all registers and keep pointer to 64K memory passed in
  void reset(void *mem_64k);

  // Run until specified time is reached. Returns true if
  // suspicious/unsupported instruction was encountered at any point during
  // run.
  bool run(sap_time_t end_time);

  // Registers are not updated until run() returns (except I flag in status)
  struct registers_t {
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t status;
    uint8_t sp;
  };
  registers_t r;

  enum { idle_addr = 0xFEFF };

  // Time of beginning of next instruction to be executed
  sap_time_t time() const { return state->time + state->base; }
  void set_time(sap_time_t t) { state->time = t - state->base; }
  void adjust_time(int delta) { state->time += delta; }

  sap_time_t irq_time() const { return m_irqTime; }
  void set_irq_time(sap_time_t);

  sap_time_t end_time() const { return end_time_; }
  void set_end_time(sap_time_t);

 public:
  SapCpu() { state = &state_; }
  enum { IRQ_INHIBIT = 0x04 };

 private:
  struct state_t {
    sap_time_t base;
    sap_time_t time;
  };
  state_t *state;  // points to state_ or a local copy within run()
  state_t state_;
  sap_time_t m_irqTime;
  sap_time_t end_time_;
  uint8_t *mem;

  inline sap_time_t update_end_time(sap_time_t end, sap_time_t irq);
};

inline sap_time_t SapCpu::update_end_time(sap_time_t t, sap_time_t irq) {
  if (irq < t && !(r.status & IRQ_INHIBIT))
    t = irq;
  sap_time_t delta = state->base - t;
  state->base = t;
  return delta;
}

inline void SapCpu::set_irq_time(sap_time_t t) { state->time += update_end_time(end_time_, (m_irqTime = t)); }

inline void SapCpu::set_end_time(sap_time_t t) { state->time += update_end_time((end_time_ = t), m_irqTime); }

}  // namespace sap
}  // namespace emu
}  // namespace gme
