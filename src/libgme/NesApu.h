// NES 2A03 APU sound chip emulator

// Nes_Snd_Emu 0.1.8
#pragma once
#include "blargg_common.h"
#include <functional>

#include "NesCpu.h"
#include "NesOscs.h"
#include <array>

class BlipBuffer;

namespace gme {
namespace emu {
namespace nes {

using IrqNotifyFn = std::function<void(void *)>;
struct apu_state_t;

class NesApu {
 public:
  // Set buffer to generate all sound into, or disable sound if NULL
  void SetOutput(BlipBuffer *);

  // Set memory reader callback used by DMC oscillator to fetch samples.
  // When callback is invoked, 'user_data' is passed unchanged as the
  // first parameter.
  void SetDmcReader(DmcReaderFn fn, void *data) {
    mDmc.prg_reader_data = data;
    mDmc.prg_reader = fn;
  }

  // All time values are the number of CPU clock cycles relative to the
  // beginning of the current time frame. Before resetting the CPU clock
  // count, call end_frame( last_cpu_time ).

  // Write to register (0x4000-0x4017, except 0x4014 and 0x4016)
  enum { START_ADDR = 0x4000 };
  enum { END_ADDR = 0x4017 };
  void WriteRegister(nes_time_t, nes_addr_t, uint8_t data);

  // Read from status register at 0x4015
  enum { STATUS_REG = 0x4015 };
  uint8_t ReadStatus(nes_time_t);

  // Run all oscillators up to specified time, end current time frame, then
  // start a new time frame at time 0. Time frames have no effect on emulation
  // and each can be whatever length is convenient.
  void EndFrame(nes_time_t);

  // Additional optional features (can be ignored without any problem)

  // Reset internal frame counter, registers, and all oscillators.
  // Use PAL timing if pal_timing is true, otherwise use NTSC timing.
  // Set the DMC oscillator's initial DAC value to initial_dmc_dac without
  // any audible click.
  void Reset(bool pal = false, int initial_dmc_dac = 0);

  // Adjust frame period
  void SetTempo(double);

  // Save/load exact emulation state
  // void saveState(apu_state_t *out) const;
  // void loadState(apu_state_t const &);

  // Set overall volume (default is 1.0)
  void SetVolume(double);

  // Set treble equalization (see notes.txt)
  void SetTrebleEq(const BlipEq &);

  // In PAL mode
  bool IsPAL() const { return this->m_palMode; }

  // Set sound output of specific oscillator to buffer. If buffer is NULL,
  // the specified oscillator is muted and emulation accuracy is reduced.
  // The oscillators are indexed as follows: 0) Square 1, 1) Square 2,
  // 2) Triangle, 3) Noise, 4) DMC.
  enum { OSCS_NUM = 5 };
  void SetOscOutput(int osc, BlipBuffer *buf) {
    assert((unsigned) osc < OSCS_NUM);
    mOscs[osc]->setOutput(buf);
  }

  // Set IRQ time callback that is invoked when the time of earliest IRQ
  // may have changed, or NULL to disable. When callback is invoked,
  // 'user_data' is passed unchanged as the first parameter.
  void SetIrqNotifier(IrqNotifyFn fn, void *data) {
    m_irqNotifier = fn;
    m_irqData = data;
  }
  // Get time that APU-generated IRQ will occur if no further register reads
  // or writes occur. If IRQ is already pending, returns IRQ_WAITING. If no
  // IRQ will occur, returns NO_IRQ.
  enum { NO_IRQ = INT_MAX / 2 + 1 };
  enum { IRQ_WAITING = 0 };
  nes_time_t EarliestIrq(nes_time_t) const { return m_earliestIrq; }

  // Count number of DMC reads that would occur if 'RunUntil( t )' were
  // executed. If last_read is not NULL, set *last_read to the earliest time
  // that 'CountDmcReads( time )' would result in the same result.
  int CountDmcReads(nes_time_t time, nes_time_t *last_read = nullptr) const {
    return mDmc.count_reads(time, last_read);
  }

  // Time when next DMC memory read will occur
  nes_time_t NextDmcReadTime() const { return mDmc.next_read_time(); }

  // Run DMC until specified time, so that any DMC memory reads can be
  // accounted for (i.e. inserting CPU wait states).
  void RunUntil(nes_time_t);

 public:
  NesApu();
 private:
  void mEnableNonlinear(double volume);
  static double mNonlinearTndGain() { return 0.75; }

 private:
  friend struct NesDmc;
  void mIrqChanged();
  void mStateRestored();
  void mRunUntil(nes_time_t);

  // noncopyable
  NesApu(const NesApu &);
  NesApu &operator=(const NesApu &);

  NesSquare::Synth mSquareSynth;  // shared by squares
  NesSquare mSquare1{&mSquareSynth};
  NesSquare mSquare2{&mSquareSynth};
  NesTriangle mTriangle;
  NesNoise mNoise;
  NesDmc mDmc;
  std::array<NesOsc *, OSCS_NUM> mOscs{{&mSquare1, &mSquare2, &mTriangle, &mNoise, &mDmc}};

  double mTempo;
  nes_time_t mLastTime;  // has been run until this time in current frame
  nes_time_t m_lastDmcTime;
  nes_time_t m_earliestIrq;
  nes_time_t m_nextIrq;
  int m_framePeriod;
  int m_frameDelay;  // cycles until frame counter runs next
  int m_frame;       // current frame (0-3)
  int m_oscEnables;
  int m_frameMode;
  IrqNotifyFn m_irqNotifier;
  void *m_irqData;
  bool m_irqFlag;
  bool m_palMode;
};

inline nes_time_t NesDmc::next_read_time() const {
  if (lengthCounter == 0)
    return NesApu::NO_IRQ;  // not reading

  return m_apu->m_lastDmcTime + delay + long(bits_remain - 1) * period;
}

}  // namespace nes
}  // namespace emu
}  // namespace gme
