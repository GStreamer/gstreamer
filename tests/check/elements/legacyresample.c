/* GStreamer
 *
 * unit test for legacyresample
 *
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright (C) <2006> Tim-Philipp Müller <tim at centricular net>
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

#include <unistd.h>

#include <gst/check/gstcheck.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;


#define RESAMPLE_CAPS_TEMPLATE_STRING   \
    "audio/x-raw-int, "                 \
    "channels = (int) [ 1, MAX ], "     \
    "rate = (int) [ 1,  MAX ], "        \
    "endianness = (int) BYTE_ORDER, "   \
    "width = (int) 16, "                \
    "depth = (int) 16, "                \
    "signed = (bool) TRUE"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (RESAMPLE_CAPS_TEMPLATE_STRING)
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (RESAMPLE_CAPS_TEMPLATE_STRING)
    );

static GstElement *
setup_legacyresample (int channels, int inrate, int outrate)
{
  GstElement *legacyresample;
  GstCaps *caps;
  GstStructure *structure;

  GST_DEBUG ("setup_legacyresample");
  legacyresample = gst_check_setup_element ("legacyresample");

  caps = gst_caps_from_string (RESAMPLE_CAPS_TEMPLATE_STRING);
  structure = gst_caps_get_structure (caps, 0);
  gst_structure_set (structure, "channels", G_TYPE_INT, channels,
      "rate", G_TYPE_INT, inrate, NULL);
  fail_unless (gst_caps_is_fixed (caps));

  fail_unless (gst_element_set_state (legacyresample,
          GST_STATE_PAUSED) == GST_STATE_CHANGE_SUCCESS,
      "could not set to paused");

  mysrcpad = gst_check_setup_src_pad (legacyresample, &srctemplate, caps);
  gst_pad_set_caps (mysrcpad, caps);
  gst_caps_unref (caps);

  caps = gst_caps_from_string (RESAMPLE_CAPS_TEMPLATE_STRING);
  structure = gst_caps_get_structure (caps, 0);
  gst_structure_set (structure, "channels", G_TYPE_INT, channels,
      "rate", G_TYPE_INT, outrate, NULL);
  fail_unless (gst_caps_is_fixed (caps));

  mysinkpad = gst_check_setup_sink_pad (legacyresample, &sinktemplate, caps);
  /* this installs a getcaps func that will always return the caps we set
   * later */
  gst_pad_set_caps (mysinkpad, caps);
  gst_pad_use_fixed_caps (mysinkpad);
  gst_caps_unref (caps);

  gst_pad_set_active (mysinkpad, TRUE);
  gst_pad_set_active (mysrcpad, TRUE);

  return legacyresample;
}

