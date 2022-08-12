// Nintendo NES/Famicom NSFE music file emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once
#include "NsfEmu.h"
#include "blargg_common.h"

namespace gme {
namespace emu {
namespace nes {

// Allows reading info from NSFE file without creating emulator
class NsfeInfo {
 public:
  blargg_err_t load(DataReader &, NsfEmu *);

  struct info_t : NsfEmu::Header {
    char game[256];
    char author[256];
    char copyright[256];
    char dumper[256];
  } info;

  void disable_playlist(bool = true);

  blargg_err_t track_info_(track_info_t *out, int track) const;

  int remap_track(int i) const;

  void unload();

  NsfeInfo();
  ~NsfeInfo();

 private:
  blargg_vector<char> track_name_data;
  blargg_vector<const char *> track_names;
  blargg_vector<unsigned char> playlist;
  blargg_vector<char[4]> track_times;
  int actual_track_count_;
  bool playlist_disabled;
};

class NsfeEmu : public NsfEmu {
 public:
  static gme_type_t static_type() { return gme_nsfe_type; }

 public:
  // deprecated
  struct header_t {
    char tag[4];
  };
  using MusicEmu::load;
  blargg_err_t load(header_t const &h,
                    DataReader &in)  // use RemainingReader
  {
    return m_loadRemaining(&h, sizeof h, in);
  }
  void disable_playlist(bool = true);  // use clear_playlist()

 public:
  NsfeEmu();
  ~NsfeEmu();
  static MusicEmu *createNsfeEmu() { return BLARGG_NEW NsfeEmu; }

 protected:
  blargg_err_t mLoad(DataReader &) override;
  blargg_err_t mGetTrackInfo(track_info_t *, int track) const override;
  blargg_err_t m_startTrack(int) override;
  void mUnload() override;
  void m_clearPlaylist() override;

 private:
  NsfeInfo info;
  bool loading;
};

}  // namespace nes
}  // namespace emu
}  // namespace gme
