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
#include "monoscope.h"
#include "convolve.h"

#define scope_width 256
#define scope_height 128

static gint16 newEq[CONVOLVE_BIG];      // latest block of 512 samples.
static gint16 copyEq[CONVOLVE_BIG];
static int avgEq[CONVOLVE_SMALL];      // a running average of the last few.
static int avgMax;                     // running average of max sample.
static guint32 display[(scope_width + 1) * (scope_height + 1)];

static convolve_state *state = NULL;
static guint32 colors[64];

static void colors_init(guint32 * colors)
{
    int i;
    for (i = 0; i < 32; i++) {
	colors[i] = (i*8 << 16) + (255 << 8);
	colors[i+31] = (255 << 16) + (((31 - i) * 8) << 8);
    }
    colors[63] = (40 << 16) + (75 << 8);
}

void monoscope_init (guint32 resx, guint32 resy)
{
    state = convolve_init();
    colors_init(colors);
}

guint32 * monoscope_update (gint16 data [2][512])
{
    /* Note that CONVOLVE_BIG must == data size here, ie 512. */
    /* Really, we want samples evenly spread over the available data.
     * Just taking a continuous chunk will do for now, though. */
    int i;
    for (i = 0; i < CONVOLVE_BIG; i++) {
	/* Average the two channels. */
	newEq[i] = (((int) data[0][i]) + (int) data[1][i]) >> 1;
    }

    int foo;
    int bar;  
    int h;
    guchar bits[ 257 * 129];
    guint32 *loc;

	int factor;
	int val;
	int max = 1;
	short * thisEq;
	memcpy (copyEq, newEq, sizeof (short) * CONVOLVE_BIG);
	thisEq = copyEq;
#if 1					
	val = convolve_match (avgEq, copyEq, state);
	thisEq += val;
#endif					
	memset(display, 0, 256 * 128 * sizeof(guint32));
	for (i=0; i < 256; i++) {
	    foo = thisEq[i] + (avgEq[i] >> 1);
	    avgEq[i] = foo;
	    if (foo < 0)
		foo = -foo;
	    if (foo > max)
		max = foo;
	}
	avgMax += max - (avgMax >> 8);
	if (avgMax < max)
	    avgMax = max; /* Avoid overflow */
	factor = 0x7fffffff / avgMax;
	/* Keep the scaling sensible. */
	if (factor > (1 << 18))
	    factor = 1 << 18;
	if (factor < (1 << 8))
	    factor = 1 << 8;
	for (i=0; i < 256; i++) {
	    foo = avgEq[i] * factor;
	    foo >>= 18;
	    if (foo > 63)
		foo = 63;
	    if (foo < -64)
		foo = -64;
	    val = (i + ((foo+64) << 8));
	    bar = val;
	    if ((bar > 0) && (bar < (256 * 128))) {
		loc = display + bar;
		if (foo < 0) {
		    for (h = 0; h <= (-foo); h++) {
			*loc = colors[h];
			loc+=256; 
		    }
		} else {
		    for (h = 0; h <= foo; h++) {
			*loc = colors[h];
			loc-=256;
		    }
		}
	    }
	}

	/* Draw grid. */
	for (i=16;i < 128; i+=16) {
	    for (h = 0; h < 256; h+=2) {
		display[(i << 8) + h] = colors[63];
		if (i == 64)
		    display[(i << 8) + h + 1] = colors[63];
	    }
	}
	for (i = 16; i < 256; i+=16) {
	    for (h = 0; h < 128; h+=2) {
		display[i + (h << 8)] = colors[63];
	    }
	}

    return display;
}

void monoscope_close ()
{
}

