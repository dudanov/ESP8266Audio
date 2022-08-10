#pragma once
#include "HesEmu.h"
#include "blargg_source.h"

namespace gme {
namespace emu {
namespace hes {

int HesEmu::cpu_read(hes_addr_t addr) {
  check(addr <= 0xFFFF);
  int result = *cpu::get_code(addr);
  if (mmr[addr >> PAGE_SHIFT] == 0xFF)
    result = cpu_read_(addr);
  return result;
}

void HesEmu::cpu_write(hes_addr_t addr, int data) {
  check(addr <= 0xFFFF);
  uint8_t *out = write_pages[addr >> PAGE_SHIFT];
  addr &= PAGE_SIZE - 1;
  if (out)
    out[addr] = data;
  else if (mmr[addr >> PAGE_SHIFT] == 0xFF)
    cpu_write_(addr, data);
}

inline uint8_t const *HesEmu::cpu_set_mmr(int page, int bank) {
  write_pages[page] = 0;
  if (bank < 0x80)
    return rom.atAddr(bank * (blargg_long) PAGE_SIZE);

  uint8_t *data = 0;
  switch (bank) {
    case 0xF8:
      data = cpu::ram;
      break;

    case 0xF9:
    case 0xFA:
    case 0xFB:
      data = &sgx[(bank - 0xF9) * PAGE_SIZE];
      break;

    default:
      if (bank != 0xFF)
        debug_printf("Unmapped bank $%02X\n", bank);
      return rom.unmapped();
  }

  write_pages[page] = data;
  return data;
}

#define CPU_READ_FAST(cpu, addr, time, out) CPU_READ_FAST_(STATIC_CAST(HesEmu *, cpu), addr, time, out)

#define CPU_READ_FAST_(cpu, addr, time, out) \
  { \
    out = READ_PROG(addr); \
    if (mmr[addr >> PAGE_SHIFT] == 0xFF) { \
      FLUSH_TIME(); \
      out = cpu->cpu_read_(addr); \
      CACHE_TIME(); \
    } \
  }

#define CPU_WRITE_FAST(cpu, addr, data, time) CPU_WRITE_FAST_(STATIC_CAST(HesEmu *, cpu), addr, data, time)

#define CPU_WRITE_FAST_(cpu, addr, data, time) \
  { \
    uint8_t *out = cpu->write_pages[addr >> PAGE_SHIFT]; \
    addr &= PAGE_SIZE - 1; \
    if (out) { \
      out[addr] = data; \
    } else if (mmr[addr >> PAGE_SHIFT] == 0xFF) { \
      FLUSH_TIME(); \
      cpu->cpu_write_(addr, data); \
      CACHE_TIME(); \
    } \
  }

#define CPU_READ(cpu, addr, time) STATIC_CAST(HesEmu *, cpu)->cpu_read(addr)

#define CPU_WRITE(cpu, addr, data, time) STATIC_CAST(HesEmu *, cpu)->cpu_write(addr, data)

#define CPU_WRITE_VDP(cpu, addr, data, time) STATIC_CAST(HesEmu *, cpu)->cpu_write_vdp(addr, data)

#define CPU_SET_MMR(cpu, page, bank) STATIC_CAST(HesEmu *, cpu)->cpu_set_mmr(page, bank)

#define CPU_DONE(cpu, time, result_out) result_out = STATIC_CAST(HesEmu *, cpu)->cpu_done()

}  // namespace hes
}  // namespace emu
}  // namespace gme
