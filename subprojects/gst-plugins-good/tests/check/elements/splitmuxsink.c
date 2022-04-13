/* GStreamer unit test for splitmuxsink elements
 *
 * Copyright (C) 2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2015 Jan Schmidt <jan@centricular.com>
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
#include <gst/video/video.h>
#include <stdlib.h>

gchar *tmpdir = NULL;
GstClockTime first_ts;
GstClockTime last_ts;
gdouble current_rate;

static void
tempdir_setup (void)
{
  const gchar *systmp = g_get_tmp_dir ();
  tmpdir = g_build_filename (systmp, "splitmux-test-XXXXXX", NULL);
  /* Rewrites tmpdir template input: */
  tmpdir = g_mkdtemp (tmpdir);
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
run_pipeline (GstElement * pipeline)
{
  GstBus *bus = gst_element_get_bus (GST_ELEMENT (pipeline));
  GstMessage *msg;

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (bus);

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR)
    dump_error (msg);

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
test_playback (const gchar * in_pattern, GstClockTime exp_first_time,
    GstClockTime exp_last_time, gboolean test_reverse)
{
  GstMessage *msg;
  GstElement *pipeline;
  GstElement *appsink;
  GstElement *fakesink2;
  GstAppSinkCallbacks callbacks = { NULL };
  gchar *uri;

  GST_DEBUG ("Playing back files matching %s", in_pattern);

  pipeline = gst_element_factory_make ("playbin", NULL);
  fail_if (pipeline == NULL);

  appsink = gst_element_factory_make ("appsink", NULL);
  fail_if (appsink == NULL);
  g_object_set (G_OBJECT (appsink), "sync", FALSE, NULL);

  g_object_set (G_OBJECT (pipeline), "video-sink", appsink, NULL);
  fakesink2 = gst_element_factory_make ("fakesink", NULL);
  fail_if (fakesink2 == NULL);
  g_object_set (G_OBJECT (pipeline), "audio-sink", fakesink2, NULL);

  uri = g_strdup_printf ("splitmux://%s", in_pattern);

  g_object_set (G_OBJECT (pipeline), "uri", uri, NULL);
  g_free (uri);

  callbacks.new_sample = receive_sample;
  gst_app_sink_set_callbacks (GST_APP_SINK (appsink), &callbacks, NULL, NULL);

  /* test forwards */
  seek_pipeline (pipeline, 1.0, 0, -1);
  fail_unless (first_ts == GST_CLOCK_TIME_NONE);
  msg = run_pipeline (pipeline);
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

  if (test_reverse) {
    /* Test backwards */
    seek_pipeline (pipeline, -1.0, 0, -1);
    msg = run_pipeline (pipeline);
    fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
    gst_message_unref (msg);
    /* Check we saw the entire range of values */
    fail_unless (first_ts == exp_first_time,
        "Expected start of playback range %" GST_TIME_FORMAT
        ", got %" GST_TIME_FORMAT, GST_TIME_ARGS (exp_first_time),
        GST_TIME_ARGS (first_ts));
    fail_unless (last_ts == exp_last_time,
        "Expected end of playback range %" GST_TIME_FORMAT
        ", got %" GST_TIME_FORMAT, GST_TIME_ARGS (exp_last_time),
        GST_TIME_ARGS (last_ts));
  }

  gst_object_unref (pipeline);
}

struct splitmux_location_state
{
  GstElement *splitmuxsink;
  gboolean got_format_location;
  gboolean fragment_opened;
  gchar *current_location;
};

static gchar *
check_format_location (GstElement * object,
    guint fragment_id, GstSample * first_sample,
    struct splitmux_location_state *location_state)
{
  GstBuffer *buf = gst_sample_get_buffer (first_sample);

  /* Must have a buffer */
  fail_if (buf == NULL);
  GST_LOG ("New file - first buffer %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  if (location_state) {
    fail_unless (location_state->got_format_location == FALSE,
        "Got format-location signal twice without an intervening splitmuxsink-fragment-closed");
    location_state->got_format_location = TRUE;
  }

