/* GStreamer
 * Copyright (C) 2009 Stefan Kost <ensonic@users.sf.net>
 *
 * controller.c: benchmark for interpolation control-source
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

#include <stdio.h>

#include <gst/gst.h>
#include <gst/controller/gstinterpolationcontrolsource.h>
#include <gst/controller/gstdirectcontrolbinding.h>

/* a song in buzztard can easily reach 30000 here */
#define NUM_CP 15000
#define BLOCK_SIZE 64

static void
event_loop (GstElement * pipe)
{
  GstBus *bus;
  GstMessage *message = NULL;
  gboolean running = TRUE;

  bus = gst_element_get_bus (GST_ELEMENT (pipe));

  while (running) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    g_assert (message != NULL);

    switch (message->type) {
      case GST_MESSAGE_EOS:
        running = FALSE;
        break;
      case GST_MESSAGE_WARNING:{
        GError *gerror;
        gchar *debug;

        gst_message_parse_warning (message, &gerror, &debug);
        gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
        g_error_free (gerror);
        g_free (debug);
        break;
      }
      case GST_MESSAGE_ERROR:{
        GError *gerror;
        gchar *debug;

        gst_message_parse_error (message, &gerror, &debug);
        gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
        g_error_free (gerror);
        g_free (debug);
        running = FALSE;
        break;
      }
      default:
        break;
    }
    gst_message_unref (message);
  }
  gst_object_unref (bus);
}

gint
main (gint argc, gchar * argv[])
{
  gint res = 1;
  gint i, j;
  GstElement *src, *sink;
  GstElement *bin;
  GstControlSource *cs;
  GstTimedValueControlSource *tvcs;
  GstClockTime bt, ct;
  GstClockTimeDiff elapsed;
  GstClockTime tick;

  gst_init (&argc, &argv);

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("audiotestsrc", "gen_audio");
  if (!src) {
    GST_WARNING ("need audiotestsrc from gst-plugins-base");
    goto Error;
  }
  sink = gst_element_factory_make ("fakesink", "swallow_audio");

  gst_bin_add_many (GST_BIN (bin), src, sink, NULL);
  if (!gst_element_link (src, sink)) {
    GST_WARNING ("can't link elements");
    goto Error;
  }

  g_object_set (G_OBJECT (src), "wave", 7,      /* sine table - we don't want to benchmark the fpu */
      "num-buffers", NUM_CP, "samplesperbuffer", BLOCK_SIZE, NULL);

  tick = BLOCK_SIZE * GST_SECOND / 44100;

  /* create and configure control source */
  cs = gst_interpolation_control_source_new ();
  tvcs = (GstTimedValueControlSource *) cs;

  gst_object_add_control_binding (GST_OBJECT (src),
      gst_direct_control_binding_new (GST_OBJECT (src), "freq", cs));
  g_object_set (cs, "mode", GST_INTERPOLATION_MODE_LINEAR, NULL);

  /* set control values, we set them in a linear order as we would when loading
   * a stored project
   */
  bt = gst_util_get_timestamp ();

  for (i = 0; i < NUM_CP; i++) {
    gst_timed_value_control_source_set (tvcs, i * tick,
        g_random_double_range (50.0, 3000.0));
  }

  ct = gst_util_get_timestamp ();
  elapsed = GST_CLOCK_DIFF (bt, ct);
  printf ("linear insert of control-points: %" GST_TIME_FORMAT "\n",
      GST_TIME_ARGS (elapsed));


  /* set extra control values, we set them in arbitrary order to simulate
   * the user editing a project from the ui
   */
  bt = gst_util_get_timestamp ();

  for (i = 0; i < 100; i++) {
    j = g_random_int_range (0, NUM_CP - 1);
    gst_timed_value_control_source_set (tvcs, j * tick,
        g_random_double_range (50.0, 3000.0));
  }

  ct = gst_util_get_timestamp ();
  elapsed = GST_CLOCK_DIFF (bt, ct);
  printf ("random insert of control-points: %" GST_TIME_FORMAT "\n",
      GST_TIME_ARGS (elapsed));

  {
    GstClockTime sample_duration =
        gst_util_uint64_scale_int (1, GST_SECOND, 44100);
    gdouble *values = g_new0 (gdouble, BLOCK_SIZE * NUM_CP);

    bt = gst_util_get_timestamp ();
    gst_control_source_get_value_array (cs, 0, sample_duration,
        BLOCK_SIZE * NUM_CP, values);
    ct = gst_util_get_timestamp ();
    g_free (values);
    elapsed = GST_CLOCK_DIFF (bt, ct);
    printf ("linear array for control-points: %" GST_TIME_FORMAT "\n",
        GST_TIME_ARGS (elapsed));
  }

  /* play, this test sequential reads */
  bt = gst_util_get_timestamp ();

  if (gst_element_set_state (bin, GST_STATE_PLAYING)) {
    /* wait for EOS */
    event_loop (bin);
    gst_element_set_state (bin, GST_STATE_NULL);
  }

  ct = gst_util_get_timestamp ();
  elapsed = GST_CLOCK_DIFF (bt, ct);
  printf ("linear read of control-points  : %" GST_TIME_FORMAT "\n",
      GST_TIME_ARGS (elapsed));

  /* cleanup */
  gst_object_unref (cs);
  gst_object_unref (bin);
  res = 0;
Error:
  return res;
}
