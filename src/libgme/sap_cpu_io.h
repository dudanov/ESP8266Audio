#include "SapEmu.h"
#include "blargg_source.h"

namespace gme {
namespace emu {
namespace sap {

#define CPU_WRITE(cpu, addr, data, time) STATIC_CAST(SapEmu &, *cpu).cpu_write(addr, data)

void SapEmu::cpu_write(sap_addr_t addr, int data) {
  mem.ram[addr] = data;
  if ((addr >> 8) == 0xD2)
    cpu_write_(addr, data);
}

#ifdef NDEBUG
#define CPU_READ(cpu, addr, time) READ_LOW(addr)
#else
#define CPU_READ(cpu, addr, time) STATIC_CAST(SapEmu &, *cpu).cpu_read(addr)

int SapEmu::cpu_read(sap_addr_t addr) {
  if ((addr & 0xF900) == 0xD000)
    debug_printf("Unmapped read $%04X\n", addr);
  return mem.ram[addr];
}
#endif

}  // namespace sap
}  // namespace emu
}  // namespace gme
