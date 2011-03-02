/*  monoscope.cpp
 *  Copyright (C) 2002 Richard Boulton <richard@tartarus.org>
 *  Copyright (C) 1998-2001 Andy Lo A Foe <andy@alsaplayer.org>
 *  Original code by Tinic Uro
 *
 *  This code is copied from Alsaplayer. The orginal code was by Tinic Uro and under
 *  the BSD license without a advertisig clause. Andy Lo A Foe then relicensed the
 *  code when he used it for Alsaplayer to GPL with Tinic's permission. Richard Boulton
 *  then took this code and made a GPL plugin out of it.
 * 
 *  7th December 2004 Christian Schaller: Richard Boulton and Andy Lo A Foe gave
 *  permission to relicense their changes under BSD license so we where able to restore the 
 *  code to Tinic's original BSD license.
 *
 * This file is under what is known as the BSD license:
 *
 * Redistribution and use in source and binary forms, with or without modification, i
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of 
 * conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list 
 * of conditions and the following disclaimer in the documentation and/or other materials 
 * provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products derived from 
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, 
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; 
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY 
 * WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "monoscope.h"

#include <string.h>
#include <stdlib.h>

static void
colors_init (guint32 * colors)
{
  int i;

  for (i = 0; i < 32; i++) {
    colors[i] = (i * 8 << 16) + (255 << 8);
    colors[i + 31] = (255 << 16) + (((31 - i) * 8) << 8);
  }
  colors[63] = (40 << 16) + (75 << 8);
}

struct monoscope_state *
monoscope_init (guint32 resx, guint32 resy)
{
  struct monoscope_state *stateptr;

  /* I didn't program monoscope to only do 256*128, but it works that way */
  g_return_val_if_fail (resx == 256, 0);
  g_return_val_if_fail (resy == 128, 0);
  stateptr = calloc (1, sizeof (struct monoscope_state));
  if (stateptr == 0)
    return 0;
  stateptr->cstate = convolve_init ();
  colors_init (stateptr->colors);
  return stateptr;
}

void
monoscope_close (struct monoscope_state *stateptr)
{
  convolve_close (stateptr->cstate);
  free (stateptr);
}

guint32 *
monoscope_update (struct monoscope_state *stateptr, gint16 data[512])
{
  /* Note that CONVOLVE_BIG must == data size here, ie 512. */
  /* Really, we want samples evenly spread over the available data.
   * Just taking a continuous chunk will do for now, though. */
  int i;
  int foo;
  int bar;
  int h;
  guint32 *loc;

  int factor;
  int val;
  int max = 1;
  short *thisEq;

  memcpy (stateptr->copyEq, data, sizeof (short) * CONVOLVE_BIG);
  thisEq = stateptr->copyEq;
#if 1
  val = convolve_match (stateptr->avgEq, stateptr->copyEq, stateptr->cstate);
  thisEq += val;
#endif
  memset (stateptr->display, 0, 256 * 128 * sizeof (guint32));
  for (i = 0; i < 256; i++) {
    foo = thisEq[i] + (stateptr->avgEq[i] >> 1);
    stateptr->avgEq[i] = foo;
    if (foo < 0)
      foo = -foo;
    if (foo > max)
      max = foo;
  }
  stateptr->avgMax += max - (stateptr->avgMax >> 8);
  if (stateptr->avgMax < max)
    stateptr->avgMax = max;     /* Avoid overflow */
  factor = 0x7fffffff / stateptr->avgMax;
  /* Keep the scaling sensible. */
  if (factor > (1 << 18))
    factor = 1 << 18;
  if (factor < (1 << 8))
    factor = 1 << 8;
  for (i = 0; i < 256; i++) {
    foo = stateptr->avgEq[i] * factor;
    foo >>= 18;
    if (foo > 63)
      foo = 63;
    if (foo < -63)
      foo = -63;
    val = (i + ((foo + 64) << 8));
    bar = val;
    if ((bar > 0) && (bar < (256 * 128))) {
      loc = stateptr->display + bar;
      if (foo < 0) {
        for (h = 0; h <= (-foo); h++) {
          *loc = stateptr->colors[h];
          loc += 256;
        }
      } else {
        for (h = 0; h <= foo; h++) {
          *loc = stateptr->colors[h];
          loc -= 256;
        }
      }
    }
  }

  /* Draw grid. */
  for (i = 16; i < 128; i += 16) {
    for (h = 0; h < 256; h += 2) {
      stateptr->display[(i << 8) + h] = stateptr->colors[63];
      if (i == 64)
        stateptr->display[(i << 8) + h + 1] = stateptr->colors[63];
    }
  }
  for (i = 16; i < 256; i += 16) {
    for (h = 0; h < 128; h += 2) {
      stateptr->display[i + (h << 8)] = stateptr->colors[63];
    }
  }

  return stateptr->display;
}
