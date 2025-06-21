/* GStreamer unit test for splitmuxsink elements
 *
 * Copyright (C) 2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2015 Jan Schmidt <jan@centricular.com>
 * Copyright (C) 2025 Stephane Cerveau <scerveau@igalia.com>
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
#  include "config.h"
#endif

#include <glib/gstdio.h>

#include <gst/check/gstcheck.h>
#include <gst/app/app.h>
#include <stdlib.h>

gchar *tmpdir = NULL;
GstClockTime first_ts;
GstClockTime last_ts;
gdouble current_rate;

static void
tempdir_setup (void)
{
  tmpdir = g_dir_make_tmp ("dashsink-test-XXXXXX", NULL);
  fail_if (tmpdir == NULL);
}

static void
tempdir_cleanup (void)
{
  GDir *d;
  const gchar *f;

  fail_if (tmpdir == NULL);

  d = g_dir_open (tmpdir, 0, NULL);
  fail_if (d == NULL);

  while ((f = g_dir_read_name (d)) != NULL) {
    gchar *fname = g_build_filename (tmpdir, f, NULL);
    fail_if (g_remove (fname) != 0, "Failed to remove tmp file %s", fname);
    g_free (fname);
  }
  g_dir_close (d);

  fail_if (g_remove (tmpdir) != 0, "Failed to delete tmpdir %s", tmpdir);

  g_free (tmpdir);
  tmpdir = NULL;
}

static guint
count_files (const gchar * target)
{
  GDir *d;
  const gchar *f;
  guint ret = 0;

  d = g_dir_open (target, 0, NULL);
  fail_if (d == NULL);

  while ((f = g_dir_read_name (d)) != NULL)
    ret++;
  g_dir_close (d);

  return ret;
}

static void
dump_error (GstMessage * msg)
{
  GError *err = NULL;
  gchar *dbg_info;

  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);

  gst_message_parse_error (msg, &err, &dbg_info);

  g_printerr ("ERROR from element %s: %s\n",
      GST_OBJECT_NAME (msg->src), err->message);
  g_printerr ("Debugging info: %s\n", (dbg_info) ? dbg_info : "none");
  g_error_free (err);
  g_free (dbg_info);
}

static GstMessage *
run_pipeline (GstElement * pipeline, guint num_segments_expected,
    const GstClockTime * segment_durations)
{
  GstBus *bus = gst_element_get_bus (GST_ELEMENT (pipeline));
  GstMessage *msg;
  guint segments_seen = 0;

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  do {
    msg =
        gst_bus_poll (bus,
        GST_MESSAGE_EOS | GST_MESSAGE_ERROR | GST_MESSAGE_ELEMENT, -1);
    if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS
        || GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
      break;
    }
    if (num_segments_expected != 0) {
      // Handle dashsink element message
      const GstStructure *s = gst_message_get_structure (msg);
      if (gst_structure_has_name (s, "dashsink-new-segment")) {
        GstClockTime segment_duration;
        guint segment_id;
        fail_unless (gst_structure_get_uint (s, "segment-id", &segment_id));
        fail_unless (segment_id < num_segments_expected);
        fail_unless (gst_structure_get_clock_time (s, "duration",
                &segment_duration));

        if (segment_durations != NULL) {
          fail_unless (segment_durations[segment_id] == segment_duration,
              "Expected duration %" GST_TIME_FORMAT
              " for fragment %u. Got duration %" GST_TIME_FORMAT,
              GST_TIME_ARGS (segment_durations[segment_id]),
              segment_id, GST_TIME_ARGS (segment_duration));
        }
        segments_seen++;
      }
    }
    gst_message_unref (msg);
  } while (TRUE);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (bus);

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR)
    dump_error (msg);
  else if (num_segments_expected != 0) {
    // Success. Check we got the expected number of fragment messages
    assert_equals_uint64 (segments_seen, num_segments_expected);
  }

  return msg;
}

static void
seek_pipeline (GstElement * pipeline, gdouble rate, GstClockTime start,
    GstClockTime end)
{
  /* Pause the pipeline, seek to the desired range / rate, wait for PAUSED again, then
   * clear the tracking vars for start_ts / end_ts */
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  /* specific end time not implemented: */
  fail_unless (end == GST_CLOCK_TIME_NONE);

  gst_element_seek (pipeline, rate, GST_FORMAT_TIME,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, GST_SEEK_TYPE_SET, start,
      GST_SEEK_TYPE_END, 0);

  /* Wait for the pipeline to preroll again */
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  GST_LOG ("Seeked pipeline. Rate %f time range %" GST_TIME_FORMAT " to %"
      GST_TIME_FORMAT, rate, GST_TIME_ARGS (start), GST_TIME_ARGS (end));

  /* Clear tracking variables now that the seek is complete */
  first_ts = last_ts = GST_CLOCK_TIME_NONE;
  current_rate = rate;
};