static void
cleanup_legacyresample (GstElement * legacyresample)
{
  GST_DEBUG ("cleanup_legacyresample");

  fail_unless (gst_element_set_state (legacyresample,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to NULL");

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (legacyresample);
  gst_check_teardown_sink_pad (legacyresample);
  gst_check_teardown_element (legacyresample);
}

static void
fail_unless_perfect_stream (void)
{
  guint64 timestamp = 0L, duration = 0L;
  guint64 offset = 0L, offset_end = 0L;

  GList *l;
  GstBuffer *buffer;

  for (l = buffers; l; l = l->next) {
    buffer = GST_BUFFER (l->data);
    ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
    GST_DEBUG ("buffer timestamp %" G_GUINT64_FORMAT ", duration %"
        G_GUINT64_FORMAT " offset %" G_GUINT64_FORMAT " offset_end %"
        G_GUINT64_FORMAT,
        GST_BUFFER_TIMESTAMP (buffer),
        GST_BUFFER_DURATION (buffer),
        GST_BUFFER_OFFSET (buffer), GST_BUFFER_OFFSET_END (buffer));

    fail_unless_equals_uint64 (timestamp, GST_BUFFER_TIMESTAMP (buffer));
    fail_unless_equals_uint64 (offset, GST_BUFFER_OFFSET (buffer));
    duration = GST_BUFFER_DURATION (buffer);
    offset_end = GST_BUFFER_OFFSET_END (buffer);

    timestamp += duration;
    offset = offset_end;
    gst_buffer_unref (buffer);
  }
  g_list_free (buffers);
  buffers = NULL;
}

/* this tests that the output is a perfect stream if the input is */
static void
test_perfect_stream_instance (int inrate, int outrate, int samples,
    int numbuffers)
{
  GstElement *legacyresample;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  guint64 offset = 0;

  int i, j;
  gint16 *p;

  legacyresample = setup_legacyresample (2, inrate, outrate);
  caps = gst_pad_get_negotiated_caps (mysrcpad);
  fail_unless (gst_caps_is_fixed (caps));

  fail_unless (gst_element_set_state (legacyresample,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  for (j = 1; j <= numbuffers; ++j) {

    inbuffer = gst_buffer_new_and_alloc (samples * 4);
    GST_BUFFER_DURATION (inbuffer) = samples * GST_SECOND / inrate;
    GST_BUFFER_TIMESTAMP (inbuffer) = GST_BUFFER_DURATION (inbuffer) * (j - 1);
    GST_BUFFER_OFFSET (inbuffer) = offset;
    offset += samples;
    GST_BUFFER_OFFSET_END (inbuffer) = offset;

    gst_buffer_set_caps (inbuffer, caps);

    p = (gint16 *) GST_BUFFER_DATA (inbuffer);

    /* create a 16 bit signed ramp */
    for (i = 0; i < samples; ++i) {
      *p = -32767 + i * (65535 / samples);
      ++p;
      *p = -32767 + i * (65535 / samples);
      ++p;
    }

    /* pushing gives away my reference ... */
    fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
    /* ... but it ends up being collected on the global buffer list */
    fail_unless_equals_int (g_list_length (buffers), j);
  }

  /* FIXME: we should make legacyresample handle eos by flushing out the last
   * samples, which will give us one more, small, buffer */
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);

  fail_unless_perfect_stream ();

  /* cleanup */
  gst_caps_unref (caps);
  cleanup_legacyresample (legacyresample);
}


/* make sure that outgoing buffers are contiguous in timestamp/duration and
 * offset/offsetend
 */
GST_START_TEST (test_perfect_stream)
{
  /* integral scalings */
  test_perfect_stream_instance (48000, 24000, 500, 20);
  test_perfect_stream_instance (48000, 12000, 500, 20);
  test_perfect_stream_instance (12000, 24000, 500, 20);
  test_perfect_stream_instance (12000, 48000, 500, 20);

  /* non-integral scalings */
  test_perfect_stream_instance (44100, 8000, 500, 20);
  test_perfect_stream_instance (8000, 44100, 500, 20);

  /* wacky scalings */
  test_perfect_stream_instance (12345, 54321, 500, 20);
  test_perfect_stream_instance (101, 99, 500, 20);
}

GST_END_TEST;

/* this tests that the output is a correct discontinuous stream
 * if the input is; ie input drops in time come out the same way */
static void
test_discont_stream_instance (int inrate, int outrate, int samples,
    int numbuffers)
{
  GstElement *legacyresample;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  GstClockTime ints;

  int i, j;
  gint16 *p;

  GST_DEBUG ("inrate:%d outrate:%d samples:%d numbuffers:%d",
      inrate, outrate, samples, numbuffers);

  legacyresample = setup_legacyresample (2, inrate, outrate);
  caps = gst_pad_get_negotiated_caps (mysrcpad);
  fail_unless (gst_caps_is_fixed (caps));

  fail_unless (gst_element_set_state (legacyresample,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  for (j = 1; j <= numbuffers; ++j) {

    inbuffer = gst_buffer_new_and_alloc (samples * 4);
    GST_BUFFER_DURATION (inbuffer) = samples * GST_SECOND / inrate;
    /* "drop" half the buffers */
    ints = GST_BUFFER_DURATION (inbuffer) * 2 * (j - 1);
    GST_BUFFER_TIMESTAMP (inbuffer) = ints;
    GST_BUFFER_OFFSET (inbuffer) = (j - 1) * 2 * samples;
    GST_BUFFER_OFFSET_END (inbuffer) = j * 2 * samples + samples;

    gst_buffer_set_caps (inbuffer, caps);

    p = (gint16 *) GST_BUFFER_DATA (inbuffer);

    /* create a 16 bit signed ramp */
    for (i = 0; i < samples; ++i) {
      *p = -32767 + i * (65535 / samples);
      ++p;
      *p = -32767 + i * (65535 / samples);
      ++p;
    }

    GST_DEBUG ("Sending Buffer time:%" G_GUINT64_FORMAT " duration:%"
        G_GINT64_FORMAT " discont:%d offset:%" G_GUINT64_FORMAT " offset_end:%"
        G_GUINT64_FORMAT, GST_BUFFER_TIMESTAMP (inbuffer),
        GST_BUFFER_DURATION (inbuffer), GST_BUFFER_IS_DISCONT (inbuffer),
        GST_BUFFER_OFFSET (inbuffer), GST_BUFFER_OFFSET_END (inbuffer));
    /* pushing gives away my reference ... */
    fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);

    /* check if the timestamp of the pushed buffer matches the incoming one */
    outbuffer = g_list_nth_data (buffers, g_list_length (buffers) - 1);
    fail_if (outbuffer == NULL);
    fail_unless_equals_uint64 (ints, GST_BUFFER_TIMESTAMP (outbuffer));
    GST_DEBUG ("Got Buffer time:%" G_GUINT64_FORMAT " duration:%"
        G_GINT64_FORMAT " discont:%d offset:%" G_GUINT64_FORMAT " offset_end:%"
        G_GUINT64_FORMAT, GST_BUFFER_TIMESTAMP (outbuffer),
        GST_BUFFER_DURATION (outbuffer), GST_BUFFER_IS_DISCONT (outbuffer),
        GST_BUFFER_OFFSET (outbuffer), GST_BUFFER_OFFSET_END (outbuffer));
    if (j > 1) {
      fail_unless (GST_BUFFER_IS_DISCONT (outbuffer),
          "expected discont for buffer #%d", j);
    }
  }

  /* cleanup */
  gst_caps_unref (caps);
  cleanup_legacyresample (legacyresample);
}

GST_START_TEST (test_discont_stream)
{
  /* integral scalings */
  test_discont_stream_instance (48000, 24000, 500, 20);
  test_discont_stream_instance (48000, 12000, 500, 20);
  test_discont_stream_instance (12000, 24000, 500, 20);
  test_discont_stream_instance (12000, 48000, 500, 20);

  /* non-integral scalings */
  test_discont_stream_instance (44100, 8000, 500, 20);
  test_discont_stream_instance (8000, 44100, 500, 20);

  /* wacky scalings */
  test_discont_stream_instance (12345, 54321, 500, 20);
  test_discont_stream_instance (101, 99, 500, 20);
}

GST_END_TEST;



GST_START_TEST (test_reuse)
{
  GstElement *legacyresample;
  GstEvent *newseg;
  GstBuffer *inbuffer;
  GstCaps *caps;

  legacyresample = setup_legacyresample (1, 9343, 48000);
  caps = gst_pad_get_negotiated_caps (mysrcpad);
  fail_unless (gst_caps_is_fixed (caps));

  fail_unless (gst_element_set_state (legacyresample,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  newseg = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0, -1, 0);
  fail_unless (gst_pad_push_event (mysrcpad, newseg) != FALSE);

  inbuffer = gst_buffer_new_and_alloc (9343 * 4);
  memset (GST_BUFFER_DATA (inbuffer), 0, GST_BUFFER_SIZE (inbuffer));
  GST_BUFFER_DURATION (inbuffer) = GST_SECOND;
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  GST_BUFFER_OFFSET (inbuffer) = 0;
  gst_buffer_set_caps (inbuffer, caps);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);

  /* ... but it ends up being collected on the global buffer list */
  fail_unless_equals_int (g_list_length (buffers), 1);

  /* now reset and try again ... */
  fail_unless (gst_element_set_state (legacyresample,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to NULL");

  fail_unless (gst_element_set_state (legacyresample,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  newseg = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0, -1, 0);
  fail_unless (gst_pad_push_event (mysrcpad, newseg) != FALSE);

  inbuffer = gst_buffer_new_and_alloc (9343 * 4);
  memset (GST_BUFFER_DATA (inbuffer), 0, GST_BUFFER_SIZE (inbuffer));
  GST_BUFFER_DURATION (inbuffer) = GST_SECOND;
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  GST_BUFFER_OFFSET (inbuffer) = 0;
  gst_buffer_set_caps (inbuffer, caps);

  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);

  /* ... it also ends up being collected on the global buffer list. If we
   * now have more than 2 buffers, then legacyresample probably didn't clean
   * up its internal buffer properly and tried to push the remaining samples
   * when it got the second NEWSEGMENT event */
  fail_unless_equals_int (g_list_length (buffers), 2);

  cleanup_legacyresample (legacyresample);
  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_shutdown)
{
  GstElement *pipeline, *src, *cf1, *ar, *cf2, *sink;
  GstCaps *caps;
  guint i;

  /* create pipeline, force legacyresample to actually resample */
  pipeline = gst_pipeline_new (NULL);

  src = gst_check_setup_element ("audiotestsrc");
  cf1 = gst_check_setup_element ("capsfilter");
  ar = gst_check_setup_element ("legacyresample");
  cf2 = gst_check_setup_element ("capsfilter");
  g_object_set (cf2, "name", "capsfilter2", NULL);
  sink = gst_check_setup_element ("fakesink");

  caps =
      gst_caps_new_simple ("audio/x-raw-int", "rate", G_TYPE_INT, 11025, NULL);
  g_object_set (cf1, "caps", caps, NULL);
  gst_caps_unref (caps);

  caps =
      gst_caps_new_simple ("audio/x-raw-int", "rate", G_TYPE_INT, 48000, NULL);
  g_object_set (cf2, "caps", caps, NULL);
  gst_caps_unref (caps);

  /* don't want to sync against the clock, the more throughput the better */
  g_object_set (src, "is-live", FALSE, NULL);
  g_object_set (sink, "sync", FALSE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, cf1, ar, cf2, sink, NULL);
  fail_if (!gst_element_link_many (src, cf1, ar, cf2, sink, NULL));

  /* now, wait until pipeline is running and then shut it down again; repeat */
  for (i = 0; i < 20; ++i) {
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_get_state (pipeline, NULL, NULL, -1);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    g_usleep (100);
    gst_element_set_state (pipeline, GST_STATE_NULL);
  }

  gst_object_unref (pipeline);
}

GST_END_TEST;

static GstFlowReturn
live_switch_alloc_only_48000 (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf)
{
  GstStructure *structure;
  gint rate;
  gint channels;
  GstCaps *desired;

  structure = gst_caps_get_structure (caps, 0);
  fail_unless (gst_structure_get_int (structure, "rate", &rate));
  fail_unless (gst_structure_get_int (structure, "channels", &channels));

  if (rate < 48000)
    return GST_FLOW_NOT_NEGOTIATED;

  desired = gst_caps_copy (caps);
  gst_caps_set_simple (desired, "rate", G_TYPE_INT, 48000, NULL);

  *buf = gst_buffer_new_and_alloc (channels * 48000);
  gst_buffer_set_caps (*buf, desired);
  gst_caps_unref (desired);

  return GST_FLOW_OK;
}

static GstCaps *
live_switch_get_sink_caps (GstPad * pad)
{
  GstCaps *result;

  result = gst_caps_copy (GST_PAD_CAPS (pad));

  gst_caps_set_simple (result,
      "rate", GST_TYPE_INT_RANGE, 48000, G_MAXINT, NULL);

  return result;
}

static void
live_switch_push (int rate, GstCaps * caps)
{
  GstBuffer *inbuffer;
  GstCaps *desired;
  GList *l;

  desired = gst_caps_copy (caps);
  gst_caps_set_simple (desired, "rate", G_TYPE_INT, rate, NULL);

  fail_unless (gst_pad_alloc_buffer_and_set_caps (mysrcpad,
          GST_BUFFER_OFFSET_NONE, rate * 4, desired, &inbuffer) == GST_FLOW_OK);

  /* When the basetransform hits the non-configured case it always
   * returns a buffer with exactly the same caps as we requested so the actual
   * renegotiation (if needed) will be done in the _chain*/
  fail_unless (inbuffer != NULL);
  fail_unless (gst_caps_is_equal (desired, GST_BUFFER_CAPS (inbuffer)));

  memset (GST_BUFFER_DATA (inbuffer), 0, GST_BUFFER_SIZE (inbuffer));
  GST_BUFFER_DURATION (inbuffer) = GST_SECOND;
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  GST_BUFFER_OFFSET (inbuffer) = 0;

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);

  /* ... but it ends up being collected on the global buffer list */
  fail_unless_equals_int (g_list_length (buffers), 1);

  for (l = buffers; l; l = l->next) {
    GstBuffer *buffer = GST_BUFFER (l->data);

    gst_buffer_unref (buffer);
  }

  g_list_free (buffers);
  buffers = NULL;

  gst_caps_unref (desired);
}

GST_START_TEST (test_live_switch)
{
  GstElement *legacyresample;
  GstEvent *newseg;
  GstCaps *caps;

  legacyresample = setup_legacyresample (4, 48000, 48000);

  /* Let the sinkpad act like something that can only handle things of
   * rate 48000- and can only allocate buffers for that rate, but if someone
   * tries to get a buffer with a rate higher then 48000 tries to renegotiate
   * */
  gst_pad_set_bufferalloc_function (mysinkpad, live_switch_alloc_only_48000);
  gst_pad_set_getcaps_function (mysinkpad, live_switch_get_sink_caps);

  caps = gst_pad_get_negotiated_caps (mysrcpad);
  fail_unless (gst_caps_is_fixed (caps));

  fail_unless (gst_element_set_state (legacyresample,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  newseg = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0, -1, 0);
  fail_unless (gst_pad_push_event (mysrcpad, newseg) != FALSE);

  /* downstream can provide the requested rate, a buffer alloc will be passed
   * on */
  live_switch_push (48000, caps);

  /* Downstream can never accept this rate, buffer alloc isn't passed on */
  live_switch_push (40000, caps);

  /* Downstream can provide the requested rate but will re-negotiate */
  live_switch_push (50000, caps);

  cleanup_legacyresample (legacyresample);
  gst_caps_unref (caps);
}

GST_END_TEST static Suite *
legacyresample_suite (void)
{
  Suite *s = suite_create ("legacyresample");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_perfect_stream);
  tcase_add_test (tc_chain, test_discont_stream);
  tcase_add_test (tc_chain, test_reuse);
  tcase_add_test (tc_chain, test_shutdown);
  tcase_add_test (tc_chain, test_live_switch);

  return s;
}

GST_CHECK_MAIN (legacyresample);
