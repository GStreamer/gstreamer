/*
 * DSP utils
 * Copyright (c) 2000, 2001 Fabrice Bellard.
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * gmc & q-pel & 32/64 bit based MC by Michael Niedermayer <michaelni@gmx.at>
 */

/**
 * @file dsputil.c
 * DSP utils
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "avcodec.h"
#include "dsputil.h"

uint8_t cropTbl[256 + 2 * MAX_NEG_CROP];

/* init static data */
void
dsputil_static_init (void)
{
  int i;

  for (i = 0; i < 256; i++)
    cropTbl[i + MAX_NEG_CROP] = i;
  for (i = 0; i < MAX_NEG_CROP; i++) {
    cropTbl[i] = 0;
    cropTbl[i + MAX_NEG_CROP + 256] = 255;
  }
}