  return NULL;
}

static GstBusSyncReply
bus_sync_handler (GstBus * bus, GstMessage * message,
    struct splitmux_location_state *location_state)
{
  switch (message->type) {
    case GST_MESSAGE_ELEMENT:
    {
      const GstStructure *s = gst_message_get_structure (message);
      if (message->src == GST_OBJECT_CAST (location_state->splitmuxsink)) {
        if (gst_structure_has_name (s, "splitmuxsink-fragment-opened")) {
          const gchar *location = gst_structure_get_string (s, "location");
          fail_unless (location != NULL);
          fail_unless (location_state->got_format_location == TRUE,
              "Failed to get format-location before fragment start");
          fail_unless (location_state->fragment_opened == FALSE);
          location_state->fragment_opened = TRUE;

          /* The location must be different to last time */
          fail_unless (location_state->current_location == NULL
              || !g_str_equal (location_state->current_location, location));
          g_free (location_state->current_location);
          location_state->current_location = g_strdup (location);

        } else if (gst_structure_has_name (s, "splitmuxsink-fragment-closed")) {
          fail_unless (location_state->got_format_location == TRUE);
          fail_unless (location_state->fragment_opened == TRUE);
          location_state->got_format_location = FALSE;  /* We need another format-location before the next open */
          location_state->fragment_opened = FALSE;
        }
      }
      break;
    }
    default:
      break;
  }

  return GST_BUS_PASS;
}

GST_START_TEST (test_splitmuxsink)
{
  GstMessage *msg;
  GstElement *pipeline;
  GstElement *sink;
  GstPad *splitmux_sink_pad;
  GstPad *enc_src_pad;
  gchar *dest_pattern;
  guint count;
  gchar *in_pattern;
  struct splitmux_location_state location_state = { NULL, FALSE, FALSE, NULL };
  GstBus *bus;

  /* This pipeline has a small time cutoff - it should start a new file
   * every GOP, ie 1 second */
  pipeline =
      gst_parse_launch
      ("videotestsrc num-buffers=15 ! video/x-raw,width=80,height=64,framerate=5/1 ! videoconvert !"
      " queue ! theoraenc keyframe-force=5 ! splitmuxsink name=splitsink "
      " max-size-time=1000000 max-size-bytes=1000000 muxer=oggmux", NULL);
  fail_if (pipeline == NULL);
  location_state.splitmuxsink = sink =
      gst_bin_get_by_name (GST_BIN (pipeline), "splitsink");
  fail_if (sink == NULL);
  g_signal_connect (sink, "format-location-full",
      (GCallback) check_format_location, &location_state);

  dest_pattern = g_build_filename (tmpdir, "out%05d.ogg", NULL);
  g_object_set (G_OBJECT (sink), "location", dest_pattern, NULL);
  g_free (dest_pattern);
  g_object_unref (sink);

  bus = gst_element_get_bus (pipeline);
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_handler,
      &location_state, NULL);
  gst_object_unref (bus);

  msg = run_pipeline (pipeline);

  /* Clean up the location state */
  g_free (location_state.current_location);

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR)
    dump_error (msg);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  /* unlink manually and release request pad to ensure that we *can* do that
   * - https://bugzilla.gnome.org/show_bug.cgi?id=753622 */
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "splitsink");
  fail_if (sink == NULL);
  splitmux_sink_pad = gst_element_get_static_pad (sink, "video");
  fail_if (splitmux_sink_pad == NULL);
  enc_src_pad = gst_pad_get_peer (splitmux_sink_pad);
  fail_if (enc_src_pad == NULL);
  fail_unless (gst_pad_unlink (enc_src_pad, splitmux_sink_pad));
  gst_object_unref (enc_src_pad);
  gst_element_release_request_pad (sink, splitmux_sink_pad);
  gst_object_unref (splitmux_sink_pad);
  /* at this point the pad must be released - try to find it again to verify */
  splitmux_sink_pad = gst_element_get_static_pad (sink, "video");
  fail_if (splitmux_sink_pad != NULL);
  g_object_unref (sink);

  gst_object_unref (pipeline);

  count = count_files (tmpdir);
  fail_unless (count == 3, "Expected 3 output files, got %d", count);

  in_pattern = g_build_filename (tmpdir, "out*.ogg", NULL);
  test_playback (in_pattern, 0, 3 * GST_SECOND, TRUE);
  g_free (in_pattern);
}

