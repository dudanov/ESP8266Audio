
// Use in place of Ym2413Emu.cpp and ym2413.c to disable support for this chip

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "Ym2413Emu.h"

namespace gme {
namespace emu {
namespace vgm {

Ym2413Emu::Ym2413Emu() {}

Ym2413Emu::~Ym2413Emu() {}

int Ym2413Emu::set_rate(double, double) { return 2; }

void Ym2413Emu::reset() {}

void Ym2413Emu::write(int, int) {}

void Ym2413Emu::mute_voices(int) {}

void Ym2413Emu::run(int, sample_t *) {}

}  // namespace vgm
}  // namespace emu
}  // namespace gme
