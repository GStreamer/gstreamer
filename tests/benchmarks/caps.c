/* GStreamer
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
 *
 * caps.c: benchmark for caps creation and destruction
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


#include <gst/gst.h>


#define NUM_CAPS 10000

#define AUDIO_FORMATS_ALL " { S8, U8, " \
    "S16LE, S16BE, U16LE, U16BE, " \
    "S24_32LE, S24_32BE, U24_32LE, U24_32BE, " \
    "S32LE, S32BE, U32LE, U32BE, " \
    "S24LE, S24BE, U24LE, U24BE, " \
    "S20LE, S20BE, U20LE, U20BE, " \
    "S18LE, S18BE, U18LE, U18BE, " \
    "F32LE, F32BE, F64LE, F64BE }"

#define GST_AUDIO_INT_PAD_TEMPLATE_CAPS \
  "audio/x-raw, " \
  "format = (string) " AUDIO_FORMATS_ALL ", " \
  "rate = (int) [ 1, MAX ], " \
  "channels = (int) [ 1, MAX ]"


gint
main (gint argc, gchar * argv[])
{
  GstCaps **capses;
  GstCaps *protocaps;
  GstClockTime start, end;
  gint i;

  gst_init (&argc, &argv);

  protocaps = gst_caps_from_string (GST_AUDIO_INT_PAD_TEMPLATE_CAPS);

  start = gst_util_get_timestamp ();
  capses = g_new (GstCaps *, NUM_CAPS);
  for (i = 0; i < NUM_CAPS; i++)
    capses[i] = gst_caps_copy (protocaps);
  end = gst_util_get_timestamp ();
  g_print ("%" GST_TIME_FORMAT " - creating %d caps\n",
      GST_TIME_ARGS (end - start), i);

  start = gst_util_get_timestamp ();
  for (i = 0; i < NUM_CAPS; i++)
    gst_caps_unref (capses[i]);
  end = gst_util_get_timestamp ();
  g_print ("%" GST_TIME_FORMAT " - destroying %d caps\n",
      GST_TIME_ARGS (end - start), i);

  g_free (capses);
  gst_caps_unref (protocaps);

  return 0;
}