GST_END_TEST;

GST_START_TEST (test_splitmuxsink_clean_failure)
{
  GstMessage *msg;
  GstElement *pipeline;
  GstElement *sink, *fakesink;

  /* This pipeline has a small time cutoff - it should start a new file
   * every GOP, ie 1 second */
  pipeline =
      gst_parse_launch
      ("videotestsrc horizontal-speed=2 is-live=true ! video/x-raw,width=80,height=64,framerate=5/1 ! videoconvert !"
      " queue ! theoraenc keyframe-force=5 ! splitmuxsink name=splitsink "
      " max-size-time=1000000 max-size-bytes=1000000 muxer=oggmux", NULL);
  fail_if (pipeline == NULL);
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "splitsink");
  fail_if (sink == NULL);

  fakesink = gst_element_factory_make ("fakesink", "fakesink-fail");
  fail_if (fakesink == NULL);

  /* Trigger an error on READY->PAUSED */
  g_object_set (fakesink, "state-error", 2, NULL);
  g_object_set (sink, "sink", fakesink, NULL);
  gst_object_unref (sink);

  msg = run_pipeline (pipeline);

  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
  gst_message_unref (msg);

  fail_unless (gst_element_set_state (pipeline,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_splitmuxsink_multivid)
{
  GstMessage *msg;
  GstElement *pipeline;
  GstElement *sink;
  gchar *dest_pattern;
  guint count;
  gchar *in_pattern;

  /* This pipeline should start a new file every GOP, ie 1 second,
   * driven by the primary video stream and with 2 auxiliary video streams */
  pipeline =
      gst_parse_launch
      ("splitmuxsink name=splitsink "
      " max-size-time=1000000 max-size-bytes=1000000 muxer=qtmux "
      "videotestsrc num-buffers=15 ! video/x-raw,width=80,height=64,framerate=5/1 ! videoconvert !"
      " queue ! vp8enc keyframe-max-dist=5 ! splitsink.video "
      "videotestsrc num-buffers=15 pattern=snow ! video/x-raw,width=80,height=64,framerate=5/1 ! videoconvert !"
      " queue ! vp8enc keyframe-max-dist=6 ! splitsink.video_aux_0 "
      "videotestsrc num-buffers=15 pattern=ball ! video/x-raw,width=80,height=64,framerate=5/1 ! videoconvert !"
      " queue ! vp8enc keyframe-max-dist=8 ! splitsink.video_aux_1 ", NULL);
  fail_if (pipeline == NULL);
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "splitsink");
  fail_if (sink == NULL);
  g_signal_connect (sink, "format-location-full",
      (GCallback) check_format_location, NULL);
  dest_pattern = g_build_filename (tmpdir, "out%05d.m4v", NULL);
  g_object_set (G_OBJECT (sink), "location", dest_pattern, NULL);
  g_free (dest_pattern);
  g_object_unref (sink);

  msg = run_pipeline (pipeline);

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR)
    dump_error (msg);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_object_unref (pipeline);

  count = count_files (tmpdir);
  fail_unless (count == 3, "Expected 3 output files, got %d", count);

  in_pattern = g_build_filename (tmpdir, "out*.m4v", NULL);
  /* FIXME: Reverse playback works poorly with multiple video streams
   * in qtdemux (at least, maybe other demuxers) at the time this was
   * written, and causes test failures like buffers being output
   * multiple times by qtdemux as it loops through GOPs. Disable that
   * for now */
  test_playback (in_pattern, 0, 3 * GST_SECOND, FALSE);
  g_free (in_pattern);
}

GST_END_TEST;

