/* GStreamer
 *
 * unit test for vorbisenc
 *
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
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

#include <gst/check/gstcheck.h>

#define TIMESTAMP_OFFSET G_GUINT64_CONSTANT(3249870963)

static GCond *cond = NULL;
static GMutex *lock = NULL;
static GstBuffer *buf = NULL;
static gulong id;

static gboolean
buffer_probe (GstPad * pad, GstBuffer * buffer, gpointer unused)
{
  g_mutex_lock (lock);

  while (buf != NULL)
    g_cond_wait (cond, lock);

  buf = gst_buffer_ref (buffer);

  g_cond_signal (cond);

  g_mutex_unlock (lock);

  return TRUE;
}

static void
start_pipeline (GstElement * bin, GstPad * pad)
{
  id = gst_pad_add_buffer_probe (pad, G_CALLBACK (buffer_probe), NULL);

  cond = g_cond_new ();
  lock = g_mutex_new ();

  gst_element_set_state (bin, GST_STATE_PLAYING);

}

static GstBuffer *
get_buffer (GstElement * bin, GstPad * pad)
{
  GstBuffer *ret;

  g_mutex_lock (lock);

  while (buf == NULL)
    g_cond_wait (cond, lock);

  ret = buf;
  buf = NULL;

  g_cond_signal (cond);

  g_mutex_unlock (lock);

  return ret;
}

static void
stop_pipeline (GstElement * bin, GstPad * pad)
{
  g_mutex_lock (lock);
  if (buf)
    gst_buffer_unref (buf);
  buf = NULL;
  gst_pad_remove_buffer_probe (pad, (guint) id);
  id = 0;
  g_cond_signal (cond);
  g_mutex_unlock (lock);

  gst_element_set_state (bin, GST_STATE_NULL);

  g_mutex_lock (lock);
  if (buf)
    gst_buffer_unref (buf);
  buf = NULL;
  g_mutex_unlock (lock);

  g_mutex_free (lock);
  g_cond_free (cond);

  lock = NULL;
  cond = NULL;
}

static void
check_buffer_timestamp (GstBuffer * buffer, GstClockTime timestamp)
{
  fail_unless (GST_BUFFER_TIMESTAMP (buffer) == timestamp,
      "expected timestamp %" GST_TIME_FORMAT
      ", but got timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));
}

static void
check_buffer_duration (GstBuffer * buffer, GstClockTime duration)
{
  fail_unless (GST_BUFFER_DURATION (buffer) == duration,
      "expected duration %" GST_TIME_FORMAT
      ", but got duration %" GST_TIME_FORMAT,
      GST_TIME_ARGS (duration), GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));
}

static void
check_buffer_granulepos (GstBuffer * buffer, GstClockTime granulepos)
{
  fail_unless (GST_BUFFER_OFFSET_END (buffer) == granulepos,
      "expected granulepos %" G_GUINT64_FORMAT
      ", but got granulepos %" G_GUINT64_FORMAT,
      granulepos, GST_BUFFER_OFFSET_END (buffer));
}

/* this check is here to check that the granulepos we derive from the timestamp
   is about correct. This is "about correct" because you can't precisely go from
   timestamp to granulepos due to the downward-rounding characteristics of
   gst_util_uint64_scale, so you check if granulepos is equal to the number, or
   the number plus one. */
static void
check_buffer_granulepos_from_endtime (GstBuffer * buffer, GstClockTime endtime)
{
  GstClockTime granulepos, expected;

  granulepos = GST_BUFFER_OFFSET_END (buffer);
  expected = gst_util_uint64_scale (endtime, 44100, GST_SECOND);

  fail_unless (granulepos == expected || granulepos == expected + 1,
      "expected granulepos %" G_GUINT64_FORMAT
      " or %" G_GUINT64_FORMAT
      ", but got granulepos %" G_GUINT64_FORMAT,
      expected, expected + 1, granulepos);
}

