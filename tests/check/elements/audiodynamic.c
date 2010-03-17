/* GStreamer
 *
 * unit test for audiodynamic
 *
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * Greatly based on the audiopanorama unit test
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
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

#include <gst/base/gstbasetransform.h>
#include <gst/check/gstcheck.h>

gboolean have_eos = FALSE;

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysrcpad, *mysinkpad;


#define DYNAMIC_CAPS_STRING    \
    "audio/x-raw-int, "                 \
    "channels = (int) 1, "              \
    "rate = (int) 44100, "              \
    "endianness = (int) BYTE_ORDER, "   \
    "width = (int) 16, "                \
    "depth = (int) 16, "                \
    "signed = (bool) TRUE"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "channels = (int) 1, "
        "rate = (int) [ 1,  MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, " "depth = (int) 16, " "signed = (bool) TRUE")
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "channels = (int) 1, "
        "rate = (int) [ 1,  MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, " "depth = (int) 16, " "signed = (bool) TRUE")
    );

static GstElement *
setup_dynamic (void)
{
  GstElement *dynamic;

  GST_DEBUG ("setup_dynamic");
  dynamic = gst_check_setup_element ("audiodynamic");
  mysrcpad = gst_check_setup_src_pad (dynamic, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (dynamic, &sinktemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return dynamic;
}

static void
cleanup_dynamic (GstElement * dynamic)
{
  GST_DEBUG ("cleanup_dynamic");

  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (dynamic);
  gst_check_teardown_sink_pad (dynamic);
  gst_check_teardown_element (dynamic);
}

GST_START_TEST (test_passthrough)
{
  GstElement *dynamic;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[6] = { 24576, -16384, 256, -128, 0, -24576 };
  gint16 *res;

  dynamic = setup_dynamic ();
  fail_unless (gst_element_set_state (dynamic,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (12);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 12);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 12) == 0);
  caps = gst_caps_from_string (DYNAMIC_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being collected on the global buffer list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO
      ("expected %+5d %+5d %+5d %+5d %+5d %+5d real %+5d %+5d %+5d %+5d %+5d %+5d",
      in[0], in[1], in[2], in[3], in[4], in[5], res[0], res[1], res[2], res[3],
      res[4], res[5]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), in, 12) == 0);

  /* cleanup */
  cleanup_dynamic (dynamic);
}

GST_END_TEST;

GST_START_TEST (test_compress_hard_50_50)
{
  GstElement *dynamic;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[8] = { -30000, 24576, -16384, 256, -128, 0, -24576, 30000 };
  gint16 *res;

  dynamic = setup_dynamic ();
  g_object_set (G_OBJECT (dynamic), "mode", 0, NULL);
  g_object_set (G_OBJECT (dynamic), "characteristics", 0, NULL);
  g_object_set (G_OBJECT (dynamic), "ratio", 0.5, NULL);
  g_object_set (G_OBJECT (dynamic), "threshold", 0.5, NULL);
  fail_unless (gst_element_set_state (dynamic,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (16);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 16);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 16) == 0);
  caps = gst_caps_from_string (DYNAMIC_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (outbuffer);

  fail_unless (res[0] > in[0]);
  fail_unless (res[1] < in[1]);
  fail_unless (res[2] == in[2]);
  fail_unless (res[3] == in[3]);
  fail_unless (res[4] == in[4]);
  fail_unless (res[5] == in[5]);
  fail_unless (res[6] > in[6]);
  fail_unless (res[7] < in[7]);

  /* cleanup */
  cleanup_dynamic (dynamic);
}

GST_END_TEST;

GST_START_TEST (test_compress_soft_50_50)
{
  GstElement *dynamic;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[8] = { -30000, 24576, -16384, 256, -128, 0, -24576, 30000 };
  gint16 *res;

  dynamic = setup_dynamic ();
  g_object_set (G_OBJECT (dynamic), "mode", 0, NULL);
  g_object_set (G_OBJECT (dynamic), "characteristics", 1, NULL);
  g_object_set (G_OBJECT (dynamic), "ratio", 0.5, NULL);
  g_object_set (G_OBJECT (dynamic), "threshold", 0.5, NULL);
  fail_unless (gst_element_set_state (dynamic,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (16);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 16);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 16) == 0);
  caps = gst_caps_from_string (DYNAMIC_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (outbuffer);

  fail_unless (res[0] > in[0]);
  fail_unless (res[1] < in[1]);
  fail_unless (res[2] == in[2]);
  fail_unless (res[3] == in[3]);
  fail_unless (res[4] == in[4]);
  fail_unless (res[5] == in[5]);
  fail_unless (res[6] > in[6]);
  fail_unless (res[7] < in[7]);

  /* cleanup */
  cleanup_dynamic (dynamic);
}

GST_END_TEST;

