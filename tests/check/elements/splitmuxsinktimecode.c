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

#ifdef HAVE_VALGRIND
# include <valgrind/valgrind.h>
#else
#define RUNNING_ON_VALGRIND FALSE
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
  tmpdir = g_build_filename (systmp, "splitmux-timecode-test-XXXXXX", NULL);
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

static gchar *
check_format_location (GstElement * object,
    guint fragment_id, GstSample * first_sample)
{
  GstBuffer *buf = gst_sample_get_buffer (first_sample);

  /* Must have a buffer */
  fail_if (buf == NULL);
  GST_LOG ("New file - first buffer %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  return NULL;
}

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
splitmuxsink_split_by_keyframe_timecode (gboolean send_keyframe_request,
    const gchar * maxsize_timecode_string, guint maxsize_timecode_in_sec,
    guint encoder_key_interval_sec)
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
      "max-size-timecode=%s"
      " send-keyframe-requests=%s muxer=qtmux "
      "videotestsrc num-buffers=30 ! video/x-raw,width=80,height=64,framerate=5/1 "
      "! videoconvert ! timecodestamper ! queue ! vp8enc name=enc keyframe-max-dist=%d ! splitsink.video ",
      maxsize_timecode_string, send_keyframe_request ? "true" : "false",
      encoder_key_interval_sec ? encoder_key_interval_sec * 5 : 1);

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
  expected_count = (6 / maxsize_timecode_in_sec) +
      (6 % maxsize_timecode_in_sec ? 1 : 0);
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

GST_START_TEST (test_splitmuxsink_without_keyframe_request_timecode)
{
  /* This encoding option is intended to produce keyframe per 1 second
   * but splitmuxsink will split file per 2 second without keyframe request */
  splitmuxsink_split_by_keyframe_timecode (FALSE, "00:00:02:00", 2, 1);
}

GST_END_TEST;

GST_START_TEST (test_splitmuxsink_keyframe_request_timecode)
{
  /* This encoding option is intended to produce keyframe per 1 second
   * but splitmuxsink will request keyframe per 2 seconds. This should produce
   * 2 seconds long files */
  splitmuxsink_split_by_keyframe_timecode (TRUE, "00:00:02:00", 2, 1);
}

GST_END_TEST;

GST_START_TEST
    (test_splitmuxsink_keyframe_request_timecode_trailing_small_segment) {
  /* This encoding option is intended to produce keyframe per 1 second
   * but splitmuxsink will request keyframe per 4 seconds. This should produce
   * 4 seconds long files */
  splitmuxsink_split_by_keyframe_timecode (TRUE, "00:00:04:00", 4, 1);
}

GST_END_TEST;

GST_START_TEST (test_splitmuxsink_keyframe_request_timecode_all_intra)
{
  /* This encoding option is intended to produce keyframe for every frame.
   * This should produce 1 second long files */
  splitmuxsink_split_by_keyframe_timecode (TRUE, "00:00:01:00", 1, 0);
}

GST_END_TEST;

static void
count_frames (const gchar * file_name, guint expected_count)
{
  GstMessage *msg;
  GstElement *pipeline;
  GstElement *appsink;
  GstElement *fakesink2;
  GstAppSinkCallbacks callbacks = { NULL };
  gchar *uri;
  guint frame_count = 0;

  GST_DEBUG ("Playing back files matching %s", file_name);

  pipeline = gst_element_factory_make ("playbin", NULL);
  fail_if (pipeline == NULL);

  appsink = gst_element_factory_make ("appsink", NULL);
  fail_if (appsink == NULL);
  g_object_set (G_OBJECT (appsink), "sync", FALSE, NULL);

  g_object_set (G_OBJECT (pipeline), "video-sink", appsink, NULL);
  fakesink2 = gst_element_factory_make ("fakesink", NULL);
  fail_if (fakesink2 == NULL);
  g_object_set (G_OBJECT (pipeline), "audio-sink", fakesink2, NULL);

  uri = g_strdup_printf ("file://%s", file_name);

  g_object_set (G_OBJECT (pipeline), "uri", uri, NULL);
  g_free (uri);

  callbacks.new_sample = receive_sample;
  gst_app_sink_set_callbacks (GST_APP_SINK (appsink),
      &callbacks, &frame_count, NULL);

  seek_pipeline (pipeline, 1.0, 0, -1);
  fail_unless (first_ts == GST_CLOCK_TIME_NONE);
  msg = run_pipeline (pipeline);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  fail_unless (frame_count == expected_count,
      "Frame count %u is not equal to expected %u frame count %u",
      expected_count, frame_count);

  gst_object_unref (pipeline);
}