GST_START_TEST (test_splitmuxsink_async)
{
  GstMessage *msg;
  GstElement *pipeline;
  GstElement *sink;
  GstPad *splitmux_sink_pad;
  GstPad *enc_src_pad;
  gchar *dest_pattern;
  guint count;
  gchar *in_pattern;

  pipeline =
      gst_parse_launch
      ("videotestsrc num-buffers=15 ! video/x-raw,width=80,height=64,framerate=5/1 ! videoconvert !"
      " queue ! theoraenc keyframe-force=5 ! splitmuxsink name=splitsink "
      " max-size-time=1000000000 async-finalize=true "
      " muxer-factory=matroskamux audiotestsrc num-buffers=15 samplesperbuffer=9600 ! "
      " audio/x-raw,rate=48000 ! splitsink.audio_%u", NULL);
  fail_if (pipeline == NULL);
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "splitsink");
  fail_if (sink == NULL);
  g_signal_connect (sink, "format-location-full",
      (GCallback) check_format_location, NULL);
  dest_pattern = g_build_filename (tmpdir, "matroska%05d.mkv", NULL);
  g_object_set (G_OBJECT (sink), "location", dest_pattern, NULL);
  g_free (dest_pattern);
  g_object_unref (sink);

  msg = run_pipeline (pipeline);

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR)
    dump_error (msg);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  /* unlink manually and release request pad to ensure that we *can* do that
   * - https://bugzilla.gnome.org/show_bug.cgi?id=753622 */
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "splitsink");
  fail_if (sink == NULL);
  splitmux_sink_pad = gst_element_get_static_pad (sink, "video");
  fail_if (splitmux_sink_pad == NULL);
  enc_src_pad = gst_pad_get_peer (splitmux_sink_pad);
  fail_if (enc_src_pad == NULL);
  fail_unless (gst_pad_unlink (enc_src_pad, splitmux_sink_pad));
  gst_object_unref (enc_src_pad);
  gst_element_release_request_pad (sink, splitmux_sink_pad);
  gst_object_unref (splitmux_sink_pad);
  /* at this point the pad must be released - try to find it again to verify */
  splitmux_sink_pad = gst_element_get_static_pad (sink, "video");
  fail_if (splitmux_sink_pad != NULL);
  g_object_unref (sink);

  gst_object_unref (pipeline);

  count = count_files (tmpdir);
  fail_unless (count == 3, "Expected 3 output files, got %d", count);

  in_pattern = g_build_filename (tmpdir, "matroska*.mkv", NULL);
  test_playback (in_pattern, 0, 3 * GST_SECOND, TRUE);
  g_free (in_pattern);
}

GST_END_TEST;