GST_START_TEST (test_compress_hard_100_50)
{
  GstElement *dynamic;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[8] = { -30000, 24576, -16384, 256, -128, 0, -24576, 30000 };
  gint16 *res;

  dynamic = setup_dynamic ();
  g_object_set (G_OBJECT (dynamic), "mode", 0, NULL);
  g_object_set (G_OBJECT (dynamic), "characteristics", 0, NULL);
  g_object_set (G_OBJECT (dynamic), "ratio", 0.5, NULL);
  g_object_set (G_OBJECT (dynamic), "threshold", 1.0, NULL);
  fail_unless (gst_element_set_state (dynamic,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (16);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 16);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 16) == 0);
  caps = gst_caps_from_string (DYNAMIC_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (outbuffer);

  fail_unless (res[0] == in[0]);
  fail_unless (res[1] == in[1]);
  fail_unless (res[2] == in[2]);
  fail_unless (res[3] == in[3]);
  fail_unless (res[4] == in[4]);
  fail_unless (res[5] == in[5]);
  fail_unless (res[6] == in[6]);
  fail_unless (res[7] == in[7]);

  /* cleanup */
  cleanup_dynamic (dynamic);
}

GST_END_TEST;


GST_START_TEST (test_expand_hard_50_200)
{
  GstElement *dynamic;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[8] = { -30000, 24576, -16383, 256, -128, 0, -24576, 30000 };
  gint16 *res;

  dynamic = setup_dynamic ();
  g_object_set (G_OBJECT (dynamic), "mode", 1, NULL);
  g_object_set (G_OBJECT (dynamic), "characteristics", 0, NULL);
  g_object_set (G_OBJECT (dynamic), "ratio", 2.0, NULL);
  g_object_set (G_OBJECT (dynamic), "threshold", 0.5, NULL);
  fail_unless (gst_element_set_state (dynamic,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (16);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 16);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 16) == 0);
  caps = gst_caps_from_string (DYNAMIC_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (outbuffer);

  fail_unless (res[0] == in[0]);
  fail_unless (res[1] == in[1]);
  fail_unless (res[2] > in[2]);
  fail_unless (res[3] < in[3]);
  fail_unless (res[4] > in[4]);
  fail_unless (res[5] == in[5]);
  fail_unless (res[6] == in[6]);
  fail_unless (res[7] == in[7]);

  /* cleanup */
  cleanup_dynamic (dynamic);
}

GST_END_TEST;

GST_START_TEST (test_expand_soft_50_200)
{
  GstElement *dynamic;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[8] = { -30000, 24576, -16383, 256, -128, 0, -24576, 30000 };
  gint16 *res;

  dynamic = setup_dynamic ();
  g_object_set (G_OBJECT (dynamic), "mode", 1, NULL);
  g_object_set (G_OBJECT (dynamic), "characteristics", 1, NULL);
  g_object_set (G_OBJECT (dynamic), "ratio", 2.0, NULL);
  g_object_set (G_OBJECT (dynamic), "threshold", 0.5, NULL);
  fail_unless (gst_element_set_state (dynamic,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (16);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 16);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 16) == 0);
  caps = gst_caps_from_string (DYNAMIC_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (outbuffer);

  fail_unless (res[0] == in[0]);
  fail_unless (res[1] == in[1]);
  fail_unless (res[2] > in[2]);
  fail_unless (res[3] < in[3]);
  fail_unless (res[4] > in[4]);
  fail_unless (res[5] == in[5]);
  fail_unless (res[6] == in[6]);
  fail_unless (res[7] == in[7]);

  /* cleanup */
  cleanup_dynamic (dynamic);
}

GST_END_TEST;

GST_START_TEST (test_expand_hard_0_200)
{
  GstElement *dynamic;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[8] = { -30000, 24576, -16383, 256, -128, 0, -24576, 30000 };
  gint16 *res;

  dynamic = setup_dynamic ();
  g_object_set (G_OBJECT (dynamic), "mode", 1, NULL);
  g_object_set (G_OBJECT (dynamic), "characteristics", 0, NULL);
  g_object_set (G_OBJECT (dynamic), "ratio", 2.0, NULL);
  g_object_set (G_OBJECT (dynamic), "threshold", 0.0, NULL);
  fail_unless (gst_element_set_state (dynamic,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (16);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 16);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 16) == 0);
  caps = gst_caps_from_string (DYNAMIC_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (outbuffer);

  fail_unless (res[0] == in[0]);
  fail_unless (res[1] == in[1]);
  fail_unless (res[2] == in[2]);
  fail_unless (res[3] == in[3]);
  fail_unless (res[4] == in[4]);
  fail_unless (res[5] == in[5]);
  fail_unless (res[6] == in[6]);
  fail_unless (res[7] == in[7]);

  /* cleanup */
  cleanup_dynamic (dynamic);
}

GST_END_TEST;

static Suite *
dynamic_suite (void)
{
  Suite *s = suite_create ("dynamic");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_passthrough);
  tcase_add_test (tc_chain, test_compress_hard_50_50);
  tcase_add_test (tc_chain, test_compress_soft_50_50);
  tcase_add_test (tc_chain, test_compress_hard_100_50);
  tcase_add_test (tc_chain, test_expand_hard_50_200);
  tcase_add_test (tc_chain, test_expand_soft_50_200);
  tcase_add_test (tc_chain, test_expand_hard_0_200);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = dynamic_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
