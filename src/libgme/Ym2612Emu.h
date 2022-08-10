// YM2612 FM sound chip emulator interface

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#ifdef VGM_YM2612_GENS  // LGPL v2.1+ license
#include "Ym2612Gens.h"
typedef gme::emu::vgm::Ym2612GensEmu Ym2612Emu;
#endif

#ifdef VGM_YM2612_NUKED  // LGPL v2.1+ license
#include "Ym2612Nuked.h"
typedef gme::emu::vgm::Ym2612NukedEmu Ym2612Emu;
#endif

#ifdef VGM_YM2612_MAME  // GPL v2+ license
#include "Ym2612Mame.h"
typedef gme::emu::vgm::Ym2612MameEmu Ym2612Emu;
#endif
