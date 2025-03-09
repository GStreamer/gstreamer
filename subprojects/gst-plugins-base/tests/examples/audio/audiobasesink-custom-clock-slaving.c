/* GStreamer
 *
 * audiobasesink-custom-clock-slaving.c: sample application to show
 * how to use a custom clock slaving algorithm with audiobasesink
 *
 * Copyright (C) <2025> Carlos Rafael Giani <crg7475 AT mailbox DOT org>
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
#include "config.h"
#endif

#include <gst/gst.h>
/* Need to include this for the gst_audio_base_sink_set_custom_slaving_callback function */
#include <gst/audio/gstaudiobasesink.h>

#include <gst/gst.h>
#include <gtk/gtk.h>

#define INITIAL_DRIFT_TOLERANCE 40
#define INITIAL_SKEW_STEP_SIZE 1.0
#define MIN_SIMULATED_DRIFT_PPM -100000
#define MAX_SIMULATED_DRIFT_PPM +100000

/* If this is enabled, a running average is used to filter the drift.
 * The running average is then compared against the drift_tolerance,
 * and a skew is requested. If this is disabled, the current, unfiltered
 * drift is directly compared against the drift_tolerance instead. */
#define USE_RUNNING_AVERAGE

/* Global widgets for the interaction. */
static GtkWidget *drift_tolerance_widget;
static GtkWidget *skew_step_size_widget;
static GtkWidget *cur_drift_display_widget;
#ifdef USE_RUNNING_AVERAGE
static GtkWidget *avg_drift_display_widget;
#endif

/* Global states, accessed by the custom clock slaving callback
 * and by the widget callbacks. Since the former runs in a separate
 * thread, a mutex is needed for synchronized access. */
static GMutex state_mutex;
/* The current drift is set by the custom clock slaving callback.
 * If USE_RUNNING_AVERAGE is enabled, the average and first drift
 * states are set as well. first_drift is used to check if
 * average_drift has a valid value or not (necessary to properly
 * initialize the running average at the beginning). */
static GstClockTimeDiff current_drift = 0;
#ifdef USE_RUNNING_AVERAGE
static gboolean first_drift = TRUE;
static GstClockTimeDiff average_drift = 0;
#endif
/* Values adjusted by the widgets and read in the custom clock
 * slaving callback. If USE_RUNNING_AVERAGE is defined, these
 * are applied against the average drift, not the current one.
 * If skew_step_size is set to zero, the current or average
 * drift is directly used as a skew request (see the)
 * (custom_clock_slaving_callback for details.) */
static GstClockTime drift_tolerance = INITIAL_DRIFT_TOLERANCE * GST_MSECOND;
static GstClockTimeDiff skew_step_size = INITIAL_SKEW_STEP_SIZE * GST_MSECOND;

static void
drift_tolerance_value_changed_callback (GtkWidget * widget,
    gpointer * user_data)
{
  gdouble value;

  value = gtk_range_get_value (GTK_RANGE (drift_tolerance_widget));

  /* Synchronize access since the value is also read by the
   * custom clock slaving callback, which runs in a separate thread. */
  g_mutex_lock (&state_mutex);
  drift_tolerance = value * GST_MSECOND;
  g_mutex_unlock (&state_mutex);
}

static void
skew_step_size_value_changed_callback (GtkWidget * widget, gpointer * user_data)
{
  gdouble value;

  value = gtk_range_get_value (GTK_RANGE (skew_step_size_widget));

  /* Synchronize access since the value is also read by the
   * custom clock slaving callback, which runs in a separate thread. */
  g_mutex_lock (&state_mutex);
  skew_step_size = value * GST_MSECOND;
  g_mutex_unlock (&state_mutex);
}

static gboolean
update_drift_labels (G_GNUC_UNUSED gpointer user_data)
{
  gchar *drift_str;

  GstClockTimeDiff cur_drift_snapshot;
#ifdef USE_RUNNING_AVERAGE
  GstClockTimeDiff avg_drift_snapshot;
#endif

  /* Synchronize access since these values are also read by the
   * custom clock slaving callback, which runs in a separate thread. */
  g_mutex_lock (&state_mutex);
  cur_drift_snapshot = current_drift;
#ifdef USE_RUNNING_AVERAGE
  avg_drift_snapshot = average_drift;
#endif
  g_mutex_unlock (&state_mutex);

  drift_str = g_strdup_printf ("%" G_GINT64_FORMAT, cur_drift_snapshot /
      GST_USECOND);
  gtk_label_set_text (GTK_LABEL (cur_drift_display_widget), drift_str);
  g_free (drift_str);

#ifdef USE_RUNNING_AVERAGE
  drift_str = g_strdup_printf ("%" G_GINT64_FORMAT, avg_drift_snapshot /
      GST_USECOND);
  gtk_label_set_text (GTK_LABEL (avg_drift_display_widget), drift_str);
  g_free (drift_str);
#endif

  return G_SOURCE_CONTINUE;
}

