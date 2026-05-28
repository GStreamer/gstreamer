/* GStreamer WavParse unit tests
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

#include <math.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/riff/riff-ids.h>

#define CORRUPT_HEADER_WAV_PATH GST_TEST_FILES_PATH G_DIR_SEPARATOR_S \
    "corruptheadertestsrc.wav"
#define SIMPLE_WAV_PATH GST_TEST_FILES_PATH G_DIR_SEPARATOR_S "audiotestsrc.wav"

/* Minimal ID3v2.4 tag with one TXXX REPLAYGAIN_TRACK_GAIN=-8.08 dB frame */
static const guint8 id3v2_replaygain_txxx[] = {
  /* ID3v2.4 header (10 bytes) */
  'I', 'D', '3', 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x29,
  /* TXXX frame header (10 bytes): id, synchsafe size=31, flags */
  'T', 'X', 'X', 'X', 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00,
  /* TXXX frame content (31 bytes): Latin-1, "REPLAYGAIN_TRACK_GAIN\0-8.08 dB" */
  0x00,
  'R', 'E', 'P', 'L', 'A', 'Y', 'G', 'A', 'I', 'N', '_',
  'T', 'R', 'A', 'C', 'K', '_', 'G', 'A', 'I', 'N', 0x00,
  '-', '8', '.', '0', '8', ' ', 'd', 'B',
};

static GstBuffer *
build_wav_with_id3_chunk (guint32 fourcc)
{
  GByteArray *ba;
  GstBuffer *buf;
  guint8 tmp[4];
  static const guint8 fmt_data[] = {
    0x01, 0x00,                 /* PCM */
    0x01, 0x00,                 /* 1 channel */
    0x44, 0xac, 0x00, 0x00,     /* 44100 Hz */
    0x88, 0x58, 0x01, 0x00,     /* byte rate = 88200 */
    0x02, 0x00,                 /* block align */
    0x10, 0x00,                 /* 16 bits per sample */
  };

  ba = g_byte_array_new ();

  /* RIFF header (size placeholder, fixed at end) */
  g_byte_array_append (ba, (guint8 *) "RIFF", 4);
  GST_WRITE_UINT32_LE (tmp, 0);
  g_byte_array_append (ba, tmp, 4);
  g_byte_array_append (ba, (guint8 *) "WAVE", 4);

  /* fmt chunk */
  g_byte_array_append (ba, (guint8 *) "fmt ", 4);
  GST_WRITE_UINT32_LE (tmp, sizeof (fmt_data));
  g_byte_array_append (ba, tmp, 4);
  g_byte_array_append (ba, fmt_data, sizeof (fmt_data));

  /* id3/ID3 chunk before data so it is parsed in streaming mode too */
  GST_WRITE_UINT32_LE (tmp, fourcc);
  g_byte_array_append (ba, tmp, 4);
  GST_WRITE_UINT32_LE (tmp, sizeof (id3v2_replaygain_txxx));
  g_byte_array_append (ba, tmp, 4);
  g_byte_array_append (ba, id3v2_replaygain_txxx,
      sizeof (id3v2_replaygain_txxx));
  /* RIFF chunks must be padded to even size */
  if (sizeof (id3v2_replaygain_txxx) % 2 != 0) {
    guint8 pad = 0;
    g_byte_array_append (ba, &pad, 1);
  }

  /* data chunk: 4 bytes of silence (2 samples) */
  g_byte_array_append (ba, (guint8 *) "data", 4);
  GST_WRITE_UINT32_LE (tmp, 4);
  g_byte_array_append (ba, tmp, 4);
  g_byte_array_append (ba, (guint8 *) "\0\0\0\0", 4);

  GST_WRITE_UINT32_LE (ba->data + 4, ba->len - 8);

  buf = gst_buffer_new_memdup (ba->data, ba->len);
  g_byte_array_free (ba, TRUE);
  return buf;
}

