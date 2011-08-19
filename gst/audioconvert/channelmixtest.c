/* GStreamer
 * Copyright (C) 2005 Benjamin Otte <otte@gnome.org>
 *
 * channelmixtest.c: simple test of channel mixing
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

#include "gstchannelmix.h"
#include "plugin.h"

int
main (gint argc, gchar ** argv)
{
  GstElement *bin, *src, *sink;
  GstAudioConvert *c;
  GstCaps *caps;
  guint i, j, k;
  struct
  {
    gchar *sinkcaps;
    gchar *srccaps;
    gfloat matrix[6][6];        /* use a predefined matrix here, makes stuff simpler */
  } tests[] = {
    /* stereo => mono */
    {
      "audio/x-raw, channels=2", "audio/x-raw, channels=1", { {
      0.5,}, {
    0.5,},}},
        /* mono => stereo */
    {
      "audio/x-raw, channels=1", "audio/x-raw, channels=2", { {
    1, 1,},}}
  };

  gst_init (&argc, &argv);

  for (i = 0; i < G_N_ELEMENTS (tests); i++) {
    g_print ("running test %u\n", i);
    bin = gst_element_factory_make ("pipeline", NULL);
    c = g_object_new (GST_TYPE_AUDIO_CONVERT, NULL);
    /* avoid gst being braindead */
    gst_object_set_name (GST_OBJECT (c), "shuddup");
    src = gst_element_factory_make ("fakesrc", NULL);
    sink = gst_element_factory_make ("fakesink", NULL);
    gst_bin_add_many (GST_BIN (bin), src, c, sink, NULL);
    caps = gst_caps_from_string (tests[i].sinkcaps);
    g_assert (caps);
    if (!gst_element_link_filtered (src, GST_ELEMENT (c), caps))
      g_assert_not_reached ();
    gst_caps_unref (caps);
    caps = gst_caps_from_string (tests[i].srccaps);
    g_assert (caps);
    if (!gst_element_link_filtered (GST_ELEMENT (c), sink, caps))
      g_assert_not_reached ();
    gst_caps_unref (caps);
    if (!gst_element_set_state (bin, GST_STATE_PLAYING))
      g_assert_not_reached ();
    g_assert (c->srccaps.channels <= 6);
    g_assert (c->sinkcaps.channels <= 6);
    for (j = 0; j < 6; j++) {
      for (k = 0; k < 6; k++) {
        if (j < c->sinkcaps.channels && k < c->srccaps.channels) {
          if (tests[i].matrix[j][k] != c->matrix[j][k]) {
            g_printerr ("matrix[j][k] should be %g but is %g\n",
                tests[i].matrix[j][k], c->matrix[j][k]);
            g_assert_not_reached ();
          }
        } else {
          g_assert (tests[i].matrix[j][k] == 0);
        }
      }
    }
    gst_object_unref (bin);
  }

  return 0;
}
