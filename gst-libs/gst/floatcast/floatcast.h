/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Library       <2002> Steve Baker <stevebaker_org@yahoo.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __FLOATCAST_H__
#define __FLOATCAST_H__

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <glib/gtypes.h>

G_BEGIN_DECLS

#if (HAVE_LRINT && HAVE_LRINTF)

	/*	These defines enable functionality introduced with the 1999 ISO C
	**	standard. They must be defined before the inclusion of math.h to
	**	engage them. If optimisation is enabled, these functions will be 
	**	inlined. With optimisation switched off, you have to link in the
	**	maths library using -lm.
	*/

	#define	_ISOC9X_SOURCE	1
	#define _ISOC99_SOURCE	1

	#define	__USE_ISOC9X	1
	#define	__USE_ISOC99	1

	#include	<math.h>

	#define gst_cast_float(x)	((gint)lrintf(x))
	#define gst_cast_double(x)	((gint)lrint(x))

#else
	/* use a standard c cast, but do rounding correctly */
	#define gst_cast_float(x)	((gint)floor((x)+0.5))
	#define gst_cast_double(x)	((gint)floor((x)+0.5))

#endif

inline static gfloat
GFLOAT_SWAP_LE_BE(gfloat in)
{
  gint32 swap;
  gfloat out;
  memcpy(&swap, &in, 4);
  swap = GUINT32_SWAP_LE_BE_CONSTANT (swap);
  memcpy(&out, &swap, 4);
  return out;
}

inline static gdouble
GDOUBLE_SWAP_LE_BE(gdouble in)
{
  gint64 swap;
  gdouble out;
  memcpy(&swap, &in, 8);
  swap = GUINT64_SWAP_LE_BE_CONSTANT (swap);
  memcpy(&out, &swap, 8);
  return out;
}

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define GFLOAT_TO_LE(val)    ((gfloat) (val))
#define GFLOAT_TO_BE(val)    (GFLOAT_SWAP_LE_BE (val))
#define GDOUBLE_TO_LE(val)   ((gdouble) (val))
#define GDOUBLE_TO_BE(val)   (GDOUBLE_SWAP_LE_BE (val))

#elif G_BYTE_ORDER == G_BIG_ENDIAN
#define GFLOAT_TO_LE(val)    (GFLOAT_SWAP_LE_BE (val))
#define GFLOAT_TO_BE(val)    ((gfloat) (val))
#define GDOUBLE_TO_LE(val)   (GDOUBLE_SWAP_LE_BE (val))
#define GDOUBLE_TO_BE(val)   ((gdouble) (val))

#else /* !G_LITTLE_ENDIAN && !G_BIG_ENDIAN */
#error unknown ENDIAN type
#endif /* !G_LITTLE_ENDIAN && !G_BIG_ENDIAN */

#define GFLOAT_FROM_LE(val)  (GFLOAT_TO_LE (val))
#define GFLOAT_FROM_BE(val)  (GDOUBLE_TO_BE (val))
#define GDOUBLE_FROM_LE(val) (GFLOAT_TO_LE (val))
#define GDOUBLE_FROM_BE(val) (GDOUBLE_TO_BE (val))

G_END_DECLS

#endif /* __FLOATCAST_H__ */

