/* GStreamer
 *
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * audiochebyshevfreqlimit.c: Unit test for the audiochebyshevfreqlimit element
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

#define CAPS_STRING             \
    "audio/x-raw-float, "               \
    "channels = (int) 1, "              \
    "rate = (int) 44100, "              \
    "endianness = (int) BYTE_ORDER, "   \
    "width = (int) 64"                  \

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "channels = (int) 1, "
        "rate = (int) 44100, "
        "endianness = (int) BYTE_ORDER, " "width = (int) 64")
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "channels = (int) 1, "
        "rate = (int) 44100, "
        "endianness = (int) BYTE_ORDER, " "width = (int) 64")
    );

GstElement *
setup_audiochebyshevfreqlimit ()
{
  GstElement *audiochebyshevfreqlimit;

  GST_DEBUG ("setup_audiochebyshevfreqlimit");
  audiochebyshevfreqlimit = gst_check_setup_element ("audiochebyshevfreqlimit");
  mysrcpad =
      gst_check_setup_src_pad (audiochebyshevfreqlimit, &srctemplate, NULL);
  mysinkpad =
      gst_check_setup_sink_pad (audiochebyshevfreqlimit, &sinktemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return audiochebyshevfreqlimit;
}

void
cleanup_audiochebyshevfreqlimit (GstElement * audiochebyshevfreqlimit)
{
  GST_DEBUG ("cleanup_audiochebyshevfreqlimit");

  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (audiochebyshevfreqlimit);
  gst_check_teardown_sink_pad (audiochebyshevfreqlimit);
  gst_check_teardown_element (audiochebyshevfreqlimit);
}

/* Test if data containing only one frequency component
 * at 0 is preserved with lowpass mode and a cutoff
 * at rate/4 */
GST_START_TEST (test_lp_0hz)
{
  GstElement *audiochebyshevfreqlimit;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gdouble *in, *res, rms;
  gint i;

  audiochebyshevfreqlimit = setup_audiochebyshevfreqlimit ();
  /* Set to lowpass */
  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "mode", 0, NULL);
  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "poles", 8, NULL);
  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "type", 1, NULL);
  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "ripple", 0.25, NULL);

  fail_unless (gst_element_set_state (audiochebyshevfreqlimit,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "cutoff", 44100 / 4.0,
      NULL);
  inbuffer = gst_buffer_new_and_alloc (128 * sizeof (gdouble));
  in = (gdouble *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 128; i++)
    in[i] = 1.0;

  caps = gst_caps_from_string (CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gdouble *) GST_BUFFER_DATA (outbuffer);

  rms = 0.0;
  for (i = 0; i < 128; i++)
    rms += res[i] * res[i];
  rms = sqrt (rms / 128.0);
  fail_unless (rms >= 0.9);

  /* cleanup */
  cleanup_audiochebyshevfreqlimit (audiochebyshevfreqlimit);
}

GST_END_TEST;

/* Test if data containing only one frequency component
 * at rate/2 is erased with lowpass mode and a cutoff
 * at rate/4 */
GST_START_TEST (test_lp_22050hz)
{
  GstElement *audiochebyshevfreqlimit;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gdouble *in, *res, rms;
  gint i;

  audiochebyshevfreqlimit = setup_audiochebyshevfreqlimit ();
  /* Set to lowpass */
  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "mode", 0, NULL);
  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "poles", 8, NULL);
  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "type", 1, NULL);
  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "ripple", 0.25, NULL);

  fail_unless (gst_element_set_state (audiochebyshevfreqlimit,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "cutoff", 44100 / 4.0,
      NULL);
  inbuffer = gst_buffer_new_and_alloc (128 * sizeof (gdouble));
  in = (gdouble *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 128; i += 2) {
    in[i] = 1.0;
    in[i + 1] = -1.0;
  }

  caps = gst_caps_from_string (CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gdouble *) GST_BUFFER_DATA (outbuffer);

  rms = 0.0;
  for (i = 0; i < 128; i++)
    rms += res[i] * res[i];
  rms = sqrt (rms / 128.0);
  fail_unless (rms <= 0.1);

  /* cleanup */
  cleanup_audiochebyshevfreqlimit (audiochebyshevfreqlimit);
}

GST_END_TEST;

/* Test if data containing only one frequency component
 * at 0 is erased with highpass mode and a cutoff
 * at rate/4 */
GST_START_TEST (test_hp_0hz)
{
  GstElement *audiochebyshevfreqlimit;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gdouble *in, *res, rms;
  gint i;

  audiochebyshevfreqlimit = setup_audiochebyshevfreqlimit ();
  /* Set to highpass */
  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "mode", 1, NULL);
  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "poles", 8, NULL);
  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "type", 1, NULL);
  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "ripple", 0.25, NULL);

  fail_unless (gst_element_set_state (audiochebyshevfreqlimit,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "cutoff", 44100 / 4.0,
      NULL);
  inbuffer = gst_buffer_new_and_alloc (128 * sizeof (gdouble));
  in = (gdouble *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 128; i++)
    in[i] = 1.0;

  caps = gst_caps_from_string (CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gdouble *) GST_BUFFER_DATA (outbuffer);

  rms = 0.0;
  for (i = 0; i < 128; i++)
    rms += res[i] * res[i];
  rms = sqrt (rms / 128.0);
  fail_unless (rms <= 0.1);

  /* cleanup */
  cleanup_audiochebyshevfreqlimit (audiochebyshevfreqlimit);
}

GST_END_TEST;

/* Test if data containing only one frequency component
 * at rate/2 is preserved with highpass mode and a cutoff
 * at rate/4 */
GST_START_TEST (test_hp_22050hz)
{
  GstElement *audiochebyshevfreqlimit;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gdouble *in, *res, rms;
  gint i;

  audiochebyshevfreqlimit = setup_audiochebyshevfreqlimit ();
  /* Set to highpass */
  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "mode", 1, NULL);
  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "poles", 8, NULL);
  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "type", 1, NULL);
  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "ripple", 0.25, NULL);

  fail_unless (gst_element_set_state (audiochebyshevfreqlimit,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  g_object_set (G_OBJECT (audiochebyshevfreqlimit), "cutoff", 44100 / 4.0,
      NULL);
  inbuffer = gst_buffer_new_and_alloc (128 * sizeof (gdouble));
  in = (gdouble *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 128; i += 2) {
    in[i] = 1.0;
    in[i + 1] = -1.0;
  }

  caps = gst_caps_from_string (CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gdouble *) GST_BUFFER_DATA (outbuffer);

  rms = 0.0;
  for (i = 0; i < 128; i++)
    rms += res[i] * res[i];
  rms = sqrt (rms / 128.0);
  fail_unless (rms >= 0.9);

  /* cleanup */
  cleanup_audiochebyshevfreqlimit (audiochebyshevfreqlimit);
}

GST_END_TEST;

Suite *
audiochebyshevfreqlimit_suite (void)
{
  Suite *s = suite_create ("audiochebyshevfreqlimit");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_lp_0hz);
  tcase_add_test (tc_chain, test_lp_22050hz);
  tcase_add_test (tc_chain, test_hp_0hz);
  tcase_add_test (tc_chain, test_hp_22050hz);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = audiochebyshevfreqlimit_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
