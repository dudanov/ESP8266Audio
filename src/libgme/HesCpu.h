// PC Engine CPU emulator for use with HES music files

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once
#include "blargg_common.h"

namespace gme {
namespace emu {
namespace hes {

typedef blargg_long hes_time_t;  // clock cycle count
typedef unsigned hes_addr_t;     // 16-bit address
enum { future_hes_time = INT_MAX / 2 + 1 };

class HesCpu {
 public:
  void reset();

  enum { PAGE_SIZE = 0x2000 };
  enum { PAGE_SHIFT = 13 };
  enum { page_count = 8 };
  void set_mmr(int reg, int bank);

  uint8_t const *get_code(hes_addr_t);

  uint8_t ram[PAGE_SIZE];

  // not kept updated during a call to run()
  struct registers_t {
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t status;
    uint8_t sp;
  };
  registers_t r;

  // page mapping registers
  uint8_t mmr[page_count + 1];

  // Set end_time and run CPU from current time. Returns true if any illegal
  // instructions were encountered.
  bool run(hes_time_t end_time);

  // Time of beginning of next instruction to be executed
  hes_time_t time() const { return state->time + state->base; }
  void set_time(hes_time_t t) { state->time = t - state->base; }
  void adjust_time(int delta) { state->time += delta; }

  hes_time_t irq_time() const { return m_irqTime; }
  void set_irq_time(hes_time_t);

  hes_time_t end_time() const { return end_time_; }
  void set_end_time(hes_time_t);

  void end_frame(hes_time_t);

  // Attempt to execute instruction here results in CPU advancing time to
  // lesser of irq_time() and end_time() (or end_time() if IRQs are
  // disabled)
  enum { idle_addr = 0x1FFF };

  // Can read this many bytes past end of a page
  enum { cpu_padding = 8 };

 public:
  HesCpu() { state = &state_; }
  enum { IRQ_INHIBIT = 0x04 };

 private:
  // noncopyable
  HesCpu(const HesCpu &);
  HesCpu &operator=(const HesCpu &);

  struct state_t {
    uint8_t const *code_map[page_count + 1];
    hes_time_t base;
    blargg_long time;
  };
  state_t *state;  // points to state_ or a local copy within run()
  state_t state_;
  hes_time_t m_irqTime;
  hes_time_t end_time_;

  void set_code_page(int, void const *);
  inline int update_end_time(hes_time_t end, hes_time_t irq);
};

inline uint8_t const *HesCpu::get_code(hes_addr_t addr) {
  return state->code_map[addr >> PAGE_SHIFT] + addr
#if !BLARGG_NONPORTABLE
                                                   % (unsigned) PAGE_SIZE
#endif
      ;
}

inline int HesCpu::update_end_time(hes_time_t t, hes_time_t irq) {
  if (irq < t && !(r.status & IRQ_INHIBIT))
    t = irq;
  int delta = state->base - t;
  state->base = t;
  return delta;
}

inline void HesCpu::set_irq_time(hes_time_t t) { state->time += update_end_time(end_time_, (m_irqTime = t)); }

inline void HesCpu::set_end_time(hes_time_t t) { state->time += update_end_time((end_time_ = t), m_irqTime); }

inline void HesCpu::end_frame(hes_time_t t) {
  assert(state == &state_);
  state_.base -= t;
  if (m_irqTime < future_hes_time)
    m_irqTime -= t;
  if (end_time_ < future_hes_time)
    end_time_ -= t;
}

}  // namespace hes
}  // namespace emu
}  // namespace gme
