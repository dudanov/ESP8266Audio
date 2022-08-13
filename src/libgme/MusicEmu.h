// Common interface to game music file emulators

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once

#include "GmeFile.h"
class MultiBuffer;

struct MusicEmu : public GmeFile {
 public:
  // Basic functionality (see GmeFile.h for file loading/track info
  // functions)

  // Set output sample rate. Must be called only once before loading file.
  blargg_err_t SetSampleRate(long sample_rate);

  // specifies if all 8 voices get rendered to their own stereo channel
  // default implementation of Music_Emu always returns not supported error
  // (i.e. no multichannel support by default) derived emus must override this
  // if they support multichannel rendering
  virtual blargg_err_t SetMultiChannel(bool is_enabled);

  // Start a track, where 0 is the first track. Also clears warning string.
  blargg_err_t StartTrack(int);

  // Generate 'count' samples info 'buf'. Output is in stereo. Any emulation
  // errors set warning string, and major errors also end track.
  typedef int16_t sample_t;
  blargg_err_t Play(long count, sample_t *buf);

  // Informational

  // Sample rate sound is generated at
  long GetSampleRate() const { return this->mSampleRate; }

  // Index of current track or -1 if one hasn't been started
  int GetCurrentTrack() const { return this->mCurrentTrack; }

  // Number of voices used by currently loaded file
  int GetChannelsNum() const { return this->mChannelsNum; }

  // Names of voices
  const char **GetChannelsNames() const { return this->mChannelsNames; }

  bool IsMultiChannel() const { return this->mIsMultiChannel; }

  // Track status/control

  // Number of milliseconds (1000 msec = 1 second) played since beginning of
  // track
  long TellMs() const;

  // Number of samples generated since beginning of track
  long TellSamples() const;

  // Seek to new time in track. Seeking backwards or far forward can take a
  // while.
  blargg_err_t SeekMs(long ms) { return this->SeekSamples(this->mMsToSamples(ms)); }

  // Equivalent to restarting track then skipping n samples
  blargg_err_t SeekSamples(long n);

  // Skip n samples
  blargg_err_t SkipSamples(long n);

  // True if a track has reached its end
  bool IsTrackEnded() const { return this->mIsTrackEnded; }

  // Set start time and length of track fade out. Once fade ends IsTrackEnded()
  // returns true. Fade time can be changed while track is playing.
  void SetFadeMs(long start_msec, long length_msec = 8000);

  // Controls whether or not to automatically load and obey track length
  // metadata for supported emulators.
  //
  // @since 0.6.2.
  bool autoloadPlaybackLimit() const { return this->mEmuAutoloadPlaybackLimit; }
  void setAutoloadPlaybackLimit(bool do_autoload_limit) { this->mEmuAutoloadPlaybackLimit = do_autoload_limit; }

  // Disable automatic end-of-track detection and skipping of silence at
  // beginning
  void SetIgnoreSilence(bool value = true) { this->mIgnoreSilence = value; }

  // Info for current track
  using GmeFile::GetTrackInfo;
  blargg_err_t GetTrackInfo(track_info_t *out) const { return GmeFile::GetTrackInfo(out, this->mCurrentTrack); }
  // Sound customization

  // Adjust song tempo, where 1.0 = normal, 0.5 = half speed, 2.0 = double
  // speed. Track length as returned by GetTrackInfo() assumes a tempo of 1.0.
  void SetTempo(double);

  // Mute/unmute voice i, where voice 0 is first voice
  void MuteChannel(int index, bool mute = true);

  // Set muting state of all voices at once using a bit mask, where -1 mutes
  // them all, 0 unmutes them all, 0x01 mutes just the first voice, etc.
  void MuteChannels(int mask);

  // Change overall output amplitude, where 1.0 results in minimal clamping.
  // Must be called before SetSampleRate().
  void SetGain(double value) {
    assert(!this->GetSampleRate());  // you must set gain before setting sample rate
    this->mGain = value;
  }

  // Request use of custom multichannel buffer. Only supported by "classic"
  // emulators; on others this has no effect. Should be called only once
  // *before* SetSampleRate().
  virtual void SetBuffer(MultiBuffer *) {}

  // Enables/disables accurate emulation options, if any are supported. Might
  // change equalizer settings.
  void SetAccuracy(bool value = true) { this->mSetAccuracy(value); }

  // Sound equalization (treble/bass)

  // Frequency equalizer parameters (see gme.txt)
  // See gme.h for definition of struct gme_equalizer_t.
  typedef gme_equalizer_t equalizer_t;

  // Set frequency equalizer parameters
  void SetEqualizer(const equalizer_t &);
  // Current frequency equalizater parameters
  const equalizer_t &GetEqualizer() const { return this->mEqualizer; }