typedef struct
{
  const gchar *max_timecode;
  guint num_frame[3];
  const gchar *fragment_name[3];
  GstClockTime expected_fku_time[3];
  guint upstream_fku_count;
} TimeCodeTestData;

static GstPadProbeReturn
count_upstrea_fku_with_data (GstPad * pad, GstPadProbeInfo * info,
    TimeCodeTestData * data)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_UPSTREAM:
      if (gst_video_event_is_force_key_unit (event)) {
        GstClockTime running_time;
        GstClockTime expected;

        expected = data->expected_fku_time[data->upstream_fku_count];

        gst_video_event_parse_upstream_force_key_unit (event,
            &running_time, NULL, NULL);

        GST_INFO ("expected fku time %" GST_TIME_FORMAT
            ", got %" GST_TIME_FORMAT, GST_TIME_ARGS (expected),
            GST_TIME_ARGS (running_time));

        /* splitmuxsink will request keyframe with slightly earlier timestamp */
        fail_unless (expected <= running_time + 5 * GST_USECOND);
        fail_unless (expected >= running_time);

        data->upstream_fku_count++;
      }
      break;
    default:
      break;
  }

  return GST_PAD_PROBE_OK;
}

static void
splitmuxsink_split_by_keyframe_timecode_framerate_29_97 (gboolean equal_dur,
    gboolean all_keyframe)
{
  GstMessage *msg;
  GstElement *pipeline;
  GstElement *sink;
  GstElement *enc;
  GstPad *srcpad;
  gchar *pipeline_str;
  gchar *dest_pattern;
  guint count;
  guint expected_fku_count;
  TimeCodeTestData data;
  gint i;

  if (equal_dur) {
    data.max_timecode = "00:01:00;02";
    data.num_frame[0] = data.num_frame[1] = 1800;
    data.expected_fku_time[0] =
        gst_util_uint64_scale (1800 * GST_SECOND, 1001, 30000);
    data.expected_fku_time[1] =
        gst_util_uint64_scale (2 * 1800 * GST_SECOND, 1001, 30000);
    data.expected_fku_time[2] =
        gst_util_uint64_scale (3 * 1800 * GST_SECOND, 1001, 30000);
  } else {
    data.max_timecode = "00:01:00;00";
    data.num_frame[0] = 1800;
    data.num_frame[1] = 1798;
    data.expected_fku_time[0] =
        gst_util_uint64_scale (1800 * GST_SECOND, 1001, 30000);
    data.expected_fku_time[1] =
        gst_util_uint64_scale ((1800 + 1798) * GST_SECOND, 1001, 30000);
    data.expected_fku_time[2] =
        gst_util_uint64_scale ((1800 + 2 * 1798) * GST_SECOND, 1001, 30000);
  }
  data.num_frame[2] = 5000 - (data.num_frame[0] + data.num_frame[1]);

  data.fragment_name[0] = "out0.m4v";
  data.fragment_name[1] = "out1.m4v";
  data.fragment_name[2] = "out2.m4v";
  data.upstream_fku_count = 0;

  pipeline_str = g_strdup_printf ("splitmuxsink name=splitsink "
      "max-size-timecode=%s "
      "send-keyframe-requests=%s muxer=qtmux "
      "videotestsrc num-buffers=5000 ! "
      "video/x-raw,width=80,height=64,framerate=30000/1001 "
      "! videoconvert ! timecodestamper drop-frame=true ! queue ! "
      "vp8enc name=enc keyframe-max-dist=%d ! splitsink.video ",
      data.max_timecode, all_keyframe ? "false" : "true",
      all_keyframe ? 1 : 5000);

  pipeline = gst_parse_launch (pipeline_str, NULL);
  g_free (pipeline_str);

  fail_if (pipeline == NULL);
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "splitsink");
  fail_if (sink == NULL);
  g_signal_connect (sink, "format-location-full",
      (GCallback) check_format_location, NULL);
  dest_pattern = g_build_filename (tmpdir, "out%d.m4v", NULL);
  g_object_set (G_OBJECT (sink), "location", dest_pattern, NULL);
  g_free (dest_pattern);
  g_object_unref (sink);

  enc = gst_bin_get_by_name (GST_BIN (pipeline), "enc");
  fail_if (enc == NULL);
  srcpad = gst_element_get_static_pad (enc, "src");
  fail_if (srcpad == NULL);

  gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
      (GstPadProbeCallback) count_upstrea_fku_with_data, &data, NULL);
  gst_object_unref (srcpad);
  gst_object_unref (enc);

  msg = run_pipeline (pipeline);

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR)
    dump_error (msg);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_object_unref (pipeline);

  count = count_files (tmpdir);
  fail_unless (count == 3, "Expected 3 output files, got %d", count);

  if (all_keyframe) {
    expected_fku_count = 0;
  } else {
    expected_fku_count = count;
  }

  GST_INFO ("Upstream force keyunit event count %d", data.upstream_fku_count);

  fail_unless (data.upstream_fku_count == expected_fku_count,
      "Expected upstream force keyunit event count %d, got %d",
      expected_fku_count, data.upstream_fku_count);

  for (i = 0; i < 3; i++) {
    gchar *file_name = g_build_filename (tmpdir, data.fragment_name[i], NULL);
    count_frames (file_name, data.num_frame[i]);
    g_free (file_name);
  }
}

