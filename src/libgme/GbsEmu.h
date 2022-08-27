// Nintendo Game Boy GBS music file emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once
#include "ClassicEmu.h"
#include "GbApu.h"
#include "GbCpu.h"

namespace gme {
namespace emu {
namespace gb {

class GbsEmu : private GbCpu, public ClassicEmu {
  typedef GbCpu cpu;

 public:
  // Equalizer profiles for Game Boy Color speaker and headphones
  static equalizer_t const handheld_eq;
  static equalizer_t const headphones_eq;
  static MusicEmu *createGbsEmu() { return BLARGG_NEW GbsEmu; }

  // GBS file header
  struct Header {
    char tag[3];
    uint8_t vers;
    uint8_t track_count;
    uint8_t first_track;
    uint8_t load_addr[2];
    uint8_t init_addr[2];
    uint8_t play_addr[2];
    uint8_t stack_ptr[2];
    uint8_t timer_modulo;
    uint8_t timer_mode;
    char game[32];
    char author[32];
    char copyright[32];
  };
  enum { HEADER_SIZE = 112 };

  // Header for currently loaded file
  // Header const &header() const { return mHeader; }

  // static gme_type_t static_type() { return gme_gbs_type; }

 public:
  GbsEmu();
  ~GbsEmu();

 protected:
  blargg_err_t mGetTrackInfo(track_info_t *, int track) const override;
  blargg_err_t mLoad(DataReader &) override;
  blargg_err_t mStartTrack(int) override;
  blargg_err_t mRunClocks(blip_clk_time_t &, int) override;
  void mSetTempo(double) override;
  void mSetChannel(int, BlipBuffer *, BlipBuffer *, BlipBuffer *) override;
  void mUpdateEq(BlipEq const &) override;
  void mUnload() override;

 private:
  // rom
  enum { BANK_SIZE = 0x4000 };
  RomData<BANK_SIZE> m_rom;
  void m_setBank(int);

  // timer
  blip_clk_time_t mCpuTime;
  blip_clk_time_t mPlayPeriod;
  blip_clk_time_t mNextPlay;
  void m_updateTimer();

  Header mHeader;
  void m_cpuJsr(gb_addr_t);

 private:
  friend class GbCpu;
  blip_time_t clock() const { return this->mCpuTime - cpu::remain(); }

  enum { JOYPAD_ADDR = 0xFF00 };
  enum { RAM_ADDR = 0xA000 };
  enum { HI_PAGE = 0xFF00 - RAM_ADDR };
  std::array<uint8_t, 0x4000 + 0x2000 + GbCpu::CPU_PADDING> m_ram;
  GbApu mApu;

  int cpu_read(gb_addr_t);
  void cpu_write(gb_addr_t, int);
};

}  // namespace gb
}  // namespace emu
}  // namespace gme
