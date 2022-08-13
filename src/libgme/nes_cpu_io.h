#include "NsfEmu.h"

#if !NSF_EMU_APU_ONLY
#include "NesNamcoApu.h"
#endif

#include "blargg_source.h"

namespace gme {
namespace emu {
namespace nes {

uint8_t NsfEmu::mCpuRead(nes_addr_t addr) {
  uint8_t result;

  result = this->m_lowMem[addr & 0x7FF];
  if (!(addr & 0xE000))
    goto exit;

  result = *cpu::getCode(addr);
  if (addr > 0x7FFF)
    goto exit;

  result = m_sram[addr & (sizeof m_sram - 1)];
  if (addr > 0x5FFF)
    goto exit;

  if (addr == NesApu::STATUS_REG)
    return mApu.ReadStatus(cpu::time());

#if !NSF_EMU_APU_ONLY
  if (addr == NesNamcoApu::data_reg_addr && namco)
    return namco->read_data();
#endif

  result = addr >> 8;  // simulate open bus

  if (addr != 0x2002)
    debug_printf("Read unmapped $%.4X\n", (unsigned) addr);

exit:
  return result;
}

void NsfEmu::mCpuWrite(nes_addr_t addr, uint8_t data) {
  {
    nes_addr_t offset = addr ^ SRAM_ADDR;
    if (offset < sizeof m_sram) {
      m_sram[offset] = data;
      return;
    }
  }
  {
    int temp = addr & 0x7FF;
    if (!(addr & 0xE000)) {
      m_lowMem[temp] = data;
      return;
    }
  }

  if (unsigned(addr - NesApu::START_ADDR) <= NesApu::END_ADDR - NesApu::START_ADDR) {
    GME_APU_HOOK(this, addr - NesApu::START_ADDR, data);
    mApu.WriteRegister(cpu::time(), addr, data);
    return;
  }

  unsigned bank = addr - BANK_SELECT_ADDR;
  if (bank < BANKS_NUM) {
    blargg_long offset = m_rom.maskAddr(data * (blargg_long) BANK_SIZE);
    if (offset >= m_rom.size())
      m_setWarning("Invalid bank");
    cpu::mapCode((bank + 8) * BANK_SIZE, BANK_SIZE, m_rom.atAddr(offset));
    return;
  }

  mCpuWriteMisc(addr, data);
}

#define CPU_READ(cpu, addr, time) STATIC_CAST(NsfEmu &, *cpu).mCpuRead(addr)
#define CPU_WRITE(cpu, addr, data, time) STATIC_CAST(NsfEmu &, *cpu).mCpuWrite(addr, data)

}  // namespace nes
}  // namespace emu
}  // namespace gme
