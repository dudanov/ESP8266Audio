// TurboGrafx-16/PC Engine HES music file emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once

#include "ClassicEmu.h"
#include "HesApu.h"
#include "HesCpu.h"

namespace gme {
namespace emu {
namespace hes {

class HesEmu : private HesCpu, public ClassicEmu {
  typedef HesCpu cpu;

 public:
  static MusicEmu *createHesEmu() { return BLARGG_NEW HesEmu; }

  // HES file header
  enum { HEADER_SIZE = 0x20 };
  struct header_t {
    uint8_t tag[4];
    uint8_t vers;
    uint8_t first_track;
    uint8_t init_addr[2];
    uint8_t banks[8];
    uint8_t data_tag[4];
    uint8_t size[4];
    uint8_t addr[4];
    uint8_t unused[4];
  };

  // Header for currently loaded file
  header_t const &header() const { return header_; }

  static gme_type_t static_type() { return gme_hes_type; }

 public:
  HesEmu();
  ~HesEmu();

 protected:
  blargg_err_t mGetTrackInfo(track_info_t *, int track) const override;
  blargg_err_t mLoad(DataReader &) override;
  blargg_err_t mStartTrack(int) override;
  blargg_err_t mRunClocks(blip_clk_time_t &) override;
  void mSetTempo(double) override;
  void mSetChannel(int, BlipBuffer *, BlipBuffer *, BlipBuffer *) override;
  void mUpdateEq(BlipEq const &) override;
  void mUnload() override;

 private:
  friend class HesCpu;
  uint8_t *write_pages[page_count + 1];  // 0 if unmapped or I/O space

  int cpu_read_(hes_addr_t);
  int cpu_read(hes_addr_t);
  void cpu_write_(hes_addr_t, int data);
  void cpu_write(hes_addr_t, int);
  void cpu_write_vdp(int addr, int data);
  uint8_t const *cpu_set_mmr(int page, int bank);
  int cpu_done();

 private:
  RomData<PAGE_SIZE> rom;
  header_t header_;
  hes_time_t play_period;
  hes_time_t last_frame_hook;
  int timer_base;

  struct {
    hes_time_t last_time;
    blargg_long count;
    blargg_long load;
    int raw_load;
    uint8_t enabled;
    uint8_t fired;
  } timer;

  struct {
    hes_time_t next_vbl;
    uint8_t latch;
    uint8_t control;
  } vdp;

  struct {
    hes_time_t timer;
    hes_time_t vdp;
    uint8_t disables;
  } irq;

  void recalc_timer_load();

  // large items
  HesApu mApu;
  uint8_t sgx[3 * PAGE_SIZE + cpu_padding];

  void irq_changed();
  void mRunUntil(hes_time_t);
};

}  // namespace hes
}  // namespace emu
}  // namespace gme