static void
do_test_id3_chunk_tags (guint32 id3_fourcc)
{
  GstHarness *h;
  GstBuffer *buf;
  GstTagList *tags = NULL;
  GstEvent *ev;
  gdouble gain;

  h = gst_harness_new_with_padnames ("wavparse", "sink", "src");
  gst_harness_set_src_caps_str (h, "audio/x-wav");

  buf = build_wav_with_id3_chunk (id3_fourcc);
  gst_harness_push (h, buf);
  gst_harness_push_event (h, gst_event_new_eos ());

  while ((ev = gst_harness_pull_event (h)) != NULL) {
    if (GST_EVENT_TYPE (ev) == GST_EVENT_TAG) {
      GstTagList *ev_tags;
      gst_event_parse_tag (ev, &ev_tags);
      if (tags)
        gst_tag_list_unref (tags);
      tags = gst_tag_list_copy (ev_tags);
    }
    if (GST_EVENT_TYPE (ev) == GST_EVENT_EOS) {
      gst_event_unref (ev);
      break;
    }
    gst_event_unref (ev);
  }

  fail_unless (tags != NULL, "No tag event received from wavparse");
  fail_unless (gst_tag_list_get_double (tags, GST_TAG_TRACK_GAIN, &gain));
  fail_unless (fabs (gain - (-8.08)) < 1e-6);

  gst_tag_list_unref (tags);
  gst_harness_teardown (h);
}

GST_START_TEST (test_id3_chunk_lowercase_tags)
{
  do_test_id3_chunk_tags (GST_RIFF_TAG_id3);
}

GST_END_TEST;

GST_START_TEST (test_id3_chunk_uppercase_tags)
{
  do_test_id3_chunk_tags (GST_RIFF_TAG_ID3);
}

GST_END_TEST;

static GstElement *
create_file_pipeline (const char *path, GstPadMode mode)
{
  GstElement *pipeline;
  GstElement *src, *q = NULL;
  GstElement *wavparse;
  GstElement *fakesink;

  pipeline = gst_pipeline_new ("testpipe");
  src = gst_element_factory_make ("filesrc", "filesrc");
  fail_if (src == NULL);
  if (mode == GST_PAD_MODE_PUSH)
    q = gst_element_factory_make ("queue", "queue");
  wavparse = gst_element_factory_make ("wavparse", "wavparse");
  fail_if (wavparse == NULL);
  fakesink = gst_element_factory_make ("fakesink", "fakesink");
  fail_if (fakesink == NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, wavparse, fakesink, q, NULL);

  g_object_set (src, "location", path, NULL);

  if (mode == GST_PAD_MODE_PUSH)
    fail_unless (gst_element_link_many (src, q, wavparse, fakesink, NULL));
  else
    fail_unless (gst_element_link_many (src, wavparse, fakesink, NULL));

  return pipeline;
}

static void
do_test_simple_file (GstPadMode mode)
{
  GstStateChangeReturn ret;
  GstElement *pipeline;
  GstMessage *msg;

  pipeline = create_file_pipeline (SIMPLE_WAV_PATH, mode);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_ASYNC);

  ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_SUCCESS);

  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipeline),
      GST_CLOCK_TIME_NONE, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);

  fail_unless_equals_string (GST_MESSAGE_TYPE_NAME (msg), "eos");

  gst_message_unref (msg);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_START_TEST (test_simple_file_pull)
{
  do_test_simple_file (TRUE);
}

GST_END_TEST;

GST_START_TEST (test_simple_file_push)
{
  do_test_simple_file (FALSE);
}

GST_END_TEST;

static void
do_test_corrupt_header_file (GstPadMode mode)
{
  GstStateChangeReturn ret;
  GstElement *pipeline;
  GstMessage *msg;

  pipeline = create_file_pipeline (CORRUPT_HEADER_WAV_PATH, mode);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_ASYNC);

  ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_FAILURE);

  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipeline),
      GST_CLOCK_TIME_NONE, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_ERROR);

  gst_message_unref (msg);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_START_TEST (test_corrupt_header_file_push)
{
  do_test_corrupt_header_file (GST_PAD_MODE_PUSH);
}

GST_END_TEST;

static void
do_test_empty_file (gboolean can_activate_pull)
{
  GstStateChangeReturn ret1, ret2;
  GstElement *pipeline;
  GstElement *src;
  GstElement *wavparse;
  GstElement *fakesink;

  /* Pull mode */
  pipeline = gst_pipeline_new ("testpipe");
  src = gst_element_factory_make ("fakesrc", NULL);
  fail_if (src == NULL);
  wavparse = gst_element_factory_make ("wavparse", NULL);
  fail_if (wavparse == NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);
  fail_if (fakesink == NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, wavparse, fakesink, NULL);
  g_object_set (src, "num-buffers", 0, "can-activate-pull", can_activate_pull,
      NULL);

  fail_unless (gst_element_link_many (src, wavparse, fakesink, NULL));

  ret1 = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret1 == GST_STATE_CHANGE_ASYNC)
    ret2 = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  else
    ret2 = ret1;

  /* should have gotten an error on the bus, no output to fakesink */
  fail_unless_equals_int (ret2, GST_STATE_CHANGE_FAILURE);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_START_TEST (test_empty_file_pull)
{
  do_test_empty_file (TRUE);
}

