// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/

#include "FirResampler.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Copyright (C) 2004-2006 Shay Green. This module is free software; you
can redistribute it and/or modify it under the terms of the GNU Lesser
General Public License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version. This
module is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
details. You should have received a copy of the GNU Lesser General Public
License along with this module; if not, write to the Free Software Foundation,
Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA */

#include "blargg_source.h"

#undef PI
#define PI 3.1415926535897932384626433832795029

static void gen_sinc(double rolloff, int width, double offset, double spacing, double scale, int count, short *out) {
  double const maxh = 256;
  double const step = PI / maxh * spacing;
  double const to_w = maxh * 2 / width;
  double const pow_a_n = pow(rolloff, maxh);
  scale /= maxh * 2;

  double angle = (count / 2 - 1 + offset) * -step;
  while (count--) {
    *out++ = 0;
    double w = angle * to_w;
    if (fabs(w) < PI) {
      double rolloff_cos_a = rolloff * cos(angle);
      double num = 1 - rolloff_cos_a - pow_a_n * cos(maxh * angle) + pow_a_n * rolloff * cos((maxh - 1) * angle);
      double den = 1 - rolloff_cos_a - rolloff_cos_a + rolloff * rolloff;
      double sinc = scale * num / den - scale;

      out[-1] = (short) (cos(w) * sinc + sinc);
    }
    angle += step;
  }
}

void FirResamplerImpl::clear() {
  this->m_impPhase = 0;
  if (this->m_buf.size()) {
    this->m_writePtr = &this->m_buf[this->m_writeOffset];
    memset(this->m_buf.begin(), 0, this->m_writeOffset * sizeof(m_buf[0]));
  }
}

blargg_err_t FirResamplerImpl::setBufferSize(int new_size) {
  RETURN_ERR(this->m_buf.resize(new_size + this->m_writeOffset));
  this->clear();
  return 0;
}

double FirResamplerImpl::setTimeRatio(double new_factor, double rolloff, double gain) {
  this->m_ratio = new_factor;

  double fstep = 0.0;
  {
    double least_error = 2;
    double pos = 0;
    this->m_res = -1;
    for (int r = 1; r <= MAX_RES; r++) {
      pos += this->m_ratio;
      double nearest = floor(pos + 0.5);
      double error = fabs(pos - nearest);
      if (error < least_error) {
        this->m_res = r;
        fstep = nearest / m_res;
        least_error = error;
      }
    }
  }

  this->m_skipBits = 0;

  this->m_step = STEREO * (int) floor(fstep);

  this->m_ratio = fstep;
  fstep = fmod(fstep, 1.0);

  double filter = (this->m_ratio < 1.0) ? 1.0 : 1.0 / this->m_ratio;
  double pos = 0.0;
  this->m_inputPerCycle = 0;
  for (int i = 0; i < this->m_res; i++) {
    gen_sinc(rolloff, int(this->m_width * filter + 1) & ~1, pos, filter, double(0x7FFF * gain * filter), this->m_width,
             this->m_impulses + i * this->m_width);

    pos += fstep;
    this->m_inputPerCycle += this->m_step;
    if (pos >= 0.9999999) {
      pos -= 1.0;
      this->m_skipBits |= 1 << i;
      this->m_inputPerCycle++;
    }
  }

  this->clear();

  return this->m_ratio;
}

int FirResamplerImpl::input_needed(blargg_long output_count) const {
  blargg_long input_count = 0;

  unsigned long skip = this->m_skipBits >> this->m_impPhase;
  int remain = this->m_res - this->m_impPhase;
  while ((output_count -= 2) > 0) {
    input_count += this->m_step + (skip & 1) * STEREO;
    skip >>= 1;
    if (!--remain) {
      skip = this->m_skipBits;
      remain = this->m_res;
    }
    output_count -= 2;
  }

  long input_extra = input_count - (this->m_writePtr - &this->m_buf[(this->m_width - 1) * STEREO]);
  if (input_extra < 0)
    input_extra = 0;
  return input_extra;
}

int FirResamplerImpl::m_available(blargg_long input_count) const {
  int cycle_count = input_count / this->m_inputPerCycle;
  int output_count = cycle_count * this->m_res * STEREO;
  input_count -= cycle_count * this->m_inputPerCycle;

  blargg_ulong skip = this->m_skipBits >> this->m_impPhase;
  int remain = this->m_res - this->m_impPhase;
  while (input_count >= 0) {
    input_count -= this->m_step + (skip & 1) * STEREO;
    skip >>= 1;
    if (!--remain) {
      skip = this->m_skipBits;
      remain = this->m_res;
    }
    output_count += 2;
  }
  return output_count;
}

int FirResamplerImpl::skipInput(long count) {
  int remain = this->m_writePtr - this->m_buf.begin();
  int max_count = remain - this->m_width * STEREO;
  if (count > max_count)
    count = max_count;

  remain -= count;
  this->m_writePtr = &this->m_buf[remain];
  memmove(this->m_buf.begin(), &this->m_buf[count], remain * sizeof(m_buf[0]));

  return count;
}
