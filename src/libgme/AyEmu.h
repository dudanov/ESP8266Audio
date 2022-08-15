// Sinclair Spectrum AY music file emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once
#include "AyCpu.h"
#include "AyApu.h"
#include "ClassicEmu.h"

namespace gme {
namespace emu {
namespace ay {

class AyEmu : private AyCpu, public ClassicEmu {
  typedef AyCpu cpu;

 public:
  static MusicEmu *createAyEmu() { return BLARGG_NEW AyEmu; }

  // AY file header
  enum { HEADER_SIZE = 0x14 };
  struct header_t {
    uint8_t tag[8];
    uint8_t vers;
    uint8_t player;
    uint8_t unused[2];
    uint8_t author[2];
    uint8_t comment[2];
    uint8_t max_track;
    uint8_t first_track;
    uint8_t track_info[2];
  };

  static gme_type_t static_type() { return gme_ay_type; }

 public:
  AyEmu();
  ~AyEmu();
  struct file_t {
    header_t const *header;
    uint8_t const *end;
    uint8_t const *tracks;
  };

 protected:
  blargg_err_t mGetTrackInfo(track_info_t *, int track) const override;
  blargg_err_t mLoad(uint8_t const *, long) override;
  blargg_err_t mStartTrack(int) override;
  blargg_err_t mRunClocks(blip_time_t &, int) override;
  void mSetTempo(double) override;
  void mSetChannel(int, BlipBuffer *, BlipBuffer *, BlipBuffer *) override;
  void mUpdateEq(BlipEq const &) override;

 private:
  file_t file;

  cpu_time_t play_period;
  cpu_time_t next_play;
  BlipBuffer *beeper_output;
  int beeper_delta;
  int last_beeper;
  AyApu::Reg mApuReg;
  int cpc_latch;
  bool spectrum_mode;
  bool cpc_mode;

  // large items
  struct {
    std::array<uint8_t, 0x100> padding1;
    std::array<uint8_t, 0x10000 + 0x100> ram;
  } mem;
  AyApu apu;
  friend void ay_cpu_out(AyCpu *, cpu_time_t, unsigned addr, int data);
  void cpu_out_misc(cpu_time_t, unsigned addr, int data);
};

}  // namespace ay
}  // namespace emu
}  // namespace gme
