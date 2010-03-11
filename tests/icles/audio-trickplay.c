/*
 * audio-trickplay.c
 *
 * Builds a pipeline with two audiotestsources mixed with adder. Assigns
 * controller patterns to the audio generators and test various trick modes.
 */

#include <gst/gst.h>
#include <gst/controller/gstcontroller.h>
#include <gst/controller/gstinterpolationcontrolsource.h>

static void
check_position (GstElement * sink, GstQuery * pos, const gchar * info)
{
  if (gst_element_query (sink, pos)) {
    gint64 play_pos;
    gst_query_parse_position (pos, NULL, &play_pos);
    printf ("pos: %" GST_TIME_FORMAT " %s\n", GST_TIME_ARGS (play_pos), info);
  } else {
    GST_WARNING ("position query failed");
  }
}

gint
main (gint argc, gchar ** argv)
{
  gint res = 1;
  GstElement *src, *mix = NULL, *sink;
  GstElement *bin;
  GstController *ctrl;
  GstInterpolationControlSource *csource1, *csource2;
  GstClock *clock;
  GstClockID clock_id;
  GstClockReturn wait_ret;
  GValue vol = { 0, };
  GstEvent *pos_seek, *rate_seek;
  GstQuery *pos;
  /* options */
  gboolean use_adder = FALSE;

  gst_init (&argc, &argv);
  gst_controller_init (&argc, &argv);

  if (argc) {
    gint arg;
    for (arg = 0; arg < argc; arg++) {
      if (!strcmp (argv[arg], "-a"))
        use_adder = TRUE;
    }
  }

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");
  clock = gst_pipeline_get_clock (GST_PIPELINE (bin));
  src = gst_element_factory_make ("audiotestsrc", NULL);
  if (!src) {
    GST_WARNING ("need audiotestsrc from gst-plugins-base");
    goto Error;
  }
  if (use_adder) {
    mix = gst_element_factory_make ("adder", NULL);
    if (!src) {
      GST_WARNING ("need adder from gst-plugins-base");
      goto Error;
    }
  }
  sink = gst_element_factory_make ("autoaudiosink", NULL);
  if (!sink) {
    GST_WARNING ("need autoaudiosink from gst-plugins-base");
    goto Error;
  }

  if (use_adder) {
    gst_bin_add_many (GST_BIN (bin), src, mix, sink, NULL);
    if (!gst_element_link_many (src, mix, sink, NULL)) {
      GST_WARNING ("can't link elements");
      goto Error;
    }
  } else {
    gst_bin_add_many (GST_BIN (bin), src, sink, NULL);
    if (!gst_element_link_many (src, sink, NULL)) {
      GST_WARNING ("can't link elements");
      goto Error;
    }
  }

  /* add a controller to the source */
  if (!(ctrl = gst_controller_new (G_OBJECT (src), "freq", "volume", NULL))) {
    GST_WARNING ("can't control source element");
    goto Error;
  }

  csource1 = gst_interpolation_control_source_new ();
  csource2 = gst_interpolation_control_source_new ();

  gst_controller_set_control_source (ctrl, "volume",
      GST_CONTROL_SOURCE (csource1));
  gst_controller_set_control_source (ctrl, "freq",
      GST_CONTROL_SOURCE (csource2));

  /* Set interpolation mode */

  gst_interpolation_control_source_set_interpolation_mode (csource1,
      GST_INTERPOLATE_LINEAR);
  gst_interpolation_control_source_set_interpolation_mode (csource2,
      GST_INTERPOLATE_LINEAR);

  /* set control values */
  g_value_init (&vol, G_TYPE_DOUBLE);
  g_value_set_double (&vol, 0.0);
  gst_interpolation_control_source_set (csource1, 0 * GST_SECOND, &vol);
  g_value_set_double (&vol, 1.0);
  gst_interpolation_control_source_set (csource1, 5 * GST_SECOND, &vol);

  g_object_unref (csource1);

  g_value_set_double (&vol, 220.0);
  gst_interpolation_control_source_set (csource2, 0 * GST_SECOND, &vol);
  g_value_set_double (&vol, 3520.0);
  gst_interpolation_control_source_set (csource2, 2 * GST_SECOND, &vol);
  g_value_set_double (&vol, 440.0);
  gst_interpolation_control_source_set (csource2, 6 * GST_SECOND, &vol);

  g_object_unref (csource2);

  /* prepare events */
  pos_seek = gst_event_new_seek (1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_SET, 3 * GST_SECOND,
      GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
  rate_seek = gst_event_new_seek (0.5, GST_FORMAT_TIME, GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE,
      GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);

  /* prepare queries */
  pos = gst_query_new_position (GST_FORMAT_TIME);


  /* run the show */
  if (gst_element_set_state (bin, GST_STATE_PAUSED)) {

    /* run for 5 seconds */
    clock_id =
        gst_clock_new_single_shot_id (clock,
        gst_clock_get_time (clock) + (5 * GST_SECOND));

    if (gst_element_set_state (bin, GST_STATE_PLAYING)) {
      check_position (sink, pos, "start");
      if ((wait_ret = gst_clock_id_wait (clock_id, NULL)) != GST_CLOCK_OK) {
        GST_WARNING ("clock_id_wait returned: %d", wait_ret);
      }
    }
    gst_clock_id_unref (clock_id);

    check_position (sink, pos, "before seek to new pos");

    /* seek to 3:00 sec (back 2 sec) */
    if (!gst_element_send_event (sink, pos_seek)) {
      GST_WARNING ("element failed to seek to new position");
    }

    check_position (sink, pos, "after seek to new pos");

    /* run for 2 seconds */
    clock_id =
        gst_clock_new_single_shot_id (clock,
        gst_clock_get_time (clock) + (2 * GST_SECOND));
    if ((wait_ret = gst_clock_id_wait (clock_id, NULL)) != GST_CLOCK_OK) {
      GST_WARNING ("clock_id_wait returned: %d", wait_ret);
    }
    gst_clock_id_unref (clock_id);

    check_position (sink, pos, "before rate change");

    /* change playback rate */
    if (!gst_element_send_event (sink, rate_seek)) {
      GST_WARNING ("element failed to change playback rate");
    }

    check_position (sink, pos, "after rate change");

    /* run for 4 seconds */
    clock_id =
        gst_clock_new_single_shot_id (clock,
        gst_clock_get_time (clock) + (4 * GST_SECOND));
    if ((wait_ret = gst_clock_id_wait (clock_id, NULL)) != GST_CLOCK_OK) {
      GST_WARNING ("clock_id_wait returned: %d", wait_ret);
    }
    gst_clock_id_unref (clock_id);

    check_position (sink, pos, "done");

    gst_element_set_state (bin, GST_STATE_NULL);
  }

  /* cleanup */
  gst_query_unref (pos);
  g_object_unref (G_OBJECT (ctrl));
  gst_object_unref (G_OBJECT (clock));
  gst_object_unref (G_OBJECT (bin));
  res = 0;
Error:
  return (res);
}
