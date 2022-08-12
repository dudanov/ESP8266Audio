// Common aspects of emulators which use BlipBuffer for sound output

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#pragma once
#include "BlipBuffer.h"
#include "MusicEmu.h"
#include "blargg_common.h"

class ClassicEmu : public MusicEmu {
 public:
  ClassicEmu();
  ~ClassicEmu();
  void setBuffer(MultiBuffer *pbuf) {
    assert(this->mBuf == nullptr && pbuf != nullptr);
    this->mBuf = pbuf;
  }
  blargg_err_t setMultiChannel(bool is_enabled) override;

 protected:
  // Services
  enum { WAVE_TYPE = 0x100, NOISE_TYPE = 0x200, MIXED_TYPE = WAVE_TYPE | NOISE_TYPE };
  void mSetChannelsTypes(const int *t) { this->mChannelTypes = t; }
  int mGetChannelType(int idx) const { return (this->mChannelTypes != nullptr) ? this->mChannelTypes[idx] : 0; }
  long mGetClockRate() const { return this->mClockRate; }
  void mChangeClockRate(long);  // experimental
  blargg_err_t mSetupBuffer(long clock_rate);

  // Overridable
  virtual void mSetChannel(int index, BlipBuffer *center, BlipBuffer *left, BlipBuffer *right) = 0;
  virtual void mUpdateEq(BlipEq const &) = 0;
  virtual blargg_err_t mStartTrack(int track) override;
  virtual blargg_err_t mRunClocks(blip_time_t &time_io, int msec) = 0;

 protected:
  blargg_err_t mSetSampleRate(long sample_rate) override;
  void mMuteChannel(int) override;
  void mSetEqualizer(equalizer_t const &) override;
  blargg_err_t mPlay(long, sample_t *) override;

 private:
  MultiBuffer *mBuf;
  MultiBuffer *mStereoBuf;  // NULL if using custom buffer
  long mClockRate;
  unsigned mBufChangedNum;
  const int *mChannelTypes;
};

// ROM data handler, used by several ClassicEmu derivitives. Loads file data
// with padding on both sides, allowing direct use in bank mapping. The main
// purpose is to allow all file data to be loaded with only one read() call (for
// efficiency).

class RomDataImpl {
 protected:
  enum { PAD_EXTRA = 8 };
  blargg_vector<uint8_t> rom;
  long m_fileSize;
  blargg_long m_romAddr;
  blargg_long m_mask;
  blargg_long m_size;  // TODO: eliminate
  blargg_err_t m_loadRomData(DataReader &in, int header_size, void *header_out, int fill, long pad_size);
  void m_setAddr(long addr, int unit);
};

template<int unit> class RomData : public RomDataImpl {
  enum { PAD_SIZE = unit + PAD_EXTRA };

 public:
  // Load file data, using already-loaded header 'h' if not NULL. Copy header
  // from loaded file data into *out and fill unmapped bytes with 'fill'.
  blargg_err_t load(DataReader &in, int header_size, void *header_out, int fill) {
    return this->m_loadRomData(in, header_size, header_out, fill, PAD_SIZE);
  }

  // Size of file data read in (excluding header)
  long fileSize() const { return this->m_fileSize; }

  // Pointer to beginning of file data
  uint8_t *begin() const { return this->rom.begin() + PAD_SIZE; }

  // Set address that file data should start at
  void setAddr(long addr) { this->m_setAddr(addr, unit); }

  // Free data
  void clear() { this->rom.clear(); }

  // Size of data + start addr, rounded to a multiple of unit
  long size() const { return this->m_size; }

  // Pointer to unmapped page filled with same value
  uint8_t *unmapped() { return this->rom.begin(); }

  // Mask address to nearest power of two greater than size()
  blargg_long maskAddr(blargg_long addr) const {
#ifdef check
    check(addr <= this->m_mask);
#endif
    return addr & this->m_mask;
  }

  // Pointer to page starting at addr. Returns unmapped() if outside data.
  uint8_t *atAddr(blargg_long addr) {
    blargg_ulong offset = this->maskAddr(addr) - this->m_romAddr;
    if (offset > blargg_ulong(this->rom.size() - PAD_SIZE))
      offset = 0;  // unmapped
    return &this->rom[offset];
  }
};

#ifndef GME_APU_HOOK
#define GME_APU_HOOK(emu, addr, data) ((void) 0)
#endif

#ifndef GME_FRAME_HOOK
#define GME_FRAME_HOOK(emu) ((void) 0)
#else
#define GME_FRAME_HOOK_DEFINED 1
#endif
