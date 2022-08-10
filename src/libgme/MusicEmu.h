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
  blargg_err_t setSampleRate(long sample_rate);

  // specifies if all 8 voices get rendered to their own stereo channel
  // default implementation of Music_Emu always returns not supported error
  // (i.e. no multichannel support by default) derived emus must override this
  // if they support multichannel rendering
  virtual blargg_err_t setMultiChannel(bool is_enabled);

  // Start a track, where 0 is the first track. Also clears warning string.
  blargg_err_t startTrack(int);

  // Generate 'count' samples info 'buf'. Output is in stereo. Any emulation
  // errors set warning string, and major errors also end track.
  typedef int16_t sample_t;
  blargg_err_t play(long count, sample_t *buf);

  // Informational

  // Sample rate sound is generated at
  long getSampleRate() const { return this->m_sampleRate; }

  // Index of current track or -1 if one hasn't been started
  int getCurrentTrack() const { return this->m_currentTrack; }

  // Number of voices used by currently loaded file
  int getChannelsNum() const { return this->m_channelsNum; }

  // Names of voices
  const char **getChannelsNames() const { return this->m_channelsNames; }

  bool getMultiChannel() const { return this->m_multiChannel; }

  // Track status/control

  // Number of milliseconds (1000 msec = 1 second) played since beginning of
  // track
  long tell() const;

  // Number of samples generated since beginning of track
  long tellSamples() const;

  // Seek to new time in track. Seeking backwards or far forward can take a
  // while.
  blargg_err_t seek(long ms) { return this->seekSamples(this->m_msToSamples(ms)); }

  // Equivalent to restarting track then skipping n samples
  blargg_err_t seekSamples(long n);

  // Skip n samples
  blargg_err_t skip(long n);

  // True if a track has reached its end
  bool isTrackEnded() const { return this->m_trackEnded; }

  // Set start time and length of track fade out. Once fade ends isTrackEnded()
  // returns true. Fade time can be changed while track is playing.
  void setFade(long start_msec, long length_msec = 8000);

  // Controls whether or not to automatically load and obey track length
  // metadata for supported emulators.
  //
  // @since 0.6.2.
  bool autoloadPlaybackLimit() const { return this->m_emuAutoloadPlaybackLimit; }
  void setAutoloadPlaybackLimit(bool do_autoload_limit) { this->m_emuAutoloadPlaybackLimit = do_autoload_limit; }

  // Disable automatic end-of-track detection and skipping of silence at
  // beginning
  void setIgnoreSilence(bool b = true) { this->m_ignoreSilence = b; }

  // Info for current track
  using GmeFile::getTrackInfo;
  blargg_err_t getTrackInfo(track_info_t *out) const { return GmeFile::getTrackInfo(out, this->m_currentTrack); }
  // Sound customization

  // Adjust song tempo, where 1.0 = normal, 0.5 = half speed, 2.0 = double
  // speed. Track length as returned by getTrackInfo() assumes a tempo of 1.0.
  void setTempo(double);

  // Mute/unmute voice i, where voice 0 is first voice
  void muteChannel(int index, bool mute = true);

  // Set muting state of all voices at once using a bit mask, where -1 mutes
  // them all, 0 unmutes them all, 0x01 mutes just the first voice, etc.
  void muteChannels(int mask);

  // Change overall output amplitude, where 1.0 results in minimal clamping.
  // Must be called before setSampleRate().
  void setGain(double g) {
    assert(!this->getSampleRate());  // you must set gain before setting sample rate
    this->m_gain = g;
  }

  // Request use of custom multichannel buffer. Only supported by "classic"
  // emulators; on others this has no effect. Should be called only once
  // *before* setSampleRate().
  virtual void setBuffer(MultiBuffer *) {}

  // Enables/disables accurate emulation options, if any are supported. Might
  // change equalizer settings.
  void setAccuracy(bool enabled = true) { this->m_setAccuracy(enabled); }

  // Sound equalization (treble/bass)

  // Frequency equalizer parameters (see gme.txt)
  // See gme.h for definition of struct gme_equalizer_t.
  typedef gme_equalizer_t equalizer_t;

  // Current frequency equalizater parameters
  equalizer_t const &equalizer() const { return this->m_equalizer; }

  // Set frequency equalizer parameters
  void set_equalizer(equalizer_t const &);

  // Construct equalizer of given treble/bass settings
  static const equalizer_t make_equalizer(double treble, double bass) {
    const MusicEmu::equalizer_t e = {treble, bass, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    return e;
  }

  // Equalizer settings for TV speaker
  static equalizer_t const tv_eq;

 public:
  MusicEmu();
  ~MusicEmu();

 protected:
  void m_setMaxInitSilence(int n) { this->m_maxInitSilence = n; }
  void m_setSilenceLookahead(int n) { this->m_silenceLookahead = n; }
  void m_setChannelsNumber(int n) { this->m_channelsNum = n; }
  void m_setChannelsNames(const char *const *names) {
    // Intentional removal of const, so users don't have to remember obscure
    // const in middle
    this->m_channelsNames = const_cast<const char **>(names);
  }
  void m_setTrackEnded() { this->m_emuTrackEnded = true; }
  double m_getGain() const { return this->m_gain; }
  double m_getTempo() const { return this->m_tempo; }
  void m_remuteChannels() { this->muteChannels(this->m_muteMask); }
  blargg_err_t m_setMultiChannel(bool is_enabled);

  virtual blargg_err_t m_setSampleRate(long) = 0;
  virtual void m_setEqualizer(equalizer_t const &) {}
  virtual void m_setAccuracy(bool) {}
  virtual void m_muteChannels(int) {}
  virtual void m_setTempo(double t) { this->m_tempo = t; }
  virtual blargg_err_t m_startTrack(int) { return 0; }  // tempo is set before this
  virtual blargg_err_t m_play(long, sample_t *) = 0;
  virtual blargg_err_t m_skip(long);

 protected:
  virtual void m_unload();
  virtual void m_preLoad();
  virtual void m_postLoad();

 private:
  /* GENERAL */
  equalizer_t m_equalizer;
  int m_maxInitSilence;
  const char **m_channelsNames;
  int m_channelsNum;
  int m_muteMask;
  double m_tempo;
  double m_gain;
  bool m_multiChannel;

  // returns the number of output channels, i.e. usually 2 for stereo, unlesss
  // m_multiChannel == true
  int m_getOutputChannels() const { return this->getMultiChannel() ? 2 * 8 : 2; }

  long m_sampleRate;
  uint32_t m_msToSamples(blargg_long ms) const;

  // EN: track-specific
  int m_currentTrack;
  // EN: number of samples played since start of track
  blargg_long m_outTime;
  // EN: number of samples emulator has generated since start of track
  blargg_long m_emuTime;
  // EN: emulator has reached end of track
  bool m_emuTrackEnded;
  // EN: whether to load and obey track length by default
  bool m_emuAutoloadPlaybackLimit;
  // EN: true if a track has reached its end
  volatile bool m_trackEnded;
  void m_clearTrackVars();
  void m_endTrackIfError(blargg_err_t);

  /* FADING */
  blargg_long m_fadeStart;
  int m_fadeStep;
  void m_handleFade(long count, sample_t *out);

  /* SILENCE DETECTION */
  // EN: Disable silence detection
  bool m_ignoreSilence;
  // EN: speed to run emulator when looking ahead for silence
  int m_silenceLookahead;
  // EN: number of samples where most recent silence began
  // RU: номер последнего сэмпла с которого началась тишина
  long m_silenceTime;
  // EN: number of samples of silence to play before using buf
  // RU: количество сэмплов тишины до начала использования буфера
  long m_silenceCount;
  // EN: number of samples left in silence buffer
  // RU: количество оставшихся сэмплов тишины в буфере
  long m_bufRemain;
  enum { BUF_SIZE = 2048 };
  blargg_vector<sample_t> m_samplesBuffer;
  void m_fillBuf();
  void m_emuPlay(long count, sample_t *out);

  MultiBuffer *m_effectsBuffer;
  friend MusicEmu *m_gmeInternalNewEmu(gme_type_t, int, bool);
  friend void gme_set_stereo_depth(MusicEmu *, double);
};

// base class for info-only derivations
struct GmeInfo : MusicEmu {
  virtual blargg_err_t m_setSampleRate(long) override { return 0; };
  virtual void m_setEqualizer(equalizer_t const &) override { check(false); };
  virtual void m_setAccuracy(bool) override { check(false); };
  virtual void m_muteChannels(int) override { check(false); };
  virtual void m_setTempo(double) override { check(false); };
  virtual blargg_err_t m_startTrack(int);
  virtual blargg_err_t m_play(long, sample_t *) override;
  virtual void m_preLoad() override { GmeFile::m_preLoad(); }    // skip MusicEmu;
  virtual void m_postLoad() override { GmeFile::m_postLoad(); }  // skip MusicEmu;
};
