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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#include <gst/gst.h>


#define NUM_CAPS 10000


#define GST_AUDIO_INT_PAD_TEMPLATE_CAPS \
  "audio/x-raw-int, " \
  "rate = (int) [ 1, MAX ], " \
  "channels = (int) [ 1, MAX ], " \
  "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
  "width = (int) { 8, 16, 24, 32 }, " \
  "depth = (int) [ 1, 32 ], " \
  "signed = (boolean) { true, false }"


static GstClockTime
gst_get_current_time (void)
{
  GTimeVal tv;

  g_get_current_time (&tv);
  return GST_TIMEVAL_TO_TIME (tv);
}


gint
main (gint argc, gchar * argv[])
{
  GstCaps **capses;
  GstCaps *protocaps;
  GstClockTime start, end;
  gint i;

  gst_init (&argc, &argv);

  protocaps = gst_caps_from_string (GST_AUDIO_INT_PAD_TEMPLATE_CAPS);

  start = gst_get_current_time ();
  capses = g_new (GstCaps *, NUM_CAPS);
  for (i = 0; i < NUM_CAPS; i++)
    capses[i] = gst_caps_copy (protocaps);
  end = gst_get_current_time ();
  g_print ("%" GST_TIME_FORMAT " - creating %d caps\n",
      GST_TIME_ARGS (end - start), i);

  start = gst_get_current_time ();
  for (i = 0; i < NUM_CAPS; i++)
    gst_caps_unref (capses[i]);
  end = gst_get_current_time ();
  g_print ("%" GST_TIME_FORMAT " - destroying %d caps\n",
      GST_TIME_ARGS (end - start), i);

  g_free (capses);
  gst_caps_unref (protocaps);

  return 0;
}
