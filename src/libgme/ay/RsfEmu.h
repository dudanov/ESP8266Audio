// Sinclair Spectrum AY music file emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once
#include "AyApu.h"
#include "ClassicEmu.h"

namespace gme {
namespace emu {
namespace ay {

class RsfEmu : public ClassicEmu {
 public:
  RsfEmu();
  ~RsfEmu();
  static MusicEmu *createRsfEmu() { return BLARGG_NEW RsfEmu; }
  static gme_type_t static_type() { return gme_rsf_type; }

  // AY file header
  enum { HEADER_SIZE = 0x14 };
  struct header_t {
    uint8_t tag[3];
    uint8_t vers;
    uint8_t framerate[2];
    uint8_t song_offset[2];
    uint8_t frames[4];
    uint8_t loop[4];
    uint8_t chip_freq[4];
    uint8_t info[0];
  };

 public:
  struct file_t {
    const header_t *header;
    const uint8_t *begin;
    const uint8_t *loop;
    const uint8_t *end;
  };

 protected:
  blargg_err_t mLoad(const uint8_t *data, long size) override;
  blargg_err_t mStartTrack(int) override;
  blargg_err_t mGetTrackInfo(track_info_t *, int track) const override;
  blargg_err_t mRunClocks(blip_clk_time_t &) override;
  void mSetTempo(double) override;
  void mSetChannel(int, BlipBuffer *, BlipBuffer *, BlipBuffer *) override;
  void mUpdateEq(BlipEq const &) override;
  void mSeekFrame(uint32_t frame);
  blargg_err_t mWriteRegisters();

 private:
  AyApu mApu;
  file_t mFile;
  const uint8_t *mIt;
  blip_clk_time_t mPlayPeriod;
  blip_clk_time_t mNextPlay;
};

}  // namespace ay
}  // namespace emu
}  // namespace gme
