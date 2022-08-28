// Common interface to game music file loading and information

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once
#include "DataReader.h"
#include "M3uPlaylist.h"
#include "blargg_common.h"
#include "gme.h"

// Error returned if file is wrong type
// extern const char gme_wrong_file_type []; // declared in gme.h

struct gme_type_t_ {
  const char *system;      /* name of system this music file type is generally for */
  int track_count;         /* non-zero for formats with a fixed number of tracks */
  int sample_rate;         /* non-zero for formats with native sample rates */
  MusicEmu *(*new_emu)();  /* Create new emulator for this type (useful in C++ only) */
  MusicEmu *(*new_info)(); /* Create new info reader for this type */

  /* internal */
  const char *extension_;
  int flags_;
};

struct track_info_t {
  long track_count;

  /* times in milliseconds; -1 if unknown */
  long length;
  long intro_length;
  long loop_length;
  long fade_length;

  /* empty string if not available */
  char system[256];
  char game[256];
  char song[256];
  char author[256];
  char copyright[256];
  char comment[256];
  char dumper[256];
};
enum { GME_MAX_FIELD = 255 };

struct GmeFile {
 public:
  GmeFile();
  virtual ~GmeFile();
  GmeFile(const GmeFile &) = delete;
  GmeFile &operator=(const GmeFile &) = delete;

  // File loading

  // Each loads game music data from a file and returns an error if
  // file is wrong type or is seriously corrupt. They also set warning
  // string for minor problems.

  // Load from file on disk
  blargg_err_t LoadFile(const char *path);

  // Load from custom data source (see DataReader.h)
  blargg_err_t Load(DataReader &);

  // Load from file already read into memory. Keeps pointer to data, so you
  // must not free it until you're done with the file.
  blargg_err_t LoadMem(void const *data, long size);

  // Load an m3u playlist. Must be done after loading main music file.
  blargg_err_t LoadM3u(const char *path);
  blargg_err_t LoadM3u(DataReader &in);

  virtual blargg_err_t LoadArchive(const char *) { return gme_wrong_file_type; }
  bool mIsArchive = false;

  // Clears any loaded m3u playlist and any internal playlist that the music
  // format supports (NSFE for example).
  void ClearPlaylist();

  // Informational

  // Type of emulator. For example if this returns gme_nsfe_type, this object
  // is an NSFE emulator, and you can cast to an NsfeEmu* if necessary.
  gme_type_t GetType() const { return mType; }

  // Most recent warning string, or NULL if none. Clears current warning after
  // returning.
  const char *GetWarning() {
    const char *s = mWarning;
    mWarning = nullptr;
    return s;
  }

  // Number of tracks or 0 if no file has been loaded
  int GetTrackCount() const { return mTrackNum; }

  // Get information for a track (length, name, author, etc.)
  // See gme.h for definition of struct track_info_t.
  blargg_err_t GetTrackInfo(track_info_t *out, int track) const;

  // User data/cleanup

  // Set/get pointer to data you want to associate with this emulator.
  // You can use this for whatever you want.
  void SetUserData(void *data) { mUserData = data; }
  void *GetUserData() const { return mUserData; }

  // Register cleanup function to be called when deleting emulator, or NULL to
  // clear it. Passes user_data to cleanup function.
  void SetUserCleanupFn(gme_user_cleanup_t func) { mUserCleanupFn = func; }

 protected:
  // Services
  void mSetTrackNum(int n) { mTrackNum = mRawTrackCount = n; }
  void mSetWarning(const char *s) { mWarning = s; }
  void mSetType(gme_type_t t) { mType = t; }
  blargg_err_t mLoadRemaining(void const *header, long header_size, DataReader &remaining);

  // Unload file. Called before loading file and if loading fails.
  virtual void mUnload();
  // Load GmeFile from DataReader
  virtual blargg_err_t mLoad(DataReader &);                    // default loads then calls mLoad()
  virtual blargg_err_t mLoad(uint8_t const *data, long size);  // use data in memory
  virtual blargg_err_t mGetTrackInfo(track_info_t *out, int track) const = 0;
  virtual void mPreLoad();
  virtual void mPostLoad();
  virtual void mClearPlaylist() {}

 public:
  blargg_err_t RemapTrack(int *track_io) const;  // need by MusicEmu
 private:
  gme_type_t mType{0};
  int mTrackNum;
  int mRawTrackCount;
  const char *mWarning;
  void *mUserData{nullptr};
  gme_user_cleanup_t mUserCleanupFn{nullptr};
  M3uPlaylist mPlaylist;
  char mPlaylistWarning[64];
  blargg_vector<uint8_t> mFileData;  // only if loaded into memory using default load

  blargg_err_t m_loadM3u(blargg_err_t);
  blargg_err_t mPostLoad(blargg_err_t err);

 public:
  // track_info field copying
  enum { MAX_FIELD = 255 };
  static void copyField(char *out, const char *in);
  static void copyField(char *out, const char *in, int len);
};

MusicEmu *gmeNew(MusicEmu *, long sample_rate);

#define GME_COPY_FIELD(in, out, name) \
  { GmeFile::copyField(out->name, in.name, sizeof in.name); }

#ifndef GME_FILE_READER
#define GME_FILE_READER StdFileReader
#elif defined(GME_FILE_READER_INCLUDE)
#include GME_FILE_READER_INCLUDE
#endif

// inline int GmeFile::error_count() const { return mWarning != 0; }
