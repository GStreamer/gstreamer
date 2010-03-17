/* GStreamer
 *
 * Copyright (C) 2008 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * equalizer.c: Unit test for the equalizer element
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

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/check/gstcheck.h>

#include <math.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysrcpad, *mysinkpad;

#define EQUALIZER_CAPS_STRING             \
    "audio/x-raw-float, "               \
    "channels = (int) 1, "              \
    "rate = (int) 48000, "              \
    "endianness = (int) BYTE_ORDER, "   \
    "width = (int) 64"                  \

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "channels = (int) 1, "
        "rate = (int) 48000, "
        "endianness = (int) BYTE_ORDER, " "width = (int) 64 ")
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "channels = (int) 1, "
        "rate = (int) 48000, "
        "endianness = (int) BYTE_ORDER, " "width = (int) 64 ")
    );

static GstElement *
setup_equalizer (void)
{
  GstElement *equalizer;

  GST_DEBUG ("setup_equalizer");
  equalizer = gst_check_setup_element ("equalizer-nbands");
  mysrcpad = gst_check_setup_src_pad (equalizer, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (equalizer, &sinktemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return equalizer;
}

static void
cleanup_equalizer (GstElement * equalizer)
{
  GST_DEBUG ("cleanup_equalizer");

  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (equalizer);
  gst_check_teardown_sink_pad (equalizer);
  gst_check_teardown_element (equalizer);
}

GST_START_TEST (test_equalizer_5bands_passthrough)
{
  GstElement *equalizer;
  GstBuffer *inbuffer;
  GstCaps *caps;
  gdouble *in, *res;
  gint i;

  equalizer = setup_equalizer ();
  g_object_set (G_OBJECT (equalizer), "num-bands", 5, NULL);

  fail_unless_equals_int (gst_child_proxy_get_children_count (GST_CHILD_PROXY
          (equalizer)), 5);

  fail_unless (gst_element_set_state (equalizer,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (1024 * sizeof (gdouble));
  in = (gdouble *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 1024; i++)
    in[i] = g_random_double_range (-1.0, 1.0);

  caps = gst_caps_from_string (EQUALIZER_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) == 1);

  res = (gdouble *) GST_BUFFER_DATA (GST_BUFFER (buffers->data));

  for (i = 0; i < 1024; i++)
    fail_unless_equals_float (in[i], res[i]);

  /* cleanup */
  cleanup_equalizer (equalizer);
}

GST_END_TEST;

GST_START_TEST (test_equalizer_5bands_minus_24)
{
  GstElement *equalizer;
  GstBuffer *inbuffer;
  GstCaps *caps;
  gdouble *in, *res, rms_in, rms_out;
  gint i;

  equalizer = setup_equalizer ();
  g_object_set (G_OBJECT (equalizer), "num-bands", 5, NULL);

  fail_unless_equals_int (gst_child_proxy_get_children_count (GST_CHILD_PROXY
          (equalizer)), 5);

  for (i = 0; i < 5; i++) {
    GstObject *band =
        gst_child_proxy_get_child_by_index (GST_CHILD_PROXY (equalizer), i);
    fail_unless (band != NULL);

    g_object_set (G_OBJECT (band), "gain", -24.0, NULL);
    g_object_unref (G_OBJECT (band));
  }

  fail_unless (gst_element_set_state (equalizer,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (1024 * sizeof (gdouble));
  in = (gdouble *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 1024; i++)
    in[i] = g_random_double_range (-1.0, 1.0);

  rms_in = 0.0;
  for (i = 0; i < 1024; i++)
    rms_in += in[i] * in[i];
  rms_in = sqrt (rms_in / 1024);

  caps = gst_caps_from_string (EQUALIZER_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) == 1);

  res = (gdouble *) GST_BUFFER_DATA (GST_BUFFER (buffers->data));

  rms_out = 0.0;
  for (i = 0; i < 1024; i++)
    rms_out += res[i] * res[i];
  rms_out = sqrt (rms_out / 1024);

  fail_unless (rms_in > rms_out);

  /* cleanup */
  cleanup_equalizer (equalizer);
}

GST_END_TEST;

GST_START_TEST (test_equalizer_5bands_plus_12)
{
  GstElement *equalizer;
  GstBuffer *inbuffer;
  GstCaps *caps;
  gdouble *in, *res, rms_in, rms_out;
  gint i;

  equalizer = setup_equalizer ();
  g_object_set (G_OBJECT (equalizer), "num-bands", 5, NULL);

  fail_unless_equals_int (gst_child_proxy_get_children_count (GST_CHILD_PROXY
          (equalizer)), 5);

  for (i = 0; i < 5; i++) {
    GstObject *band =
        gst_child_proxy_get_child_by_index (GST_CHILD_PROXY (equalizer), i);
    fail_unless (band != NULL);

    g_object_set (G_OBJECT (band), "gain", 12.0, NULL);
    g_object_unref (G_OBJECT (band));
  }

  fail_unless (gst_element_set_state (equalizer,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (1024 * sizeof (gdouble));
  in = (gdouble *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 1024; i++)
    in[i] = g_random_double_range (-1.0, 1.0);

  rms_in = 0.0;
  for (i = 0; i < 1024; i++)
    rms_in += in[i] * in[i];
  rms_in = sqrt (rms_in / 1024);

  caps = gst_caps_from_string (EQUALIZER_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) == 1);

  res = (gdouble *) GST_BUFFER_DATA (GST_BUFFER (buffers->data));

  rms_out = 0.0;
  for (i = 0; i < 1024; i++)
    rms_out += res[i] * res[i];
  rms_out = sqrt (rms_out / 1024);

  fail_unless (rms_in < rms_out);

  /* cleanup */
  cleanup_equalizer (equalizer);
}

GST_END_TEST;

GST_START_TEST (test_equalizer_band_number_changing)
{
  GstElement *equalizer;
  gint i;

  equalizer = setup_equalizer ();

  g_object_set (G_OBJECT (equalizer), "num-bands", 5, NULL);
  fail_unless_equals_int (gst_child_proxy_get_children_count (GST_CHILD_PROXY
          (equalizer)), 5);

  for (i = 0; i < 5; i++) {
    GstObject *band;

    band = gst_child_proxy_get_child_by_index (GST_CHILD_PROXY (equalizer), i);
    fail_unless (band != NULL);
    gst_object_unref (band);
  }

  g_object_set (G_OBJECT (equalizer), "num-bands", 10, NULL);
  fail_unless_equals_int (gst_child_proxy_get_children_count (GST_CHILD_PROXY
          (equalizer)), 10);

  for (i = 0; i < 10; i++) {
    GstObject *band;

    band = gst_child_proxy_get_child_by_index (GST_CHILD_PROXY (equalizer), i);
    fail_unless (band != NULL);
    gst_object_unref (band);
  }

  /* cleanup */
  cleanup_equalizer (equalizer);
}

GST_END_TEST;

static Suite *
equalizer_suite (void)
{
  Suite *s = suite_create ("equalizer");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_equalizer_5bands_passthrough);
  tcase_add_test (tc_chain, test_equalizer_5bands_minus_24);
  tcase_add_test (tc_chain, test_equalizer_5bands_plus_12);
  tcase_add_test (tc_chain, test_equalizer_band_number_changing);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = equalizer_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
