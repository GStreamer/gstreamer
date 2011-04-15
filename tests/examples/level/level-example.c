/* GStreamer
 * Copyright (C) 2000,2001,2002,2003,2005
 *           Thomas Vander Stichele <thomas at apestaart dot org>
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

#include <string.h>
#include <math.h>

#include <gst/gst.h>

static gboolean
message_handler (GstBus * bus, GstMessage * message, gpointer data)
{

  if (message->type == GST_MESSAGE_ELEMENT) {
    const GstStructure *s = gst_message_get_structure (message);
    const gchar *name = gst_structure_get_name (s);

    if (strcmp (name, "level") == 0) {
      gint channels;
      GstClockTime endtime;
      gdouble rms_dB, peak_dB, decay_dB;
      gdouble rms;
      const GValue *list;
      const GValue *value;

      gint i;

      if (!gst_structure_get_clock_time (s, "endtime", &endtime))
        g_warning ("Could not parse endtime");
      /* we can get the number of channels as the length of any of the value
       * lists */
      list = gst_structure_get_value (s, "rms");
      channels = gst_value_list_get_size (list);

      g_print ("endtime: %" GST_TIME_FORMAT ", channels: %d\n",
          GST_TIME_ARGS (endtime), channels);
      for (i = 0; i < channels; ++i) {
        g_print ("channel %d\n", i);
        list = gst_structure_get_value (s, "rms");
        value = gst_value_list_get_value (list, i);
        rms_dB = g_value_get_double (value);
        list = gst_structure_get_value (s, "peak");
        value = gst_value_list_get_value (list, i);
        peak_dB = g_value_get_double (value);
        list = gst_structure_get_value (s, "decay");
        value = gst_value_list_get_value (list, i);
        decay_dB = g_value_get_double (value);
        g_print ("    RMS: %f dB, peak: %f dB, decay: %f dB\n",
            rms_dB, peak_dB, decay_dB);

        /* converting from dB to normal gives us a value between 0.0 and 1.0 */
        rms = pow (10, rms_dB / 20);
        g_print ("    normalized rms value: %f\n", rms);
      }
    }
  }
  /* we handled the message we want, and ignored the ones we didn't want.
   * so the core can unref the message for us */
  return TRUE;
}

int
main (int argc, char *argv[])
{
  GstElement *audiotestsrc, *audioconvert, *level, *fakesink;
  GstElement *pipeline;
  GstCaps *caps;
  GstBus *bus;
  guint watch_id;
  GMainLoop *loop;

  gst_init (&argc, &argv);

  caps = gst_caps_from_string ("audio/x-raw-int,channels=2");

  pipeline = gst_pipeline_new (NULL);
  g_assert (pipeline);
  audiotestsrc = gst_element_factory_make ("audiotestsrc", NULL);
  g_assert (audiotestsrc);
  audioconvert = gst_element_factory_make ("audioconvert", NULL);
  g_assert (audioconvert);
  level = gst_element_factory_make ("level", NULL);
  g_assert (level);
  fakesink = gst_element_factory_make ("fakesink", NULL);
  g_assert (fakesink);

  gst_bin_add_many (GST_BIN (pipeline), audiotestsrc, audioconvert, level,
      fakesink, NULL);
  g_assert (gst_element_link (audiotestsrc, audioconvert));
  g_assert (gst_element_link_filtered (audioconvert, level, caps));
  g_assert (gst_element_link (level, fakesink));

  /* make sure we'll get messages */
  g_object_set (G_OBJECT (level), "message", TRUE, NULL);
  /* run synced and not as fast as we can */
  g_object_set (G_OBJECT (fakesink), "sync", TRUE, NULL);

  bus = gst_element_get_bus (pipeline);
  watch_id = gst_bus_add_watch (bus, message_handler, NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* we need to run a GLib main loop to get the messages */
  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  g_source_remove (watch_id);
  g_main_loop_unref (loop);
  return 0;
}
