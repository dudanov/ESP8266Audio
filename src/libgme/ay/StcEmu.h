#pragma once

// Sinclair Spectrum STC music file emulator

#include "AyApu.h"
#include "../ClassicEmu.h"

namespace gme {
namespace emu {
namespace ay {

class StcEmu : public ClassicEmu {
 public:
  StcEmu();
  ~StcEmu();
  static MusicEmu *createStcEmu() { return BLARGG_NEW StcEmu; }
  static gme_type_t static_type() { return gme_stc_type; }

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