GST_END_TEST;

GST_START_TEST (test_empty_file_push)
{
  do_test_empty_file (FALSE);
}

GST_END_TEST;

static GstPadProbeReturn
fakesink_buffer_cb (GstPad * sink, GstPadProbeInfo * info, gpointer user_data)
{
  GstClockTime *ts = user_data;
  GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER (info);

  fail_unless (buf);
  if (!GST_CLOCK_TIME_IS_VALID (*ts))
    *ts = GST_BUFFER_PTS (buf);

  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_seek)
{
  GstStateChangeReturn ret;
  GstElement *pipeline;
  GstMessage *msg;
  GstElement *wavparse, *fakesink;
  GstPad *pad;
  GstClockTime seek_position = (20 * GST_MSECOND);
  GstClockTime first_ts = GST_CLOCK_TIME_NONE;

  pipeline = create_file_pipeline (SIMPLE_WAV_PATH, GST_PAD_MODE_PULL);
  wavparse = gst_bin_get_by_name (GST_BIN (pipeline), "wavparse");
  fail_unless (wavparse);
  fakesink = gst_bin_get_by_name (GST_BIN (pipeline), "fakesink");
  fail_unless (fakesink);

  pad = gst_element_get_static_pad (fakesink, "sink");
  fail_unless (pad);
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER, fakesink_buffer_cb,
      &first_ts, NULL);
  gst_object_unref (pad);

  /* wavparse is able to seek in the READY state */
  ret = gst_element_set_state (pipeline, GST_STATE_READY);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_SUCCESS);

  fail_unless (gst_element_seek_simple (wavparse, GST_FORMAT_TIME,
          GST_SEEK_FLAG_FLUSH, seek_position));

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_ASYNC);

  ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_SUCCESS);

  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipeline),
      GST_CLOCK_TIME_NONE, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);

  /* check that the first buffer produced by wavparse matches the seek
     position we requested */
  fail_unless_equals_clocktime (first_ts, seek_position);

  fail_unless_equals_string (GST_MESSAGE_TYPE_NAME (msg), "eos");

  gst_message_unref (msg);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (wavparse);
  gst_object_unref (fakesink);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_query_uri)
{
  GstElement *pipeline, *filesrc, *wavparse, *fakesink;
  GstQuery *query;
  gchar *uri;
  fail_unless ((pipeline = gst_pipeline_new (NULL)) != NULL,
      "Could not create pipeline");
  fail_unless ((filesrc = gst_element_factory_make ("filesrc", NULL)) != NULL,
      "Could not create filesrc");
  fail_unless ((wavparse = gst_element_factory_make ("wavparse", NULL)) != NULL,
      "Could not create wavparse");
  fail_unless ((fakesink = gst_element_factory_make ("fakesink", NULL)) != NULL,
      "Could not create fakesink");
  gst_bin_add_many (GST_BIN (pipeline), filesrc, wavparse, fakesink, NULL);
  gst_element_link_many (filesrc, wavparse, fakesink, NULL);
  g_object_set (G_OBJECT (filesrc), "location", "my_test_file", NULL);
  fail_unless ((query = gst_query_new_uri ()) != NULL,
      "Could not prepare uri query");
  fail_unless (gst_element_query (GST_ELEMENT (wavparse), query),
      "Could not query uri");
  gst_query_parse_uri (query, &uri);
  fail_unless (uri != NULL);

  g_free (uri);
  gst_query_unref (query);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
wavparse_suite (void)
{
  Suite *s = suite_create ("wavparse");
  TCase *tc_chain = tcase_create ("wavparse");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_empty_file_pull);
  tcase_add_test (tc_chain, test_empty_file_push);
  tcase_add_test (tc_chain, test_corrupt_header_file_push);
  tcase_add_test (tc_chain, test_simple_file_pull);
  tcase_add_test (tc_chain, test_simple_file_push);
  tcase_add_test (tc_chain, test_seek);
  tcase_add_test (tc_chain, test_query_uri);
  tcase_add_test (tc_chain, test_id3_chunk_lowercase_tags);
  tcase_add_test (tc_chain, test_id3_chunk_uppercase_tags);
  return s;
}

GST_CHECK_MAIN (wavparse)
