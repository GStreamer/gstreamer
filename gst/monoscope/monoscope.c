/*  monoscope.cpp
 *  Copyright (C) 2002 Richard Boulton <richard@tartarus.org>
 *  Copyright (C) 1998-2001 Andy Lo A Foe <andy@alsaplayer.org>
 *  Original code by Tinic Uro
 *
 *  This code is copied from Alsaplayer.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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
  stateptr = calloc (1, sizeof (struct monoscope_state));
  if (stateptr == 0)
    return 0;
  stateptr->cstate = convolve_init ();
  colors_init (stateptr->colors);
  return stateptr;
}

guint32 *
monoscope_update (struct monoscope_state * stateptr, gint16 data[512])
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
    stateptr->avgMax = max;	/* Avoid overflow */
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
    if (foo < -64)
      foo = -64;
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

void
monoscope_close (struct monoscope_state *stateptr)
{
}
