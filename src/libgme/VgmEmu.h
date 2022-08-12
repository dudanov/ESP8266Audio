// Sega Master System/Mark III, Sega Genesis/Mega Drive, BBC Micro VGM music
// file emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once

#include "VgmEmuImpl.h"

// Emulates VGM music using SN76489/SN76496 PSG, YM2612, and YM2413 FM sound
// chips. Supports custom sound buffer and frequency equalization when VGM uses
// just the PSG. FM sound chips can be run at their proper rates, or slightly
// higher to reduce aliasing on high notes. Currently YM2413 support requires
// that you supply a YM2413 sound chip emulator. I can provide one I've modified
// to work with the library.

namespace gme {
namespace emu {
namespace vgm {

class VgmEmu : public VgmEmuImpl {
 public:
  // True if custom buffer and custom equalization are supported
  // TODO: move into MusicEmu and rename to something like
  // supports_custom_buffer()
  bool is_classic_emu() const { return !uses_fm; }
  static MusicEmu *createVgmEmu() { return BLARGG_NEW VgmEmu; }

  blargg_err_t setMultiChannel(bool is_enabled) override;

  // Disable running FM chips at higher than normal rate. Will result in
  // slightly more aliasing of high notes.
  void disable_oversampling(bool disable = true) { disable_oversampling_ = disable; }

  // VGM header format
  enum { HEADER_SIZE = 0x40 };
  struct header_t {
    char tag[4];
    uint8_t data_size[4];
    uint8_t version[4];
    uint8_t psg_rate[4];
    uint8_t ym2413_rate[4];
    uint8_t gd3_offset[4];
    uint8_t track_duration[4];
    uint8_t loop_offset[4];
    uint8_t loop_duration[4];
    uint8_t frame_rate[4];
    uint8_t noise_feedback[2];
    uint8_t noise_width;
    uint8_t unused1;
    uint8_t ym2612_rate[4];
    uint8_t ym2151_rate[4];
    uint8_t data_offset[4];
    uint8_t unused2[8];
  };

  // Header for currently loaded file
  header_t const &header() const { return *(header_t const *) data; }

  static gme_type_t static_type() { return gme_vgm_type; }

 public:
  // deprecated
  using MusicEmu::load;
  blargg_err_t load(header_t const &h,
                    DataReader &in)  // use RemainingReader
  {
    return m_loadRemaining(&h, sizeof h, in);
  }
  uint8_t const *gd3_data(int *size_out = 0) const;  // use getTrackInfo()

 public:
  VgmEmu();
  ~VgmEmu();

 protected:
  blargg_err_t mGetTrackInfo(track_info_t *, int track) const override;
  blargg_err_t mLoad(uint8_t const *, long) override;
  blargg_err_t mSetSampleRate(long sample_rate) override;
  blargg_err_t mStartTrack(int) override;
  blargg_err_t mPlay(long count, sample_t *) override;
  blargg_err_t mRunClocks(blip_time_t &, int) override;
  void mSetTempo(double) override;
  void mMuteChannel(int mask) override;
  void mSetChannel(int, BlipBuffer *, BlipBuffer *, BlipBuffer *) override;
  void mUpdateEq(BlipEq const &) override;

 private:
  // removed; use disable_oversampling() and set_tempo() instead
  VgmEmu(bool oversample, double tempo = 1.0);
  double m_fmRate;
  long m_psgRate;
  long m_vgmRate;
  bool disable_oversampling_;
  bool uses_fm;
  blargg_err_t setup_fm();
};

}  // namespace vgm
}  // namespace emu
}  // namespace gme