/* For verifying bug https://bugzilla.gnome.org/show_bug.cgi?id=762893 */
GST_START_TEST (test_splitmuxsink_reuse_simple)
{
  GstElement *sink;
  GstPad *pad;

  sink = gst_element_factory_make ("splitmuxsink", NULL);
  pad = gst_element_request_pad_simple (sink, "video");
  fail_unless (pad != NULL);
  g_object_set (sink, "location", "/dev/null", NULL);

  fail_unless (gst_element_set_state (sink,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_ASYNC);
  fail_unless (gst_element_set_state (sink,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);
  fail_unless (gst_element_set_state (sink,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_ASYNC);
  fail_unless (gst_element_set_state (sink,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);

  gst_element_release_request_pad (sink, pad);
  gst_object_unref (pad);
  gst_object_unref (sink);
}

GST_END_TEST;

GST_START_TEST (test_splitmuxsink_muxer_pad_map)
{
  GstElement *sink, *muxer;
  GstPad *muxpad;
  GstPad *pad1 = NULL, *pad2 = NULL;
  GstStructure *pad_map;

  pad_map = gst_structure_new ("x-pad-map",
      "video", G_TYPE_STRING, "video_100",
      "audio_0", G_TYPE_STRING, "audio_101", NULL);

  muxer = gst_element_factory_make ("qtmux", NULL);
  fail_if (muxer == NULL);
  sink = gst_element_factory_make ("splitmuxsink", NULL);
  fail_if (sink == NULL);

  g_object_set (sink, "muxer", muxer, "muxer-pad-map", pad_map, NULL);
  gst_structure_free (pad_map);

  pad1 = gst_element_request_pad_simple (sink, "video");
  fail_unless (g_str_equal ("video", GST_PAD_NAME (pad1)));
  muxpad = gst_element_get_static_pad (muxer, "video_100");
  fail_unless (muxpad != NULL);
  gst_object_unref (muxpad);

  pad2 = gst_element_request_pad_simple (sink, "audio_0");
  fail_unless (g_str_equal ("audio_0", GST_PAD_NAME (pad2)));
  muxpad = gst_element_get_static_pad (muxer, "audio_101");
  fail_unless (muxpad != NULL);
  gst_object_unref (muxpad);

  g_object_set (sink, "location", "/dev/null", NULL);

  fail_unless (gst_element_set_state (sink,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_ASYNC);
  fail_unless (gst_element_set_state (sink,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);

  gst_element_release_request_pad (sink, pad1);
  gst_object_unref (pad1);
  gst_element_release_request_pad (sink, pad2);
  gst_object_unref (pad2);
  gst_object_unref (sink);
}

GST_END_TEST;

static void
run_eos_pipeline (guint num_video_buf, guint num_audio_buf,
    gboolean configure_audio)
{
  GstMessage *msg;
  GstElement *pipeline;
  gchar *dest_pattern;
  gchar *pipeline_str;
  gchar *audio_branch = NULL;

  dest_pattern = g_build_filename (tmpdir, "out%05d.mp4", NULL);

  if (configure_audio) {
    audio_branch = g_strdup_printf ("audiotestsrc num-buffers=%d ! "
        "splitsink.audio_0", num_audio_buf);
  }

  pipeline_str = g_strdup_printf ("splitmuxsink name=splitsink location=%s "
      "muxer-factory=qtmux videotestsrc num-buffers=%d ! jpegenc ! splitsink. "
      "%s", dest_pattern, num_video_buf, audio_branch ? audio_branch : "");
  pipeline = gst_parse_launch (pipeline_str, NULL);
  g_free (dest_pattern);
  g_free (audio_branch);
  g_free (pipeline_str);

  fail_if (pipeline == NULL);

  msg = run_pipeline (pipeline);

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR)
    dump_error (msg);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_object_unref (pipeline);
}

GST_START_TEST (test_splitmuxsink_eos_without_buffer)
{
  /* below pipelines will create non-playable files but at least we should not
   * crash */
  run_eos_pipeline (0, 0, FALSE);
  run_eos_pipeline (0, 0, TRUE);
  run_eos_pipeline (1, 0, TRUE);
  run_eos_pipeline (0, 1, TRUE);
}

GST_END_TEST;

static GstPadProbeReturn
count_upstrea_fku (GstPad * pad, GstPadProbeInfo * info,
    guint * upstream_fku_count)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_UPSTREAM:
      if (gst_video_event_is_force_key_unit (event))
        *upstream_fku_count += 1;
      break;
    default:
      break;
  }

  return GST_PAD_PROBE_OK;
}

static void
splitmuxsink_split_by_keyframe (gboolean send_keyframe_request,
    guint max_size_time_sec, guint encoder_key_interval_sec)
{
  GstMessage *msg;
  GstElement *pipeline;
  GstElement *sink;
  GstElement *enc;
  GstPad *srcpad;
  gchar *pipeline_str;
  gchar *dest_pattern;
  guint count;
  guint expected_count;
  gchar *in_pattern;
  guint upstream_fku_count = 0;
  guint expected_fku_count;

  pipeline_str = g_strdup_printf ("splitmuxsink name=splitsink "
      "max-size-time=%" G_GUINT64_FORMAT
      " send-keyframe-requests=%s muxer=qtmux "
      "videotestsrc num-buffers=30 ! video/x-raw,width=80,height=64,framerate=5/1 "
      "! videoconvert ! queue ! vp8enc name=enc keyframe-max-dist=%d ! splitsink.video ",
      max_size_time_sec * GST_SECOND, send_keyframe_request ? "true" : "false",
      encoder_key_interval_sec * 5);

  pipeline = gst_parse_launch (pipeline_str, NULL);
  g_free (pipeline_str);

  fail_if (pipeline == NULL);
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "splitsink");
  fail_if (sink == NULL);
  g_signal_connect (sink, "format-location-full",
      (GCallback) check_format_location, NULL);
  dest_pattern = g_build_filename (tmpdir, "out%05d.m4v", NULL);
  g_object_set (G_OBJECT (sink), "location", dest_pattern, NULL);
  g_free (dest_pattern);
  g_object_unref (sink);

  enc = gst_bin_get_by_name (GST_BIN (pipeline), "enc");
  fail_if (enc == NULL);
  srcpad = gst_element_get_static_pad (enc, "src");
  fail_if (srcpad == NULL);

  gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
      (GstPadProbeCallback) count_upstrea_fku, &upstream_fku_count, NULL);
  gst_object_unref (srcpad);
  gst_object_unref (enc);

  msg = run_pipeline (pipeline);

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR)
    dump_error (msg);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_object_unref (pipeline);

  count = count_files (tmpdir);
  expected_count = 6 / max_size_time_sec;
  fail_unless (count == expected_count,
      "Expected %d output files, got %d", expected_count, count);

  if (!send_keyframe_request) {
    expected_fku_count = 0;
  } else {
    expected_fku_count = count;
  }

  GST_INFO ("Upstream force keyunit event count %d", upstream_fku_count);

  fail_unless (upstream_fku_count == expected_fku_count,
      "Expected upstream force keyunit event count %d, got %d",
      expected_fku_count, upstream_fku_count);

  in_pattern = g_build_filename (tmpdir, "out*.m4v", NULL);
  /* FIXME: Reverse playback works poorly with multiple video streams
   * in qtdemux (at least, maybe other demuxers) at the time this was
   * written, and causes test failures like buffers being output
   * multiple times by qtdemux as it loops through GOPs. Disable that
   * for now */
  test_playback (in_pattern, 0, 6 * GST_SECOND, FALSE);
  g_free (in_pattern);
}

