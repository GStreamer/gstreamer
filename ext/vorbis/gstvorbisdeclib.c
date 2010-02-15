/* GStreamer
 * Copyright (C) 2010 Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 *   Contact: Stefan Kost <stefan.kost@nokia.com>
 *
 * Tremor modifications <2006>:
 *   Chris Lord, OpenedHand Ltd. <chris@openedhand.com>, http://www.o-hand.com/
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvorbisdeclib.h"

#ifndef TREMOR
/* These samples can be outside of the float -1.0 -- 1.0 range, this
 * is allowed, downstream elements are supposed to clip */
void
copy_samples (vorbis_sample_t * out, vorbis_sample_t ** in, guint samples,
    gint channels, gint width)
{
  gint i, j;

  g_assert (width == 4);

#ifdef GST_VORBIS_DEC_SEQUENTIAL
  for (i = 0; i < channels; i++) {
    memcpy (out, in[i], samples * sizeof (float));
    out += samples;
  }
#else
  for (j = 0; j < samples; j++) {
    for (i = 0; i < channels; i++) {
      *out++ = in[i][j];
    }
  }
#endif
}

#else

/* Taken from Tremor, misc.h */
#ifdef _ARM_ASSEM_
static inline ogg_int32_t
CLIP_TO_15 (ogg_int32_t x)
{
  int tmp;
  asm volatile ("subs	%1, %0, #32768\n\t"
      "movpl	%0, #0x7f00\n\t"
      "orrpl	%0, %0, #0xff\n"
      "adds	%1, %0, #32768\n\t"
      "movmi	%0, #0x8000":"+r" (x), "=r" (tmp)
      ::"cc");

  return (x);
}
#else
static inline ogg_int32_t
CLIP_TO_15 (ogg_int32_t x)
{
  int ret = x;

  ret -= ((x <= 32767) - 1) & (x - 32767);
  ret -= ((x >= -32768) - 1) & (x + 32768);
  return (ret);
}
#endif

static void
copy_samples_32 (gint32 * out, ogg_int32_t ** in, guint samples, gint channels)
{
  gint i, j;

  for (j = 0; j < samples; j++) {
    for (i = 0; i < channels; i++) {
      *out++ = CLIP_TO_15 (in[i][j] >> 9);
    }
  }
}

static void
copy_samples_16 (gint16 * out, ogg_int32_t ** in, guint samples, gint channels)
{
  gint i, j;

  for (j = 0; j < samples; j++) {
    for (i = 0; i < channels; i++) {
      *out++ = CLIP_TO_15 (in[i][j] >> 9);
    }
  }
}

void
copy_samples (vorbis_sample_t * out, vorbis_sample_t ** in, guint samples,
    gint channels, gint width)
{
  if (width == 4) {
    copy_samples_32 ((gint32 *) out, in, samples, channels);
  } else if (width == 2) {
    copy_samples_16 ((gint16 *) out, in, samples, channels);
  } else {
    g_assert_not_reached ();
  }
}

#endif
