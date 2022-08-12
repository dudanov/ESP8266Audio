// Atari XL/XE SAP music file emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once

#include "ClassicEmu.h"
#include "SapApu.h"
#include "SapCpu.h"

namespace gme {
namespace emu {
namespace sap {

class SapEmu : private SapCpu, public ClassicEmu {
  typedef SapCpu cpu;

 public:
  static gme_type_t static_type() { return gme_sap_type; }
  static MusicEmu *createSapEmu() { return BLARGG_NEW SapEmu; }

 public:
  SapEmu();
  ~SapEmu();
  struct info_t {
    uint8_t const *rom_data;
    const char *warning;
    long init_addr;
    long play_addr;
    long music_addr;
    int type;
    int track_count;
    int fastplay;
    bool stereo;
    char author[256];
    char name[256];
    char copyright[32];
  };

 protected:
  blargg_err_t mGetTrackInfo(track_info_t *, int track) const override;
  blargg_err_t mLoad(uint8_t const *, long) override;
  blargg_err_t mStartTrack(int) override;
  blargg_err_t mRunClocks(blip_time_t &, int) override;
  void mSetTempo(double) override;
  void mSetChannel(int, BlipBuffer *, BlipBuffer *, BlipBuffer *) override;
  void mUpdateEq(BlipEq const &) override;

 public:
 private:
  friend class SapCpu;
  int cpu_read(sap_addr_t);
  void cpu_write(sap_addr_t, int);
  void cpu_write_(sap_addr_t, int);

 private:
  info_t info;

  uint8_t const *file_end;
  sap_time_t scanline_period;
  sap_time_t next_play;
  sap_time_t time_mask;
  SapApu apu;
  SapApu apu2;

  // large items
  struct {
    uint8_t padding1[0x100];
    uint8_t ram[0x10000 + 0x100];
  } mem;
  SapApuImpl apu_impl;

  sap_time_t play_period() const;
  void call_play();
  void cpu_jsr(sap_addr_t);
  void call_init(int track);
  void run_routine(sap_addr_t);
};

}  // namespace sap
}  // namespace emu
}  // namespace gme
