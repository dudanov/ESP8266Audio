// Konami VRC6 sound chip emulator

// Nes_Snd_Emu 0.1.8
#pragma once

#include "BlipBuffer.h"
#include "blargg_common.h"

namespace gme {
namespace emu {
namespace nes {

struct vrc6_apu_state_t;

class NesVrc6Apu {
 public:
  // See NesApu.h for reference
  void reset();
  void volume(double);
  void treble_eq(BlipEq const &);
  void output(BlipBuffer *);
  enum { OSCS_NUM = 3 };
  void osc_output(int index, BlipBuffer *);
  void end_frame(blip_time_t);
  void save_state(vrc6_apu_state_t *) const;
  void load_state(vrc6_apu_state_t const &);

  // Oscillator 0 write-only registers are at $9000-$9002
  // Oscillator 1 write-only registers are at $A000-$A002
  // Oscillator 2 write-only registers are at $B000-$B002
  enum { REGS_NUM = 3 };
  enum { BASE_ADDR = 0x9000 };
  enum { ADDR_STEP = 0x1000 };
  void write_osc(blip_time_t, int osc, int reg, int data);

 public:
  NesVrc6Apu();
 private:
  // noncopyable
  NesVrc6Apu(const NesVrc6Apu &);
  NesVrc6Apu &operator=(const NesVrc6Apu &);

  struct Vrc6Osc {
    uint8_t mRegs[3];
    BlipBuffer *output;
    int mDelay;
    int last_amp;
    int phase;
    int amp;  // only used by saw

    int period() const { return (mRegs[2] & 0x0F) * 0x100L + mRegs[1] + 1; }
  };

  Vrc6Osc oscs[OSCS_NUM];
  blip_time_t last_time;

  BlipSynth<BLIP_MED_QUALITY, 1> saw_synth;
  BlipSynth<BLIP_GOOD_QUALITY, 1> square_synth;

  void run_until(blip_time_t);
  void run_square(Vrc6Osc &osc, blip_time_t);
  void run_saw(blip_time_t);
};

struct vrc6_apu_state_t {
  uint8_t mRegs[3][3];
  uint8_t saw_amp;
  uint16_t delays[3];
  uint8_t phases[3];
  uint8_t unused;
};

inline void NesVrc6Apu::osc_output(int i, BlipBuffer *buf) {
  assert((unsigned) i < OSCS_NUM);
  oscs[i].output = buf;
}

inline void NesVrc6Apu::volume(double v) {
  double const factor = 0.0967 * 2;
  saw_synth.setVolume(factor / 31 * v);
  square_synth.setVolume(factor * 0.5 / 15 * v);
}

inline void NesVrc6Apu::treble_eq(BlipEq const &eq) {
  saw_synth.setTrebleEq(eq);
  square_synth.setTrebleEq(eq);
}

}  // namespace nes
}  // namespace emu
}  // namespace gme
