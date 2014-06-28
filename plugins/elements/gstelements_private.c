/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@schleef.org>
 * Copyright (C) 2011 Tim-Philipp Müller <tim.muller@collabora.co.uk>
 * Copyright (C) 2014 Tim-Philipp Müller <tim@centricular.com>
 * Copyright (C) 2014 Vincent Penquerc'h <vincent@collabora.co.uk>
 *
 * gstelements_private.c: Shared code for core elements
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <string.h>
#include "gst/gst.h"
#include "gstelements_private.h"

#define BUFFER_FLAG_SHIFT 4

G_STATIC_ASSERT ((1 << BUFFER_FLAG_SHIFT) == GST_MINI_OBJECT_FLAG_LAST);

/* Returns a newly allocated string describing the flags on this buffer */
char *
gst_buffer_get_flags_string (GstBuffer * buffer)
{
  static const char flag_strings[] =
      "\000\000\000\000live\000decode-only\000discont\000resync\000corrupted\000"
      "marker\000header\000gap\000droppable\000delta-unit\000tag-memory\000"
      "FIXME";
  static const guint8 flag_idx[] = { 0, 1, 2, 3, 4, 9, 21, 29, 36, 46, 53,
    60, 64, 74, 85, 96
  };
  int i, max_bytes;
  char *flag_str, *end;

  /* max size is all flag strings plus a space or terminator after each one */
  max_bytes = sizeof (flag_strings);
  flag_str = g_malloc (max_bytes);

  end = flag_str;
  end[0] = '\0';
  for (i = BUFFER_FLAG_SHIFT; i < G_N_ELEMENTS (flag_idx); i++) {
    if (GST_MINI_OBJECT_CAST (buffer)->flags & (1 << i)) {
      strcpy (end, flag_strings + flag_idx[i]);
      end += strlen (end);
      end[0] = ' ';
      end[1] = '\0';
      end++;
    }
  }

  return flag_str;
}