GST_START_TEST (test_splitmuxsink_timecode_framerate_29_97_equal_duration)
{
  splitmuxsink_split_by_keyframe_timecode_framerate_29_97 (TRUE, FALSE);
}

GST_END_TEST;

GST_START_TEST
    (test_splitmuxsink_timecode_framerate_29_97_equal_duration_all_intra) {
  splitmuxsink_split_by_keyframe_timecode_framerate_29_97 (TRUE, TRUE);
}

GST_END_TEST;

GST_START_TEST (test_splitmuxsink_timecode_framerate_29_97_not_equal_duration)
{
  splitmuxsink_split_by_keyframe_timecode_framerate_29_97 (TRUE, FALSE);
}

GST_END_TEST;

GST_START_TEST
    (test_splitmuxsink_timecode_framerate_29_97_not_equal_duration_all_intra) {
  splitmuxsink_split_by_keyframe_timecode_framerate_29_97 (TRUE, TRUE);
}

GST_END_TEST;

static void
splitmuxsink_timecode_framerate_25 (gboolean all_keyframe)
{
  GstMessage *msg;
  GstElement *pipeline;
  GstElement *sink;
  GstElement *enc;
  GstPad *srcpad;
  gchar *pipeline_str;
  gchar *dest_pattern;
  guint count;
  guint expected_fku_count;
  TimeCodeTestData data;
  gint i;
  guint num_total_frames = 4000;

  data.max_timecode = "00:01:00;00";
  data.num_frame[0] = 1500;
  data.num_frame[1] = 1500;
  data.num_frame[2] =
      num_total_frames - (data.num_frame[0] + data.num_frame[1]);
  /* in case of framerate 25/1 with maxsize timecode "00:01:00;00",
   * all fragments will have equal size */
  data.expected_fku_time[0] = GST_SECOND * 60;
  data.expected_fku_time[1] = GST_SECOND * 120;
  data.expected_fku_time[2] = GST_SECOND * 180;

  data.fragment_name[0] = "out0.m4v";
  data.fragment_name[1] = "out1.m4v";
  data.fragment_name[2] = "out2.m4v";
  data.upstream_fku_count = 0;

  pipeline_str = g_strdup_printf ("splitmuxsink name=splitsink "
      "max-size-timecode=%s "
      "send-keyframe-requests=%s muxer=qtmux "
      "videotestsrc num-buffers=%d ! "
      "video/x-raw,width=80,height=64,framerate=25/1 "
      "! videoconvert ! timecodestamper drop-frame=true ! queue ! "
      "vp8enc name=enc keyframe-max-dist=%d ! splitsink.video ",
      data.max_timecode, all_keyframe ? "false" : "true", num_total_frames,
      all_keyframe ? 1 : num_total_frames);

  pipeline = gst_parse_launch (pipeline_str, NULL);
  g_free (pipeline_str);

  fail_if (pipeline == NULL);
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "splitsink");
  fail_if (sink == NULL);
  g_signal_connect (sink, "format-location-full",
      (GCallback) check_format_location, NULL);
  dest_pattern = g_build_filename (tmpdir, "out%d.m4v", NULL);
  g_object_set (G_OBJECT (sink), "location", dest_pattern, NULL);
  g_free (dest_pattern);
  g_object_unref (sink);

  enc = gst_bin_get_by_name (GST_BIN (pipeline), "enc");
  fail_if (enc == NULL);
  srcpad = gst_element_get_static_pad (enc, "src");
  fail_if (srcpad == NULL);

  gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
      (GstPadProbeCallback) count_upstrea_fku_with_data, &data, NULL);
  gst_object_unref (srcpad);
  gst_object_unref (enc);

  msg = run_pipeline (pipeline);

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR)
    dump_error (msg);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_object_unref (pipeline);

  count = count_files (tmpdir);
  fail_unless (count == 3, "Expected 3 output files, got %d", count);

  if (all_keyframe) {
    expected_fku_count = 0;
  } else {
    expected_fku_count = count;
  }

  GST_INFO ("Upstream force keyunit event count %d", data.upstream_fku_count);

  fail_unless (data.upstream_fku_count == expected_fku_count,
      "Expected upstream force keyunit event count %d, got %d",
      expected_fku_count, data.upstream_fku_count);

  for (i = 0; i < 3; i++) {
    gchar *file_name = g_build_filename (tmpdir, data.fragment_name[i], NULL);
    count_frames (file_name, data.num_frame[i]);
    g_free (file_name);
  }
}