  // Construct equalizer of given treble/bass settings
  static equalizer_t MakeEqualizer(double treble, double bass) {
    return {treble, bass, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  }

  // Equalizer settings for TV speaker
  static const equalizer_t tv_eq;

 public:
  MusicEmu();
  ~MusicEmu();

 protected:
  void mSetMaxInitSilence(int n) { this->mMaxInitSilence = n; }
  void mSetSilenceLookahead(int n) { this->mSilenceLookahead = n; }
  void mSetChannelsNumber(int n) { this->mChannelsNum = n; }
  void mSetChannelsNames(const char *const *names) {
    // Intentional removal of const, so users don't have to remember obscure
    // const in middle
    this->mChannelsNames = const_cast<const char **>(names);
  }
  void mSetTrackEnded() { this->mEmuTrackEnded = true; }
  double mGetGain() const { return this->mGain; }
  double mGetTempo() const { return this->mTempo; }
  void mRemuteChannels() { this->MuteChannels(this->mMuteMask); }
  blargg_err_t mSetMultiChannel(bool is_enabled);

  virtual blargg_err_t mSetSampleRate(long) = 0;
  virtual void mSetEqualizer(equalizer_t const &) {}
  virtual void mSetAccuracy(bool) {}
  virtual void mMuteChannel(int) {}
  virtual void mSetTempo(double t) { this->mTempo = t; }
  virtual blargg_err_t mStartTrack(int) { return 0; }  // tempo is set before this
  virtual blargg_err_t mPlay(long, sample_t *) = 0;
  virtual blargg_err_t mSkipSamples(long);

 protected:
  virtual void mUnload();
  virtual void mPreLoad();
  virtual void mPostLoad();

 private:
  /* GENERAL */
  equalizer_t mEqualizer;
  int mMaxInitSilence;
  const char **mChannelsNames;
  int mChannelsNum;
  int mMuteMask;
  double mTempo;
  double mGain;
  bool mIsMultiChannel;

  // returns the number of output channels, i.e. usually 2 for stereo, unlesss
  // mIsMultiChannel == true
  int mGetOutputChannels() const { return this->IsMultiChannel() ? 2 * 8 : 2; }

  long mSampleRate;
  uint32_t mMsToSamples(blargg_long ms) const;

  // EN: track-specific
  int mCurrentTrack;
  // EN: number of samples played since start of track
  blargg_long mOutTime;
  // EN: number of samples emulator has generated since start of track
  blargg_long mEmuTime;
  // EN: emulator has reached end of track
  bool mEmuTrackEnded;
  // EN: whether to load and obey track length by default
  bool mEmuAutoloadPlaybackLimit;
  // EN: true if a track has reached its end
  volatile bool mIsTrackEnded;
  void mClearTrackVars();
  void mEndTrackIfError(blargg_err_t);

  /* FADING */
  blargg_long mFadeStart;
  int mFadeStep;
  void mHandleFade(long count, sample_t *out);

  /* SILENCE DETECTION */
  // EN: Disable silence detection
  bool mIgnoreSilence;
  // EN: speed to run emulator when looking ahead for silence
  int mSilenceLookahead;
  // EN: number of samples where most recent silence began
  // RU: номер последнего сэмпла с которого началась тишина
  long mSilenceTime;
  // EN: number of samples of silence to play before using buf
  // RU: количество сэмплов тишины до начала использования буфера
  long mSilenceCount;
  // EN: number of samples left in silence buffer
  // RU: количество оставшихся сэмплов тишины в буфере
  long mBufRemain;
  enum { BUF_SIZE = 2048 };
  blargg_vector<sample_t> mSamplesBuffer;
  void mFillBuf();
  void mEmuPlay(long count, sample_t *out);

  MultiBuffer *mEffectsBuffer;
  friend MusicEmu *m_gmeInternalNewEmu(gme_type_t, int, bool);
  friend void gme_set_stereo_depth(MusicEmu *, double);
};

// base class for info-only derivations
struct GmeInfo : MusicEmu {
  virtual blargg_err_t mSetSampleRate(long) override { return 0; }
  virtual void mSetEqualizer(const equalizer_t &) override { check(false); }
  virtual void mSetAccuracy(bool) override { check(false); }
  virtual void mMuteChannel(int) override { check(false); }
  virtual void mSetTempo(double) override { check(false); }
  virtual blargg_err_t mStartTrack(int) override;
  virtual blargg_err_t mPlay(long, sample_t *) override;
  virtual void mPreLoad() override { GmeFile::mPreLoad(); }    // skip MusicEmu;
  virtual void mPostLoad() override { GmeFile::mPostLoad(); }  // skip MusicEmu;
};