static void
setup_gui (GstElement * audiosink)
{
  GtkWidget *window;
  GtkWidget *vbox;
  GtkWidget *label, *hbox;

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (window, "destroy", gtk_main_quit, NULL);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  /* drift tolerance */
  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  label = gtk_label_new ("Drift tolerance (ms)");
  gtk_container_add (GTK_CONTAINER (hbox), label);
  drift_tolerance_widget = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
      1.0, 80.0, 1);
  gtk_range_set_value (GTK_RANGE (drift_tolerance_widget),
      INITIAL_DRIFT_TOLERANCE);
  gtk_widget_set_size_request (drift_tolerance_widget, 400, -1);
  gtk_container_add (GTK_CONTAINER (hbox), drift_tolerance_widget);
  gtk_container_add (GTK_CONTAINER (vbox), hbox);
  g_signal_connect (drift_tolerance_widget, "value-changed",
      G_CALLBACK (drift_tolerance_value_changed_callback), NULL);

  /* skew step size */
  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  label = gtk_label_new ("Skew step size (ms)");
  gtk_container_add (GTK_CONTAINER (hbox), label);
  skew_step_size_widget = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
      0, 10.0, 0.2);
  gtk_range_set_value (GTK_RANGE (skew_step_size_widget),
      INITIAL_SKEW_STEP_SIZE);
  gtk_widget_set_size_request (skew_step_size_widget, 400, -1);
  gtk_container_add (GTK_CONTAINER (hbox), skew_step_size_widget);
  gtk_container_add (GTK_CONTAINER (vbox), hbox);
  g_signal_connect (skew_step_size_widget, "value-changed",
      G_CALLBACK (skew_step_size_value_changed_callback), NULL);

  /* current drift display */
  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  label = gtk_label_new ("Current drift (µs): ");
  cur_drift_display_widget = gtk_label_new ("0");
  gtk_container_add (GTK_CONTAINER (hbox), label);
  gtk_container_add (GTK_CONTAINER (hbox), cur_drift_display_widget);
  gtk_container_add (GTK_CONTAINER (vbox), hbox);

#ifdef USE_RUNNING_AVERAGE
  /* average drift display */
  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  label = gtk_label_new ("Average drift (µs): ");
  avg_drift_display_widget = gtk_label_new ("0");
  gtk_container_add (GTK_CONTAINER (hbox), label);
  gtk_container_add (GTK_CONTAINER (hbox), avg_drift_display_widget);
  gtk_container_add (GTK_CONTAINER (vbox), hbox);
#endif

  gtk_widget_show_all (GTK_WIDGET (window));

  /* Start a timeout source that will repeatedly update the drift labels.
   * This is useful since the drift figures are changed constantly in the
   * custom_clock_slaving_callback. By updating in 50 ms intervals, it is
   * ensured that the UI is not updated too often, which otherwise may
   * use too much CPU%. */
  g_timeout_add (50, update_drift_labels, NULL);
}

/* Helper function to locate a suitable audio sink inside a bin
 * (including the pipeline, which is the top level bin). */
static GstElement *
get_audio_sink (GstElement * element)
{
  if (GST_IS_BIN (element)) {
    GstElement *sink = NULL;
    GstIterator *iter = gst_bin_iterate_sinks (GST_BIN (element));
    GValue item = G_VALUE_INIT;

    while (gst_iterator_next (iter, &item) == GST_ITERATOR_OK) {
      sink = GST_ELEMENT (g_value_get_object (&item));
      sink = get_audio_sink (sink);
      g_value_unset (&item);
      if (sink != NULL)
        break;
    }

    gst_iterator_free (iter);

    return sink;
  } else if (GST_IS_AUDIO_BASE_SINK (element)) {
    gst_object_ref (element);
    return element;
  } else {
    return NULL;
  }
}