GST_START_TEST (test_granulepos_offset)
{
  GstElement *bin;
  GstPad *pad;
  gchar *pipe_str;
  GstBuffer *buffer;
  GError *error = NULL;
  GstClockTime timestamp;

  pipe_str = g_strdup_printf ("audiotestsrc timestamp-offset=%" G_GUINT64_FORMAT
      " ! audio/x-raw-int,rate=44100"
      " ! audioconvert ! vorbisenc ! fakesink", TIMESTAMP_OFFSET);

  bin = gst_parse_launch (pipe_str, &error);
  fail_unless (bin != NULL, "Error parsing pipeline: %s",
      error ? error->message : "(invalid error)");
  g_free (pipe_str);

  /* get the pad */
  {
    GstElement *sink = gst_bin_get_by_name (GST_BIN (bin), "fakesink0");

    fail_unless (sink != NULL, "Could not get fakesink out of bin");
    pad = gst_element_get_pad (sink, "sink");
    fail_unless (pad != NULL, "Could not get pad out of fakesink");
    gst_object_unref (sink);
  }

  start_pipeline (bin, pad);

  /* header packets should have timestamp == NONE, granulepos 0 */
  buffer = get_buffer (bin, pad);
  check_buffer_timestamp (buffer, GST_CLOCK_TIME_NONE);
  check_buffer_duration (buffer, GST_CLOCK_TIME_NONE);
  check_buffer_granulepos (buffer, 0);
  gst_buffer_unref (buffer);

  buffer = get_buffer (bin, pad);
  check_buffer_timestamp (buffer, GST_CLOCK_TIME_NONE);
  check_buffer_duration (buffer, GST_CLOCK_TIME_NONE);
  check_buffer_granulepos (buffer, 0);
  gst_buffer_unref (buffer);

  buffer = get_buffer (bin, pad);
  check_buffer_timestamp (buffer, GST_CLOCK_TIME_NONE);
  check_buffer_duration (buffer, GST_CLOCK_TIME_NONE);
  check_buffer_granulepos (buffer, 0);
  gst_buffer_unref (buffer);

  {
    GstClockTime next_timestamp, last_granulepos;

    /* first buffer should have timestamp of TIMESTAMP_OFFSET, granulepos to
     * match the timestamp of the end of the last sample in the output buffer.
     * Note that one cannot go timestamp->granulepos->timestamp and get the same
     * value due to loss of precision with granulepos. vorbisenc does take care
     * to timestamp correctly based on the offset of the input data however, so
     * it does do sub-granulepos timestamping. */
    buffer = get_buffer (bin, pad);
    last_granulepos = GST_BUFFER_OFFSET_END (buffer);
    check_buffer_timestamp (buffer, TIMESTAMP_OFFSET);
    /* don't really have a good way of checking duration... */
    check_buffer_granulepos_from_endtime (buffer,
        TIMESTAMP_OFFSET + GST_BUFFER_DURATION (buffer));

    next_timestamp = TIMESTAMP_OFFSET + GST_BUFFER_DURATION (buffer);

    gst_buffer_unref (buffer);

    /* check continuity with the next buffer */
    buffer = get_buffer (bin, pad);
    check_buffer_timestamp (buffer, next_timestamp);
    check_buffer_duration (buffer,
        gst_util_uint64_scale (GST_BUFFER_OFFSET_END (buffer), GST_SECOND,
            44100)
        - gst_util_uint64_scale (last_granulepos, GST_SECOND, 44100));
    check_buffer_granulepos_from_endtime (buffer,
        next_timestamp + GST_BUFFER_DURATION (buffer));

    gst_buffer_unref (buffer);
  }

  stop_pipeline (bin, pad);

  gst_object_unref (pad);
  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_timestamps)
{
  GstElement *bin;
  GstPad *pad;
  gchar *pipe_str;
  GstBuffer *buffer;
  GError *error = NULL;
  GstClockTime timestamp;

  pipe_str = g_strdup_printf ("audiotestsrc"
      " ! audio/x-raw-int,rate=44100" " ! audioconvert ! vorbisenc ! fakesink");

  bin = gst_parse_launch (pipe_str, &error);
  fail_unless (bin != NULL, "Error parsing pipeline: %s",
      error ? error->message : "(invalid error)");
  g_free (pipe_str);

  /* get the pad */
  {
    GstElement *sink = gst_bin_get_by_name (GST_BIN (bin), "fakesink0");

    fail_unless (sink != NULL, "Could not get fakesink out of bin");
    pad = gst_element_get_pad (sink, "sink");
    fail_unless (pad != NULL, "Could not get pad out of fakesink");
    gst_object_unref (sink);
  }

  start_pipeline (bin, pad);

  /* check header packets */
  buffer = get_buffer (bin, pad);
  check_buffer_timestamp (buffer, GST_CLOCK_TIME_NONE);
  check_buffer_duration (buffer, GST_CLOCK_TIME_NONE);
  check_buffer_granulepos (buffer, 0);
  gst_buffer_unref (buffer);

  buffer = get_buffer (bin, pad);
  check_buffer_timestamp (buffer, GST_CLOCK_TIME_NONE);
  check_buffer_duration (buffer, GST_CLOCK_TIME_NONE);
  check_buffer_granulepos (buffer, 0);
  gst_buffer_unref (buffer);

  buffer = get_buffer (bin, pad);
  check_buffer_timestamp (buffer, GST_CLOCK_TIME_NONE);
  check_buffer_duration (buffer, GST_CLOCK_TIME_NONE);
  check_buffer_granulepos (buffer, 0);
  gst_buffer_unref (buffer);

  {
    GstClockTime next_timestamp, last_granulepos;

    /* first buffer has timestamp 0 */
    buffer = get_buffer (bin, pad);
    last_granulepos = GST_BUFFER_OFFSET_END (buffer);
    check_buffer_timestamp (buffer, 0);
    /* don't really have a good way of checking duration... */
    check_buffer_granulepos_from_endtime (buffer, GST_BUFFER_DURATION (buffer));

    next_timestamp = GST_BUFFER_DURATION (buffer);

    gst_buffer_unref (buffer);

    /* check continuity with the next buffer */
    buffer = get_buffer (bin, pad);
    check_buffer_timestamp (buffer, next_timestamp);
    check_buffer_duration (buffer,
        gst_util_uint64_scale (GST_BUFFER_OFFSET_END (buffer), GST_SECOND,
            44100)
        - gst_util_uint64_scale (last_granulepos, GST_SECOND, 44100));
    check_buffer_granulepos_from_endtime (buffer,
        next_timestamp + GST_BUFFER_DURATION (buffer));

    gst_buffer_unref (buffer);
  }

  stop_pipeline (bin, pad);

  gst_object_unref (pad);
  gst_object_unref (bin);
}

GST_END_TEST;

Suite *
vorbisenc_suite (void)
{
  Suite *s = suite_create ("vorbisenc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_granulepos_offset);
  tcase_add_test (tc_chain, test_timestamps);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = vorbisenc_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