GST_START_TEST (test_splitmuxsink_timecode_framerate_25)
{
  splitmuxsink_timecode_framerate_25 (TRUE);
}

GST_END_TEST;

GST_START_TEST (test_splitmuxsink_timecode_framerate_25_all_intra)
{
  splitmuxsink_timecode_framerate_25 (FALSE);
}

GST_END_TEST;

static Suite *
splitmuxsinktimecode_suite (void)
{
  Suite *s = suite_create ("splitmuxsink-timecode");
  TCase *tc_chain = tcase_create ("general");
  gboolean have_qtmux, have_vp8, have_timecodestamper;

  /* we assume that if encoder/muxer are there, decoder/demuxer will be a well */
  have_qtmux = gst_registry_check_feature_version (gst_registry_get (),
      "qtmux", GST_VERSION_MAJOR, GST_VERSION_MINOR, 0);
  have_vp8 = gst_registry_check_feature_version (gst_registry_get (),
      "vp8enc", GST_VERSION_MAJOR, GST_VERSION_MINOR, 0);
  have_timecodestamper =
      gst_registry_check_feature_version (gst_registry_get (),
      "timecodestamper", GST_VERSION_MAJOR, GST_VERSION_MINOR, 0);

  suite_add_tcase (s, tc_chain);

  if (have_qtmux && have_vp8 && have_timecodestamper) {
    tcase_add_checked_fixture (tc_chain, tempdir_setup, tempdir_cleanup);
    tcase_add_test (tc_chain,
        test_splitmuxsink_without_keyframe_request_timecode);
    tcase_add_test (tc_chain, test_splitmuxsink_keyframe_request_timecode);
    tcase_add_test (tc_chain,
        test_splitmuxsink_keyframe_request_timecode_trailing_small_segment);
    tcase_add_test (tc_chain,
        test_splitmuxsink_keyframe_request_timecode_all_intra);
    if (!(RUNNING_ON_VALGRIND)) {
      tcase_add_test (tc_chain,
          test_splitmuxsink_timecode_framerate_29_97_equal_duration);
      tcase_add_test (tc_chain,
          test_splitmuxsink_timecode_framerate_29_97_equal_duration_all_intra);
      tcase_add_test (tc_chain,
          test_splitmuxsink_timecode_framerate_29_97_not_equal_duration);
      tcase_add_test (tc_chain,
          test_splitmuxsink_timecode_framerate_29_97_not_equal_duration_all_intra);
      tcase_add_test (tc_chain, test_splitmuxsink_timecode_framerate_25);
      tcase_add_test (tc_chain,
          test_splitmuxsink_timecode_framerate_25_all_intra);
    }
  } else {
    GST_INFO
        ("Skipping tests, missing plugins: vp8enc, mp4mux, or timecodestamper");
  }

  return s;
}

GST_CHECK_MAIN (splitmuxsinktimecode);