static void
message_received (GstBus * bus, GstMessage * message, GstPipeline * pipeline)
{
  const GstStructure *s;
  GError *error = NULL;
  gchar *debug_info = NULL;
  gboolean do_quit = FALSE;
  GstObject *msg_src;
  const gchar *msg_src_name;

  msg_src = GST_MESSAGE_SRC (message);
  msg_src_name = msg_src ? GST_OBJECT_NAME (msg_src) : NULL;
  msg_src_name = GST_STR_NULL (msg_src_name);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_INFO:{
      gst_message_parse_info (message, &error, &debug_info);
      g_print ("Got info message from \"%s\": \"%s\" (debug info: \"%s\")\n",
          msg_src_name, error->message, debug_info);
      break;
    }

    case GST_MESSAGE_WARNING:{
      gst_message_parse_warning (message, &error, &debug_info);
      g_print ("Got warning message from \"%s\": \"%s\" (debug info: \"%s\")\n",
          msg_src_name, error->message, debug_info);
      break;
    }

    case GST_MESSAGE_ERROR:{
      gst_message_parse_error (message, &error, &debug_info);
      g_print ("Got error message from \"%s\": \"%s\" (debug info: \"%s\")\n",
          msg_src_name, error->message, debug_info);
      do_quit = TRUE;
      break;
    }

    case GST_MESSAGE_EOS:{
      g_print ("Got EOS message from \"%s\"\n", msg_src_name);
      do_quit = TRUE;
      break;
    }

    default:{
      s = gst_message_get_structure (message);
      g_print ("Got message from \"%s\" (%s): ", msg_src_name,
          gst_message_type_get_name (GST_MESSAGE_TYPE (message)));

      if (s) {
        gchar *sstr;

        sstr = gst_structure_to_string (s);
        g_print ("%s\n", sstr);
        g_free (sstr);
      } else {
        g_print ("no message details\n");
      }

      break;
    }
  }

  if (error != NULL)
    g_error_free (error);

  g_free (debug_info);

  if (do_quit)
    gtk_main_quit ();
}

static const gchar *
discont_reason_to_string (GstAudioBaseSinkDiscontReason discont_reason)
{
  switch (discont_reason) {
    case GST_AUDIO_BASE_SINK_DISCONT_REASON_NO_DISCONT:
      return "no discont";
    case GST_AUDIO_BASE_SINK_DISCONT_REASON_NEW_CAPS:
      return "new caps";
    case GST_AUDIO_BASE_SINK_DISCONT_REASON_FLUSH:
      return "flush";
    case GST_AUDIO_BASE_SINK_DISCONT_REASON_SYNC_LATENCY:
      return "sync latency";
    case GST_AUDIO_BASE_SINK_DISCONT_REASON_ALIGNMENT:
      return "alignment";
    case GST_AUDIO_BASE_SINK_DISCONT_REASON_DEVICE_FAILURE:
      return "device failure";
    default:
      return "<unknown>";
  }
}

