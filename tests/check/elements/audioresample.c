/* GStreamer
 *
 * unit test for audioresample
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

#include <unistd.h>

#include <gst/check/gstcheck.h>

GList *buffers = NULL;
gboolean have_eos = FALSE;

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysrcpad, *mysinkpad;


#define RESAMPLE_CAPS_TEMPLATE_STRING	\
    "audio/x-raw-int, "			\
    "channels = (int) [ 1, MAX ], "	\
    "rate = (int) [ 1,  MAX ], "	\
    "endianness = (int) BYTE_ORDER, "	\
    "width = (int) 16, "		\
    "depth = (int) 16, "		\
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

GstElement *
setup_audioresample (int channels, int inrate, int outrate)
{
  GstElement *audioresample;
  GstCaps *caps;
  GstStructure *structure;
  GstPad *pad;

  GST_DEBUG ("setup_audioresample");
  audioresample = gst_check_setup_element ("audioresample");

  caps = gst_caps_from_string (RESAMPLE_CAPS_TEMPLATE_STRING);
  structure = gst_caps_get_structure (caps, 0);
  gst_structure_set (structure, "channels", G_TYPE_INT, channels,
      "rate", G_TYPE_INT, inrate, NULL);
  fail_unless (gst_caps_is_fixed (caps));

  mysrcpad = gst_check_setup_src_pad (audioresample, &srctemplate, caps);
  pad = gst_pad_get_peer (mysrcpad);
  gst_pad_set_caps (pad, caps);
  gst_object_unref (GST_OBJECT (pad));
  gst_caps_unref (caps);
  gst_pad_set_active (mysrcpad, TRUE);

  caps = gst_caps_from_string (RESAMPLE_CAPS_TEMPLATE_STRING);
  structure = gst_caps_get_structure (caps, 0);
  gst_structure_set (structure, "channels", G_TYPE_INT, channels,
      "rate", G_TYPE_INT, outrate, NULL);
  fail_unless (gst_caps_is_fixed (caps));

  mysinkpad = gst_check_setup_sink_pad (audioresample, &sinktemplate, caps);
  /* this installs a getcaps func that will always return the caps we set
   * later */
  gst_pad_use_fixed_caps (mysinkpad);
  pad = gst_pad_get_peer (mysinkpad);
  gst_pad_set_caps (pad, caps);
  gst_object_unref (GST_OBJECT (pad));
  gst_caps_unref (caps);
  gst_pad_set_active (mysinkpad, TRUE);

  return audioresample;
}

void
cleanup_audioresample (GstElement * audioresample)
{
  GST_DEBUG ("cleanup_audioresample");

  gst_check_teardown_src_pad (audioresample);
  gst_check_teardown_sink_pad (audioresample);
  gst_check_teardown_element (audioresample);
}

static void
fail_unless_perfect_stream ()
{
  guint64 timestamp = 0L, duration = 0L;
  guint64 offset = 0L, offset_end = 0L;

  GList *l;
  GstBuffer *buffer;

  for (l = buffers; l; l = l->next) {
    buffer = GST_BUFFER (l->data);
    ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
    GST_DEBUG ("buffer timestamp %" G_GUINT64_FORMAT ", duration %"
        G_GUINT64_FORMAT, GST_BUFFER_TIMESTAMP (buffer),
        GST_BUFFER_DURATION (buffer));

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

static void
test_perfect_stream_instance (int inrate, int outrate, int samples,
    int numbuffers)
{
  GstElement *audioresample;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;

  int i, j;
  gint16 *p;

  audioresample = setup_audioresample (2, inrate, outrate);
  caps = gst_pad_get_negotiated_caps (mysrcpad);
  fail_unless (gst_caps_is_fixed (caps));

  fail_unless (gst_element_set_state (audioresample,
          GST_STATE_PLAYING) == GST_STATE_SUCCESS, "could not set to playing");

  for (j = 1; j <= numbuffers; ++j) {

    inbuffer = gst_buffer_new_and_alloc (samples * 4);
    GST_BUFFER_DURATION (inbuffer) = samples * GST_SECOND / inrate;
    GST_BUFFER_TIMESTAMP (inbuffer) = GST_BUFFER_DURATION (inbuffer) * (j - 1);
    GST_BUFFER_OFFSET (inbuffer) = 0;
    GST_BUFFER_OFFSET_END (inbuffer) = samples;

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

  /* FIXME: we should make audioresample handle eos by flushing out the last
   * samples, which will give us one more, small, buffer */
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);

  fail_unless_perfect_stream ();

  /* cleanup */
  gst_caps_unref (caps);
  cleanup_audioresample (audioresample);
}


/* make sure that outgoing buffers are contiguous in timestamp/duration and
 * offset/offsetend
 */
GST_START_TEST (test_perfect_stream)
{
  guint inrate, outrate, bytes;

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

Suite *
audioresample_suite (void)
{
  Suite *s = suite_create ("audioresample");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_perfect_stream);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = audioresample_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
