// MSX computer KSS music file emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once

#include "AyApu.h"
#include "ClassicEmu.h"
#include "KssCpu.h"
#include "KssSccApu.h"
#include "SmsApu.h"

namespace gme {
namespace emu {
namespace kss {

class KssEmu : private KssCpu, public ClassicEmu {
  typedef KssCpu cpu;

 public:
  // KSS file header
  enum { HEADER_SIZE = 0x10 };
  struct header_t {
    uint8_t tag[4];
    uint8_t load_addr[2];
    uint8_t load_size[2];
    uint8_t init_addr[2];
    uint8_t play_addr[2];
    uint8_t first_bank;
    uint8_t bank_mode;
    uint8_t extra_header;
    uint8_t device_flags;
  };

  enum { EXT_HEADER_SIZE = 0x10 };
  struct ext_header_t {
    uint8_t data_size[4];
    uint8_t unused[4];
    uint8_t first_track[2];
    uint8_t last_tack[2];
    uint8_t psg_vol;
    uint8_t scc_vol;
    uint8_t msx_music_vol;
    uint8_t msx_audio_vol;
  };
  static MusicEmu *createKssEmu() { return BLARGG_NEW KssEmu; }

  struct composite_header_t : header_t, ext_header_t {};

  // Header for currently loaded file
  composite_header_t const &header() const { return header_; }

  static gme_type_t static_type() { return gme_kss_type; }

 public:
  KssEmu();
  ~KssEmu();

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
  RomData<PAGE_SIZE> m_rom;
  composite_header_t header_;

  bool scc_accessed;
  bool gain_updated;
  void update_gain();

  unsigned scc_enabled;  // 0 or 0xC000
  int bank_count;
  void set_bank(int logical, int physical);
  blargg_long bank_size() const { return (16 * 1024L) >> (header_.bank_mode >> 7 & 1); }

  blip_clk_time_t play_period;
  blip_clk_time_t mNextPlay;
  int ay_latch;

  friend void kss_cpu_out(class KssCpu *, cpu_time_t, unsigned addr, int data);
  friend int kss_cpu_in(class KssCpu *, cpu_time_t, unsigned addr);
  void cpu_write(unsigned addr, int data);
  friend void kss_cpu_write(class KssCpu *, unsigned addr, int data);

  // large items
  enum { mem_size = 0x10000 };
  uint8_t ram[mem_size + cpu_padding];

  ay::AyApu ay;
  SccApu scc;
  sms::SmsApu *sn;
  uint8_t unmapped_read[0x100];
  uint8_t unmapped_write[PAGE_SIZE];
};

}  // namespace kss
}  // namespace emu
}  // namespace gme
