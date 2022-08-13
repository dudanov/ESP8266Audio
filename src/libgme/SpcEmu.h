// Super Nintendo SPC music file emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once

#include "FirResampler.h"
#include "MusicEmu.h"
#include "SnesSpc.h"
#include "SpcFilter.h"

namespace gme {
namespace emu {
namespace snes {

class SpcEmu : public MusicEmu {
 public:
  static MusicEmu *createSpcEmu() { return BLARGG_NEW SpcEmu; }
  SpcEmu(gme_type_t);
  SpcEmu() : SpcEmu(gme_spc_type) {}
  ~SpcEmu() {}
  // The Super Nintendo hardware samples at 32kHz. Other sample rates are
  // handled by resampling the 32kHz output; emulation accuracy is not
  // affected.
  enum { NATIVE_SAMPLE_RATE = 32000 };

  // SPC file header
  enum { HEADER_SIZE = 0x100 };
  struct header_t {
    char tag[35];
    uint8_t format;
    uint8_t version;
    uint8_t pc[2];
    uint8_t a, x, y, psw, sp;
    uint8_t unused[2];
    char song[32];
    char game[32];
    char dumper[16];
    char comment[32];
    uint8_t date[11];
    uint8_t len_secs[3];
    uint8_t fade_msec[4];
    char author[32];  // sometimes first char should be skipped (see official
                      // SPC spec)
    uint8_t mute_mask;
    uint8_t emulator;
    uint8_t unused2[46];
  };

  // Prevents channels and global volumes from being phase-negated
  void disableSurround(bool b = true) { m_apu.disableSurround(b); }

 protected:
  blargg_err_t mLoad(uint8_t const *, long) override;
  blargg_err_t mGetTrackInfo(track_info_t *, int track) const override;
  blargg_err_t mSetSampleRate(long) override;
  blargg_err_t mStartTrack(int) override;
  blargg_err_t mPlay(long, sample_t *) override;
  blargg_err_t mSkipSamples(long) override;
  void mMuteChannel(int) override;
  void mSetTempo(double) override;
  void mSetAccuracy(bool) override;

  // Pointer to file data
  const uint8_t *m_fileData;
  // Size of data
  long m_fileSize;

 private:
  FirResampler<24> m_resampler;
  SpcFilter m_filter;
  SnesSpc m_apu;

  blargg_err_t m_playAndFilter(long count, sample_t out[]);
  // Header for currently loaded file
  const header_t &m_header() const { return *(const header_t *) m_fileData; }
  const uint8_t *m_trailer() const;
  long m_trailerSize() const;
};

class RsnEmu : public SpcEmu {
 public:
  static MusicEmu *createRsnEmu() { return BLARGG_NEW RsnEmu; }
  RsnEmu() : SpcEmu(gme_rsn_type) { m_isArchive = true; }
  ~RsnEmu();
  blargg_err_t loadArchive(const char *) override;
  header_t const &header(int track) const { return *(header_t const *) m_spc[track]; }

 protected:
  blargg_err_t mGetTrackInfo(track_info_t *, int) const override;
  blargg_err_t mStartTrack(int) override;

 private:
  blargg_vector<uint8_t> m_rsn;
  blargg_vector<uint8_t *> m_spc;
  uint8_t const *m_trailer(int) const;
  long m_trailerSize(int) const;
};

}  // namespace snes
}  // namespace emu
}  // namespace gme