static void
custom_clock_slaving_callback (GstAudioBaseSink * sink, GstClockTime etime,
    GstClockTime itime, GstClockTimeDiff * requested_skew,
    GstAudioBaseSinkDiscontReason discont_reason, gpointer user_data)
{
  GstClockTimeDiff drift;
  GstClockTimeDiff actual_skew_step_size;

  /* Synchronize access, since current_drift as well as drift_tolerance
   * and skew_step_size are accessed by this callback and by the
   * callbacks of the associated widgets, which run in the main thread.
   * This callback is called by a different thread. */
  g_mutex_lock (&state_mutex);

  /* According to the documentation, the only time the skew can be
   * set is when there is no discontinuity. In case of discontinuities,
   * the notion of a drift makes no sense, since a drift takes place
   * within continuous playback. For this reason, if a discontinuity
   * happens, just use 0 as drift figure and do not try to request
   * a skew (especially since the requested_skew pointer might be NULL
   * when a discontinuity happens). */
  if (discont_reason == GST_AUDIO_BASE_SINK_DISCONT_REASON_NO_DISCONT) {
    /* etime is the external clock time. The external clock is the
     * pipeline clock ("external" from the point of view of the audio sink).
     * itime is the internal clock (that is, the audio clock).
     *
     * etime > itime means the pipeline clock is faster than the audio clock.
     * itime < etime means the pipeline clock is slower than the audio clock.
     * etime == itime means both clocks are perfectly in sync, speed wise. */
    current_drift = GST_CLOCK_DIFF (itime, etime);

    /* Since the measured drift is prone to statistical noise, applying a
     * running average is generally useful. This is exactly what the default
     * skew algorithm in audiobasesink does. */
#ifdef USE_RUNNING_AVERAGE
    if (first_drift) {
      average_drift = current_drift;
      first_drift = FALSE;
    } else {
      average_drift = (31 * average_drift + current_drift) / 32;
    }
    drift = average_drift;
#else
    drift = current_drift;
#endif

    /* The default skew algorithm directly uses the running average drift for
     * skewing. In this example, this behavior is optional. As an alternative,
     * a fixed step size can be used, which can lead to more stable drift
     * compensation in some cases, but more audible clicks in others. Both
     * are available in this example to be able to experiment with this. */
    if (skew_step_size == 0)
      actual_skew_step_size = ABS (drift);
    else
      actual_skew_step_size = skew_step_size;

    /* Check if the drift exceeds the tolerance threshold. If it does, request
     * a skew. This will "skew" the playout pointer, effectively jumping within
     * the output by the requested amount. If the diff is positive, it means
     * that the pipeline clock is faster than the audio clock. The requested
     * skew needs to be negative then to effectively skip audio data, since
     * the audio clock's slower speed means that the audio sink is consuming
     * data slower than expected. If the diff is negative, it means the audio
     * clock is faster than the pipeline clock, so it is consuming data faster
     * than expected. The requested skew must then be positive to jump ahead
     * and produce null filler data for the audio sink. */
    if (ABS (drift) > drift_tolerance) {
      *requested_skew =
          (drift < 0) ? actual_skew_step_size : (-actual_skew_step_size);
#ifdef USE_RUNNING_AVERAGE
      /* Factor the requested skew into the average drift. Otherwise,
       * due to the running average's inertia, it will take some time for
       * the skew to be noticeable in this average drift quantity. */
      average_drift += *requested_skew;
#endif
      g_print ("Requesting skew by %" G_GINT64_FORMAT " ns ; "
          "pipeline clock time: %" GST_TIME_FORMAT " internal audio "
          "clock time: %" GST_TIME_FORMAT "\n", *requested_skew,
          GST_TIME_ARGS (etime), GST_TIME_ARGS (itime));
    }
  } else {
    /* In case of a discontinuity, just print when it happened (in pipeline
     * clock time) and the stated reason.
     *
     * Note that etime might be set to GST_CLOCK_TIME_NONE. This can happen at
     * the very beginning for example, when caps are first set. */
    if (GST_CLOCK_TIME_IS_VALID (etime)) {
      g_print ("Got discontinuity at pipeline clock time %" GST_TIME_FORMAT "; "
          "reason: %s\n", GST_TIME_ARGS (etime),
          discont_reason_to_string (discont_reason));
    } else {
      g_print ("Got discontinuity (no known pipeline clock time); reason: %s\n",
          discont_reason_to_string (discont_reason));
    }

    /* Statistical calculations like the moving average above need to be reset
     * here, since a discontinuity also means that any previous observations
     * are no longer usable. */
#ifdef USE_RUNNING_AVERAGE
    average_drift = 0;
    first_drift = TRUE;
#endif
  }

  g_mutex_unlock (&state_mutex);
}

