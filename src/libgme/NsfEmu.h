// Nintendo NES/Famicom NSF music file emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once

#include "ClassicEmu.h"
#include "NesApu.h"
#include "NesCpu.h"
#include <array>

namespace gme {
namespace emu {
namespace nes {

class NsfEmu : private NesCpu, public ClassicEmu {
  typedef NesCpu cpu;

 public:
  // Equalizer profiles for US NES and Japanese Famicom
  static equalizer_t const nes_eq;
  static equalizer_t const famicom_eq;
  static MusicEmu *createNsfEmu() { return BLARGG_NEW NsfEmu; }

  // NSF file header
  struct Header {
    char tag[5];
    uint8_t vers;
    uint8_t track_count;
    uint8_t first_track;
    uint8_t load_addr[2];
    uint8_t init_addr[2];
    uint8_t play_addr[2];
    char game[32];
    char author[32];
    char copyright[32];
    uint8_t ntsc_speed[2];
    uint8_t banks[8];
    uint8_t pal_speed[2];
    uint8_t speed_flags;
    uint8_t chip_flags;
    uint8_t unused[4];
  };
  static const size_t HEADER_SIZE = 128;

  // Header for currently loaded file
  const Header &header() const { return this->mHeader; }

  static gme_type_t static_type() { return gme_nsf_type; }

 public:
  NsfEmu();
  ~NsfEmu();
  // NesApu *apu_() { return &this->mApu; }

 protected:
  /* GmeFile methods */

  void mUnload() override;
  blargg_err_t mLoad(DataReader &) override;
  blargg_err_t mGetTrackInfo(track_info_t *, int track) const override;

  /* MusicEmu methods */

  blargg_err_t mStartTrack(int) override;
  void mSetTempo(double) override;

  /* ClassicEmu methods */

  blargg_err_t mRunClocks(blip_clk_time_t &) override;
  void mSetChannel(int, BlipBuffer *, BlipBuffer *, BlipBuffer *) override;
  void mUpdateEq(const BlipEq &) override;

 protected:
  static const size_t BANKS_NUM = 8;
  std::array<uint8_t, BANKS_NUM> m_initBanks;
  nes_addr_t m_initAddress;
  nes_addr_t m_playAddress;
  float m_clockRate;
  bool mPalMode;

  // timing
  NesCpu::registers_t m_savedState;
  blip_clk_time_t mNextPlay;
  blip_clk_time_t mPlayPeriod;
  int m_playExtra;
  int m_playReady;

  static const nes_addr_t ROM_BEGIN = 0x8000;
  static const nes_addr_t BANK_SELECT_ADDR = 0x5FF8;
  static const nes_addr_t BANK_SIZE = 0x1000;
  RomData<BANK_SIZE> m_rom;

 private:
  friend class NesCpu;
  uint8_t mCpuRead(nes_addr_t);
  void mCpuWrite(nes_addr_t, uint8_t);
  void mCpuWriteMisc(nes_addr_t, uint8_t);
  static const nes_addr_t BADOP_ADDR = BANK_SELECT_ADDR;

  static int pcmRead(void *, nes_addr_t);

  class NesNamcoApu *namco;
  class NesVrc6Apu *vrc6;
  class NesFme7Apu *fme7;
  // NES APU instance
  NesApu mApu;
  // Header of current opened file
  Header mHeader;

  blargg_err_t mInitSound();

  static const nes_addr_t SRAM_ADDR = 0x6000;
  std::array<uint8_t, 8192> m_sram;
  std::array<uint8_t, NesCpu::PAGE_SIZE + 8> m_unMappedCode;
};

}  // namespace nes
}  // namespace emu
}  // namespace gme