GST_START_TEST (test_splitmuxsink_without_keyframe_request)
{
  /* This encoding option is intended to produce keyframe per 1 seconds
   * but splitmuxsink will split file per 2 second without keyframe request */
  splitmuxsink_split_by_keyframe (FALSE, 2, 1);
}

GST_END_TEST;

GST_START_TEST (test_splitmuxsink_keyframe_request)
{
  /* This encoding option is intended to produce keyframe per 2 seconds
   * and splitmuxsink will request keyframe per 2 seconds as well.
   * This should produce 2 seconds long files */
  splitmuxsink_split_by_keyframe (TRUE, 2, 2);
}

GST_END_TEST;

GST_START_TEST (test_splitmuxsink_keyframe_request_more)
{
  /* This encoding option is intended to produce keyframe per 2 seconds
   * but splitmuxsink will request keyframe per 1 second. This should produce
   * 1 second long files */
  splitmuxsink_split_by_keyframe (TRUE, 1, 2);
}

GST_END_TEST;

GST_START_TEST (test_splitmuxsink_keyframe_request_less)
{
  /* This encoding option is intended to produce keyframe per 1 second
   * but splitmuxsink will request keyframe per 2 seconds. This should produce
   * 2 seconds long files */
  splitmuxsink_split_by_keyframe (TRUE, 2, 1);
}

GST_END_TEST;

