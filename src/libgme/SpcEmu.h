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
  static MusicEmu *CreateSpcEmu() { return BLARGG_NEW SpcEmu; }
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
  void DisableSurround(bool b = true) { mApu.DisableSurround(b); }

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
  const uint8_t *mFileData;
  // Size of data
  long mFileSize;

 private:
  // FirResampler<24> m_resampler;
  SpcFilter mFilter;
  SnesSpc mApu;

  blargg_err_t mPlayAndFilter(long count, sample_t out[]);
  // Header for currently loaded file
  const header_t &mHeader() const { return *(const header_t *) mFileData; }
  const uint8_t *mTrailer() const;
  long mTrailerSize() const;
};

class RsnEmu : public SpcEmu {
 public:
  static MusicEmu *CreateRsnEmu() { return BLARGG_NEW RsnEmu; }
  RsnEmu() : SpcEmu(gme_rsn_type) { mIsArchive = true; }
  ~RsnEmu();
  blargg_err_t LoadArchive(const char *) override;
  header_t const &header(int track) const { return *(header_t const *) mSpc[track]; }

 protected:
  blargg_err_t mGetTrackInfo(track_info_t *, int) const override;
  blargg_err_t mStartTrack(int) override;

 private:
  blargg_vector<uint8_t> mRsn;
  blargg_vector<uint8_t *> mSpc;
  uint8_t const *mTrailer(int) const;
  long mTrailerSize(int) const;
};

}  // namespace snes
}  // namespace emu
}  // namespace gme