static GstFlowReturn
receive_sample (GstAppSink * appsink, gpointer user_data)
{
  GstSample *sample;
  GstSegment *seg;
  GstBuffer *buf;
  GstClockTime start;
  GstClockTime end;

  g_signal_emit_by_name (appsink, "pull-sample", &sample);
  fail_unless (sample != NULL);

  seg = gst_sample_get_segment (sample);
  fail_unless (seg != NULL);

  buf = gst_sample_get_buffer (sample);
  fail_unless (buf != NULL);

  GST_LOG ("Got buffer %" GST_PTR_FORMAT, buf);

  start = GST_BUFFER_PTS (buf);
  end = start;

  if (GST_CLOCK_TIME_IS_VALID (start))
    start = gst_segment_to_stream_time (seg, GST_FORMAT_TIME, start);

  if (GST_CLOCK_TIME_IS_VALID (end)) {
    if (GST_BUFFER_DURATION_IS_VALID (buf))
      end += GST_BUFFER_DURATION (buf);

    end = gst_segment_to_stream_time (seg, GST_FORMAT_TIME, end);
  }

  GST_DEBUG ("Got buffer stream time %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (start), GST_TIME_ARGS (end));

  /* Check time is moving in the right direction */
  if (current_rate > 0) {
    if (GST_CLOCK_TIME_IS_VALID (first_ts))
      fail_unless (start >= first_ts,
          "Timestamps went backward during forward play, %" GST_TIME_FORMAT
          " < %" GST_TIME_FORMAT, GST_TIME_ARGS (start),
          GST_TIME_ARGS (first_ts));
    if (GST_CLOCK_TIME_IS_VALID (last_ts))
      fail_unless (end >= last_ts,
          "Timestamps went backward during forward play, %" GST_TIME_FORMAT
          " < %" GST_TIME_FORMAT, GST_TIME_ARGS (end), GST_TIME_ARGS (last_ts));
  } else {
    fail_unless (start <= first_ts,
        "Timestamps went forward during reverse play, %" GST_TIME_FORMAT " > %"
        GST_TIME_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (first_ts));
    fail_unless (end <= last_ts,
        "Timestamps went forward during reverse play, %" GST_TIME_FORMAT " > %"
        GST_TIME_FORMAT, GST_TIME_ARGS (end), GST_TIME_ARGS (last_ts));
  }

  /* update the range of timestamps we've encountered */
  if (!GST_CLOCK_TIME_IS_VALID (first_ts) || start < first_ts)
    first_ts = start;
  if (!GST_CLOCK_TIME_IS_VALID (last_ts) || end > last_ts)
    last_ts = end;

  gst_sample_unref (sample);

  if (user_data) {
    guint *num_frame = (guint *) user_data;

    *num_frame = *num_frame + 1;
  }

  return GST_FLOW_OK;
}

static void
test_playback (const gchar * filename, GstClockTime exp_first_time,
    GstClockTime exp_last_time,
    guint num_segments_expected, const GstClockTime * segment_durations)
{
  GstMessage *msg;
  GstElement *pipeline;
  GstElement *appsink;
  GstElement *fakesink;
  GstAppSinkCallbacks callbacks = { NULL };
  gchar *uri;

  GST_DEBUG ("Playing back file %s", filename);

  pipeline = gst_element_factory_make ("playbin", NULL);
  fail_if (pipeline == NULL);

  appsink = gst_element_factory_make ("appsink", NULL);
  fail_if (appsink == NULL);

  /*Full speed playback */
  g_object_set (G_OBJECT (appsink), "sync", FALSE, NULL);

  g_object_set (G_OBJECT (pipeline), "video-sink", appsink, NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);
  fail_if (fakesink == NULL);
  g_object_set (G_OBJECT (pipeline), "audio-sink", fakesink, NULL);

  uri = g_strdup_printf ("file://%s", filename);

  g_object_set (G_OBJECT (pipeline), "uri", uri, NULL);
  g_free (uri);

  callbacks.new_sample = receive_sample;
  gst_app_sink_set_callbacks (GST_APP_SINK (appsink), &callbacks, NULL, NULL);

  /* test forwards */
  seek_pipeline (pipeline, 1.0, 0, -1);
  fail_unless (first_ts == GST_CLOCK_TIME_NONE);
  msg = run_pipeline (pipeline, 0, NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  /* Check we saw the entire range of values */
  fail_unless (first_ts == exp_first_time,
      "Expected start of playback range %" GST_TIME_FORMAT ", got %"
      GST_TIME_FORMAT, GST_TIME_ARGS (exp_first_time),
      GST_TIME_ARGS (first_ts));
  fail_unless (last_ts == exp_last_time,
      "Expected end of playback range %" GST_TIME_FORMAT ", got %"
      GST_TIME_FORMAT, GST_TIME_ARGS (exp_last_time), GST_TIME_ARGS (last_ts));

  gst_object_unref (pipeline);
}

GST_START_TEST (test_dashsink_video_ts)
{
  GstMessage *msg;
  GstElement *pipeline;
  GstElement *sink;
  guint count;
  gchar *filename;

  /* This pipeline has a small time cutoff - it should start a new file
   * every GOP, ie 1 second */
  pipeline =
      gst_parse_launch
      ("dashsink name=dashsink videotestsrc num-buffers=15 ! video/x-raw,width=80,height=64,framerate=5/1 ! openh264enc ! dashsink.video_0",
      NULL);
  fail_if (pipeline == NULL);
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "dashsink");
  fail_if (sink == NULL);

  g_object_set (G_OBJECT (sink), "mpd-root-path", tmpdir, NULL);
  g_object_set (G_OBJECT (sink), "target-duration", 1, NULL);
  g_object_set (G_OBJECT (sink), "use-segment-list", TRUE, NULL);

  g_object_unref (sink);

  GstClockTime durations[] = { GST_SECOND, GST_SECOND, GST_SECOND };
  msg = run_pipeline (pipeline, 3, durations);

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR)
    dump_error (msg);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_object_unref (pipeline);

  count = count_files (tmpdir);
  fail_unless (count == 4, "Expected 4 output files, got %d", count);

  filename = g_build_filename (tmpdir, "dash.mpd", NULL);
  // mpegtsmux generates a first PTS at 0.125 second and does not end at 3 seconds exactly.
  test_playback (filename, 0.125 * GST_SECOND, 2.925 * GST_SECOND, 3,
      durations);
  g_free (filename);
}

GST_END_TEST;


static Suite *
dashsink_suite (void)
{
  Suite *s = suite_create ("dashsink");
  TCase *tc_chain = tcase_create ("general");

  gboolean have_h264;

  /* we assume that if encoder/muxer are there, decoder/demuxer will be a well */
  have_h264 = gst_registry_check_feature_version (gst_registry_get (),
      "openh264enc", GST_VERSION_MAJOR, GST_VERSION_MINOR, 0);

  suite_add_tcase (s, tc_chain);

  if (have_h264) {
    tcase_add_checked_fixture (tc_chain, tempdir_setup, tempdir_cleanup);
    tcase_add_test (tc_chain, test_dashsink_video_ts);
  } else {
    GST_INFO ("Skipping tests, missing plugins: openh264enc");
  }

  return s;
}

GST_CHECK_MAIN (dashsink);
