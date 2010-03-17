/* GStreamer ReplayGain limiter
 *
 * Copyright (C) 2007 Rene Stadler <mail@renestadler.de>
 *
 * rglimiter.c: Unit test for the rglimiter element
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <gst/check/gstcheck.h>

#include <math.h>

GList *buffers = NULL;

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;

#define RG_LIMITER_CAPS_TEMPLATE_STRING   \
  "audio/x-raw-float, "                   \
  "width = (int) 32, "                    \
  "endianness = (int) BYTE_ORDER, "       \
  "channels = (int) [ 1, MAX ], "         \
  "rate = (int) [ 1, MAX ]"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (RG_LIMITER_CAPS_TEMPLATE_STRING)
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (RG_LIMITER_CAPS_TEMPLATE_STRING)
    );

static GstElement *
setup_rglimiter (void)
{
  GstElement *element;

  GST_DEBUG ("setup_rglimiter");
  element = gst_check_setup_element ("rglimiter");
  mysrcpad = gst_check_setup_src_pad (element, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (element, &sinktemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return element;
}

static void
cleanup_rglimiter (GstElement * element)
{
  GST_DEBUG ("cleanup_rglimiter");

  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;

  gst_check_teardown_src_pad (element);
  gst_check_teardown_sink_pad (element);
  gst_check_teardown_element (element);
}

static void
set_playing_state (GstElement * element)
{
  fail_unless (gst_element_set_state (element,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "Could not set state to PLAYING");
}

static const gfloat test_input[] = {
  -2.0, -1.0, -0.75, -0.5, -0.25, 0.0, 0.25, 0.5, 0.75, 1.0, 2.0
};

static const gfloat test_output[] = {
  -0.99752737684336523,         /* -2.0  */
  -0.88079707797788243,         /* -1.0  */
  -0.7310585786300049,          /* -0.75 */
  -0.5, -0.25, 0.0, 0.25, 0.5,
  0.7310585786300049,           /* 0.75 */
  0.88079707797788243,          /* 1.0  */
  0.99752737684336523,          /* 2.0  */
};

static GstBuffer *
create_test_buffer ()
{
  GstBuffer *buf = gst_buffer_new_and_alloc (sizeof (test_input));
  GstCaps *caps;

  memcpy (GST_BUFFER_DATA (buf), test_input, sizeof (test_input));

  caps = gst_caps_new_simple ("audio/x-raw-float",
      "rate", G_TYPE_INT, 44100, "channels", G_TYPE_INT, 1,
      "endianness", G_TYPE_INT, G_BYTE_ORDER, "width", G_TYPE_INT, 32, NULL);
  gst_buffer_set_caps (buf, caps);
  gst_caps_unref (caps);

  ASSERT_BUFFER_REFCOUNT (buf, "buf", 1);

  return buf;
}

static void
verify_test_buffer (GstBuffer * buf)
{
  gfloat *output = (gfloat *) GST_BUFFER_DATA (buf);
  gint i;

  fail_unless (GST_BUFFER_SIZE (buf) == sizeof (test_output));
  for (i = 0; i < G_N_ELEMENTS (test_input); i++)
    fail_unless (ABS (output[i] - test_output[i]) < 1.e-6,
        "Incorrect output value %.6f for input %.2f, expected %.6f",
        output[i], test_input[i], test_output[i]);
}

/* Start of tests. */

GST_START_TEST (test_no_buffer)
{
  GstElement *element = setup_rglimiter ();

  set_playing_state (element);

  cleanup_rglimiter (element);
}

GST_END_TEST;

GST_START_TEST (test_disabled)
{
  GstElement *element = setup_rglimiter ();
  GstBuffer *buf, *out_buf;

  g_object_set (element, "enabled", FALSE, NULL);
  set_playing_state (element);

  buf = create_test_buffer ();
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  fail_unless (g_list_length (buffers) == 1);
  out_buf = buffers->data;
  fail_if (out_buf == NULL);
  buffers = g_list_remove (buffers, out_buf);
  ASSERT_BUFFER_REFCOUNT (out_buf, "out_buf", 1);
  fail_unless (buf == out_buf);
  gst_buffer_unref (out_buf);

  cleanup_rglimiter (element);
}

GST_END_TEST;

GST_START_TEST (test_limiting)
{
  GstElement *element = setup_rglimiter ();
  GstBuffer *buf, *out_buf;

  set_playing_state (element);

  /* Mutable variant. */
  buf = create_test_buffer ();
  GST_DEBUG ("push mutable buffer");
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  fail_unless (g_list_length (buffers) == 1);
  out_buf = buffers->data;
  fail_if (out_buf == NULL);
  ASSERT_BUFFER_REFCOUNT (out_buf, "out_buf", 1);
  verify_test_buffer (out_buf);

  /* Immutable variant. */
  buf = create_test_buffer ();
  /* Extra ref: */
  gst_buffer_ref (buf);
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 2);
  GST_DEBUG ("push immutable buffer");
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  ASSERT_BUFFER_REFCOUNT (buf, "buf", 1);
  fail_unless (g_list_length (buffers) == 2);
  out_buf = g_list_last (buffers)->data;
  fail_if (out_buf == NULL);
  ASSERT_BUFFER_REFCOUNT (out_buf, "out_buf", 1);
  fail_unless (buf != out_buf);
  /* Drop our extra ref: */
  gst_buffer_unref (buf);
  verify_test_buffer (out_buf);

  cleanup_rglimiter (element);
}

GST_END_TEST;

GST_START_TEST (test_gap)
{
  GstElement *element = setup_rglimiter ();
  GstBuffer *buf, *out_buf;

  set_playing_state (element);

  buf = create_test_buffer ();
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_GAP);
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  fail_unless (g_list_length (buffers) == 1);
  out_buf = buffers->data;
  fail_if (out_buf == NULL);
  ASSERT_BUFFER_REFCOUNT (out_buf, "out_buf", 1);

  /* Verify that the baseclass does not lift the GAP flag: */
  fail_unless (GST_BUFFER_FLAG_IS_SET (out_buf, GST_BUFFER_FLAG_GAP));

  g_assert (GST_BUFFER_SIZE (out_buf) == GST_BUFFER_SIZE (buf));
  /* We cheated by passing an input buffer with non-silence that has the GAP
   * flag set.  The element cannot know that however and must have skipped
   * adjusting the buffer because of the flag, which we can easily verify: */
  fail_if (memcmp (GST_BUFFER_DATA (out_buf),
          GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (out_buf)) != 0);

  cleanup_rglimiter (element);
}

GST_END_TEST;

static Suite *
rglimiter_suite (void)
{
  Suite *s = suite_create ("rglimiter");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_no_buffer);
  tcase_add_test (tc_chain, test_disabled);
  tcase_add_test (tc_chain, test_limiting);
  tcase_add_test (tc_chain, test_gap);

  return s;
}

int
main (int argc, char **argv)
{
  gint nf;

  Suite *s = rglimiter_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_ENV);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
