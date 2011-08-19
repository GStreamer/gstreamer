/* GStreamer
 *
 * unit test for level
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
#include <math.h>

#include <gst/check/gstcheck.h>

gboolean have_eos = FALSE;

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysrcpad, *mysinkpad;

#define LEVEL_CAPS_TEMPLATE_STRING \
  "audio/x-raw, " \
    "format = (string) { S8, "GST_AUDIO_NE(S16)" }, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ]"

#define LEVEL_CAPS_STRING \
  "audio/x-raw, " \
    "format = (string) "GST_AUDIO_NE(S16)", " \
    "rate = (int) 1000, " \
    "channels = (int) 2"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (LEVEL_CAPS_TEMPLATE_STRING)
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (LEVEL_CAPS_TEMPLATE_STRING)
    );

/* takes over reference for outcaps */
static GstElement *
setup_level (void)
{
  GstElement *level;

  GST_DEBUG ("setup_level");
  level = gst_check_setup_element ("level");
  mysrcpad = gst_check_setup_src_pad (level, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (level, &sinktemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return level;
}

static void
cleanup_level (GstElement * level)
{
  GST_DEBUG ("cleanup_level");

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (level);
  gst_check_teardown_sink_pad (level);
  gst_check_teardown_element (level);
}


GST_START_TEST (test_int16)
{
  GstElement *level;
  GstBuffer *inbuffer, *outbuffer;
  GstBus *bus;
  GstCaps *caps;
  GstMessage *message;
  const GstStructure *structure;
  int i, j;
  gint16 *data;
  const GValue *list, *value;
  GstClockTime endtime;
  gdouble dB;

  level = setup_level ();
  g_object_set (level, "message", TRUE, "interval", GST_SECOND / 10, NULL);

  fail_unless (gst_element_set_state (level,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  /* create a fake 0.1 sec buffer with a half-amplitude block signal */
  inbuffer = gst_buffer_new_and_alloc (400);
  data = (gint16 *) GST_BUFFER_DATA (inbuffer);
  for (j = 0; j < 200; ++j) {
    *data = 16536;
    ++data;
  }
  caps = gst_caps_from_string (LEVEL_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* create a bus to get the level message on */
  bus = gst_bus_new ();
  ASSERT_OBJECT_REFCOUNT (bus, "bus", 1);
  gst_element_set_bus (level, bus);
  ASSERT_OBJECT_REFCOUNT (bus, "bus", 2);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);

  message = gst_bus_poll (bus, GST_MESSAGE_ELEMENT, -1);
  ASSERT_OBJECT_REFCOUNT (message, "message", 1);

  fail_unless (message != NULL);
  fail_unless (GST_MESSAGE_SRC (message) == GST_OBJECT (level));
  fail_unless (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ELEMENT);
  structure = gst_message_get_structure (message);
  fail_if (structure == NULL);
  fail_unless_equals_string ((char *) gst_structure_get_name (structure),
      "level");
  fail_unless (gst_structure_get_clock_time (structure, "endtime", &endtime));

  /* block wave of half amplitude has -5.94 dB for rms, peak and decay */
  for (i = 0; i < 2; ++i) {
    const gchar *fields[3] = { "rms", "peak", "decay" };
    for (j = 0; j < 3; ++j) {
      list = gst_structure_get_value (structure, fields[j]);
      value = gst_value_list_get_value (list, i);
      dB = g_value_get_double (value);
      GST_DEBUG ("%s is %lf", fields[j], dB);
      fail_if (dB < -6.0);
      fail_if (dB > -5.9);
    }
  }
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);

  /* clean up */
  /* flush current messages,and future state change messages */
  gst_bus_set_flushing (bus, TRUE);

  /* message has a ref to the element */
  ASSERT_OBJECT_REFCOUNT (level, "level", 2);
  gst_message_unref (message);
  ASSERT_OBJECT_REFCOUNT (level, "level", 1);

  gst_element_set_bus (level, NULL);
  ASSERT_OBJECT_REFCOUNT (bus, "bus", 1);
  gst_object_unref (bus);
  gst_buffer_unref (outbuffer);
  fail_unless (gst_element_set_state (level,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");
  ASSERT_OBJECT_REFCOUNT (level, "level", 1);
  cleanup_level (level);
}

GST_END_TEST;

GST_START_TEST (test_int16_panned)
{
  GstElement *level;
  GstBuffer *inbuffer, *outbuffer;
  GstBus *bus;
  GstCaps *caps;
  GstMessage *message;
  const GstStructure *structure;
  int j;
  gint16 *data;
  const GValue *list, *value;
  GstClockTime endtime;
  gdouble dB;
  const gchar *fields[3] = { "rms", "peak", "decay" };

  level = setup_level ();
  g_object_set (level, "message", TRUE, "interval", GST_SECOND / 10, NULL);

  fail_unless (gst_element_set_state (level,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  /* create a fake 0.1 sec buffer with a half-amplitude block signal */
  inbuffer = gst_buffer_new_and_alloc (400);
  data = (gint16 *) GST_BUFFER_DATA (inbuffer);
  for (j = 0; j < 100; ++j) {
    *data = 0;
    ++data;
    *data = 16536;
    ++data;
  }
  caps = gst_caps_from_string (LEVEL_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* create a bus to get the level message on */
  bus = gst_bus_new ();
  ASSERT_OBJECT_REFCOUNT (bus, "bus", 1);
  gst_element_set_bus (level, bus);
  ASSERT_OBJECT_REFCOUNT (bus, "bus", 2);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);

  message = gst_bus_poll (bus, GST_MESSAGE_ELEMENT, -1);
  ASSERT_OBJECT_REFCOUNT (message, "message", 1);

  fail_unless (message != NULL);
  fail_unless (GST_MESSAGE_SRC (message) == GST_OBJECT (level));
  fail_unless (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ELEMENT);
  structure = gst_message_get_structure (message);
  fail_if (structure == NULL);
  fail_unless_equals_string ((char *) gst_structure_get_name (structure),
      "level");
  fail_unless (gst_structure_get_clock_time (structure, "endtime", &endtime));

  /* silence has 0 dB for rms, peak and decay */
  for (j = 0; j < 3; ++j) {
    list = gst_structure_get_value (structure, fields[j]);
    value = gst_value_list_get_value (list, 0);
    dB = g_value_get_double (value);
    GST_DEBUG ("%s[0] is %lf", fields[j], dB);
#ifdef HAVE_ISINF
    fail_unless (isinf (dB));
#elif defined (HAVE_FPCLASS)
    fail_unless (fpclass (dB) == FP_NINF);
#endif
  }
  /* block wave of half amplitude has -5.94 dB for rms, peak and decay */
  for (j = 0; j < 3; ++j) {
    list = gst_structure_get_value (structure, fields[j]);
    value = gst_value_list_get_value (list, 1);
    dB = g_value_get_double (value);
    GST_DEBUG ("%s[1] is %lf", fields[j], dB);
    fail_if (dB < -6.0);
    fail_if (dB > -5.9);
  }
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);

  /* clean up */
  /* flush current messages,and future state change messages */
  gst_bus_set_flushing (bus, TRUE);

  /* message has a ref to the element */
  ASSERT_OBJECT_REFCOUNT (level, "level", 2);
  gst_message_unref (message);
  ASSERT_OBJECT_REFCOUNT (level, "level", 1);

  gst_element_set_bus (level, NULL);
  ASSERT_OBJECT_REFCOUNT (bus, "bus", 1);
  gst_object_unref (bus);
  gst_buffer_unref (outbuffer);
  fail_unless (gst_element_set_state (level,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");
  ASSERT_OBJECT_REFCOUNT (level, "level", 1);
  cleanup_level (level);
}

GST_END_TEST;

static Suite *
level_suite (void)
{
  Suite *s = suite_create ("level");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_int16);
  tcase_add_test (tc_chain, test_int16_panned);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = level_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
