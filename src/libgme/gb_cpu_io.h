
#include "GbsEmu.h"

#include "blargg_source.h"

namespace gme {
namespace emu {
namespace gb {

int GbsEmu::cpu_read(gb_addr_t addr) {
  int result = *cpu::get_code(addr);
  if (unsigned(addr - GbApu::START_ADDR) < GbApu::REGS_NUM)
    result = m_apu.readRegister(clock(), addr);
#ifndef NDEBUG
  else if (unsigned(addr - 0x8000) < 0x2000 || unsigned(addr - 0xE000) < 0x1F00)
    debug_printf("Read from unmapped memory $%.4x\n", (unsigned) addr);
  else if (unsigned(addr - 0xFF01) < 0xFF80 - 0xFF01)
    debug_printf("Unhandled I/O read 0x%4X\n", (unsigned) addr);
#endif
  return result;
}

void GbsEmu::cpu_write(gb_addr_t addr, int data) {
  unsigned offset = addr - RAM_ADDR;
  if (offset <= 0xFFFF - RAM_ADDR) {
    m_ram[offset] = data;
    if ((addr ^ 0xE000) <= 0x1F80 - 1) {
      if (unsigned(addr - GbApu::START_ADDR) < GbApu::REGS_NUM) {
        GME_APU_HOOK(this, addr - GbApu::START_ADDR, data);
        m_apu.writeRegister(clock(), addr, data);
      } else if ((addr ^ 0xFF06) < 2)
        m_updateTimer();
      else if (addr == JOYPAD_ADDR)
        m_ram[offset] = 0;  // keep joypad return value 0
      else
        m_ram[offset] = 0xFF;

      // if ( addr == 0xFFFF )
      //  debug_printf( "Wrote interrupt mask\n" );
    }
  } else if ((addr ^ 0x2000) <= 0x2000 - 1) {
    m_setBank(data);
  }
#ifndef NDEBUG
  else if (unsigned(addr - 0x8000) < 0x2000 || unsigned(addr - 0xE000) < 0x1F00) {
    debug_printf("Wrote to unmapped memory $%.4x\n", (unsigned) addr);
  }
#endif
}

#define CPU_READ_FAST(cpu, addr, time, out) CPU_READ_FAST_(STATIC_CAST(GbsEmu *, cpu), addr, time, out)

#define CPU_READ_FAST_(emu, addr, time, out) \
  { \
    out = READ_PROG(addr); \
    if (unsigned(addr - GbApu::START_ADDR) < GbApu::REGS_NUM) \
      out = emu->m_apu.readRegister(emu->m_cpuTime - time * CLOCKS_PER_INSTRUCTION, addr); \
    else \
      check(out == emu->cpu_read(addr)); \
  }

#define CPU_READ(cpu, addr, time) STATIC_CAST(GbsEmu *, cpu)->cpu_read(addr)

#define CPU_WRITE(cpu, addr, data, time) STATIC_CAST(GbsEmu *, cpu)->cpu_write(addr, data)

}  // namespace gb
}  // namespace emu
}  // namespace gme