static Suite *
splitmuxsink_suite (void)
{
  Suite *s = suite_create ("splitmuxsink");
  TCase *tc_chain = tcase_create ("general");
  TCase *tc_chain_basic = tcase_create ("basic");
  TCase *tc_chain_complex = tcase_create ("complex");
  TCase *tc_chain_mp4_jpeg = tcase_create ("caps_change");
  TCase *tc_chain_keyframe_request = tcase_create ("keyframe_request");
  gboolean have_theora, have_ogg, have_vorbis, have_matroska, have_qtmux,
      have_jpeg, have_vp8;

  /* we assume that if encoder/muxer are there, decoder/demuxer will be a well */
  have_theora = gst_registry_check_feature_version (gst_registry_get (),
      "theoraenc", GST_VERSION_MAJOR, GST_VERSION_MINOR, 0);
  have_ogg = gst_registry_check_feature_version (gst_registry_get (),
      "oggmux", GST_VERSION_MAJOR, GST_VERSION_MINOR, 0);
  have_vorbis = gst_registry_check_feature_version (gst_registry_get (),
      "vorbisenc", GST_VERSION_MAJOR, GST_VERSION_MINOR, 0);
  have_matroska = gst_registry_check_feature_version (gst_registry_get (),
      "matroskamux", GST_VERSION_MAJOR, GST_VERSION_MINOR, 0);
  have_qtmux = gst_registry_check_feature_version (gst_registry_get (),
      "qtmux", GST_VERSION_MAJOR, GST_VERSION_MINOR, 0);
  have_jpeg = gst_registry_check_feature_version (gst_registry_get (),
      "jpegenc", GST_VERSION_MAJOR, GST_VERSION_MINOR, 0);
  have_vp8 = gst_registry_check_feature_version (gst_registry_get (),
      "vp8enc", GST_VERSION_MAJOR, GST_VERSION_MINOR, 0);

  suite_add_tcase (s, tc_chain);
  suite_add_tcase (s, tc_chain_basic);
  suite_add_tcase (s, tc_chain_complex);
  suite_add_tcase (s, tc_chain_mp4_jpeg);
  suite_add_tcase (s, tc_chain_keyframe_request);

  tcase_add_test (tc_chain_basic, test_splitmuxsink_reuse_simple);

  if (have_theora && have_ogg) {
    tcase_add_checked_fixture (tc_chain, tempdir_setup, tempdir_cleanup);

    tcase_add_test (tc_chain, test_splitmuxsink);
    tcase_add_test (tc_chain, test_splitmuxsink_clean_failure);

    if (have_matroska && have_vorbis) {
      tcase_add_checked_fixture (tc_chain_complex, tempdir_setup,
          tempdir_cleanup);

      tcase_add_test (tc_chain, test_splitmuxsink_async);
    } else {
      GST_INFO ("Skipping tests, missing plugins: matroska and/or vorbis");
    }
  } else {
    GST_INFO ("Skipping tests, missing plugins: theora and/or ogg");
  }


  if (have_qtmux && have_jpeg) {
    tcase_add_checked_fixture (tc_chain_mp4_jpeg, tempdir_setup,
        tempdir_cleanup);
    tcase_add_test (tc_chain_mp4_jpeg, test_splitmuxsink_muxer_pad_map);
    tcase_add_test (tc_chain_mp4_jpeg, test_splitmuxsink_eos_without_buffer);
  } else {
    GST_INFO ("Skipping tests, missing plugins: jpegenc or mp4mux");
  }

  if (have_qtmux && have_vp8) {
    tcase_add_checked_fixture (tc_chain_keyframe_request, tempdir_setup,
        tempdir_cleanup);
    tcase_add_test (tc_chain_keyframe_request, test_splitmuxsink_multivid);
    tcase_add_test (tc_chain_keyframe_request,
        test_splitmuxsink_without_keyframe_request);
    tcase_add_test (tc_chain_keyframe_request,
        test_splitmuxsink_keyframe_request);
    tcase_add_test (tc_chain_keyframe_request,
        test_splitmuxsink_keyframe_request_more);
    tcase_add_test (tc_chain_keyframe_request,
        test_splitmuxsink_keyframe_request_less);
  } else {
    GST_INFO ("Skipping tests, missing plugins: vp8enc or mp4mux");
  }

  return s;
}

GST_CHECK_MAIN (splitmuxsink);