int
main (int argc, char *argv[])
{
  GstElement *pipeline = NULL;

#ifndef GST_DISABLE_PARSE
  GError *error = NULL;
#endif
  GstElement *audiosink = NULL;
  GstBus *bus = NULL;
  GstClock *pipeline_clock = NULL;
  gint64 simulated_drift_ppm;
  gchar *ppm_str;
  gchar *ppm_str_endptr = NULL;

#ifdef GST_DISABLE_PARSE
  g_print ("GStreamer was built without pipeline parsing capabilities.\n");
  g_print
      ("Please rebuild GStreamer with pipeline parsing capabilities activated to use this example.\n");
  return 1;
#else
  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  if (argc < 2) {
    g_print ("Usage: %s <simulated clock drift in PPM> <pipeline>\n", argv[0]);
    g_print ("The PPM must be in the %d .. %d range\n", MIN_SIMULATED_DRIFT_PPM,
        MAX_SIMULATED_DRIFT_PPM);
    return 1;
  }

  ppm_str = argv[1];
  simulated_drift_ppm = g_ascii_strtoll (ppm_str, &ppm_str_endptr, 10);
  if (simulated_drift_ppm < MIN_SIMULATED_DRIFT_PPM ||
      simulated_drift_ppm > MAX_SIMULATED_DRIFT_PPM) {
    g_print ("PPM value %" G_GINT64_FORMAT " is outside of the valid range "
        "%d .. %d\n", simulated_drift_ppm, MIN_SIMULATED_DRIFT_PPM,
        MAX_SIMULATED_DRIFT_PPM);
    return 1;
  }
  if (ppm_str_endptr == ppm_str || *ppm_str_endptr != '\0') {
    g_print ("Got invalid PPM \"%s\"; first argument must be the PPM for "
        "the simulated clock drift\n", ppm_str);
    return 1;
  }

  pipeline = gst_parse_launchv ((const gchar **) &argv[2], &error);
  if (error) {
    g_print ("pipeline could not be constructed: %s\n", error->message);
    g_print ("Please give a complete pipeline with a GstAudioBaseSink"
        " based sink element (or a bin with such a sink inside).\n");
    g_print ("Example: audiotestsrc ! %s\n", DEFAULT_AUDIOSINK);
    g_error_free (error);
    return 1;
  }
#endif

  /* Set the pipeline to the READY state here. Some sink elements are not
   * actually configurable or usable with get_audio_sink () until they are
   * set to the READY state. autoaudiosink is one example of this - it
   * creates its internal audio sink only when reaching the READY state. */
  if (gst_element_set_state (pipeline, GST_STATE_READY) ==
      GST_STATE_CHANGE_FAILURE) {
    g_print ("Failed to set the pipeline to the READY state\n");
    goto cleanup;
  }

  audiosink = get_audio_sink (pipeline);
  if (audiosink == NULL) {
    g_print ("Please give a pipeline with a GstAudioBaseSink based sink"
        " in it (or a bin with such a sink inside).\n");
    return 1;
  }

  /* Setup custom clock slaving. */
  /* To be able to simulate clock drift behavior and demonstrate how the
   * custom clock slaving callback, don't let the audio sink provide
   * its audio clock to the pipeline, and also set the slave-method to "custom".
   * That way, the pipeline won't even try to pick the audio sink's clock,
   * and will use a different clock as its clock instead (see below).
   * Only then will the callback be invoked, since if the audio sink clock
   * is the pipeline clock, then there is no drift in the audio output. */
  gst_util_set_object_arg (G_OBJECT (audiosink), "slave-method", "custom");
  gst_util_set_object_arg (G_OBJECT (audiosink), "provide-clock", "false");
  gst_audio_base_sink_set_custom_slaving_callback (GST_AUDIO_BASE_SINK
      (audiosink), custom_clock_slaving_callback, NULL, NULL);

  /* Explicitly set the monotonic system clock as pipeline clock, and
   * calibrate it to be faster/slower by a certain PPM amount to be
   * able to better simulate clock drift behavior. */
  pipeline_clock = g_object_new (GST_TYPE_SYSTEM_CLOCK, "name",
      "CustomSystemClock", "clock-type", GST_CLOCK_TYPE_MONOTONIC, NULL);
  gst_clock_set_calibration (pipeline_clock, 0, 0,
      1000000 + simulated_drift_ppm, 1000000);
  gst_pipeline_use_clock (GST_PIPELINE (pipeline), pipeline_clock);

  g_print ("Using an extra simulated clock drift of %" G_GINT64_FORMAT " PPM\n",
      simulated_drift_ppm);

  /* Setup message handling. */
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);
  g_signal_connect (bus, "message", (GCallback) message_received, pipeline);

  /* Setup GUI. */
  setup_gui (audiosink);

  /* Go to main loop */
  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    g_print ("Failed to start pipeline\n");
  } else {
    gtk_main ();
  }

cleanup:
  /* Cleanup. */
  if (audiosink != NULL)
    gst_object_unref (audiosink);
  if (pipeline != NULL) {
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
  }
  if (bus != NULL) {
    gst_bus_remove_signal_watch (bus);
    gst_object_unref (bus);
  }
  if (pipeline_clock != NULL)
    gst_object_unref (pipeline_clock);

  /* Call thts to be able to use the GStreamer tracing framework. */
  gst_deinit ();

  return 0;
}
