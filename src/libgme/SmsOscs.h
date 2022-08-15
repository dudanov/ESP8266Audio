// Private oscillators used by SmsApu

// Sms_Snd_Emu 0.1.4
#pragma once

#include "BlipBuffer.h"
#include "blargg_common.h"

namespace gme {
namespace emu {
namespace sms {

struct SmsOsc {
  SmsOsc();
  void reset();
  BlipBuffer *outputs[4];  // NULL, right, left, center
  BlipBuffer *output;
  int output_select;
  int mDelay;
  int last_amp;
  int volume;
};

struct SmsSquare : SmsOsc {
  void run(blip_time_t, blip_time_t);
  void reset();
  int period;
  int phase;
  typedef BlipSynth<BLIP_GOOD_QUALITY, 1> Synth;
  const Synth *synth;
};

struct SmsNoise : SmsOsc {
  void run(blip_time_t, blip_time_t);
  void reset();
  const int *period;
  unsigned shifter;
  unsigned feedback;
  typedef BlipSynth<BLIP_MED_QUALITY, 1> Synth;
  Synth synth;
};

}  // namespace sms
}  // namespace emu
}  // namespace gme
