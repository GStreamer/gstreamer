/* GStreamer
 *
 * unit test for volume
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


#define VOLUME_CAPS_TEMPLATE_STRING	\
    "audio/x-raw-int, "			\
    "channels = (int) [ 1, MAX ], "	\
    "rate = (int) [ 1,  MAX ], "	\
    "endianness = (int) BYTE_ORDER, "	\
    "width = (int) 16, "		\
    "depth = (int) 16, "		\
    "signed = (bool) TRUE"

#define VOLUME_CAPS_STRING		\
    "audio/x-raw-int, "			\
    "channels = (int) 1, "		\
    "rate = (int) 44100, "		\
    "endianness = (int) BYTE_ORDER, "	\
    "width = (int) 16, "		\
    "depth = (int) 16, "		\
    "signed = (bool) TRUE"

#define VOLUME_WRONG_CAPS_STRING	\
    "audio/x-raw-int, "			\
    "channels = (int) 1, "		\
    "rate = (int) 44100, "		\
    "endianness = (int) BYTE_ORDER, "	\
    "width = (int) 16, "		\
    "depth = (int) 16, "		\
    "signed = (bool) FALSE"


static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VOLUME_CAPS_TEMPLATE_STRING)
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VOLUME_CAPS_TEMPLATE_STRING)
    );

GstElement *
setup_volume ()
{
  GstElement *volume;

  GST_DEBUG ("setup_volume");
  volume = gst_check_setup_element ("volume");
  mysrcpad = gst_check_setup_src_pad (volume, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (volume, &sinktemplate, NULL);
  return volume;
}

void
cleanup_volume (GstElement * volume)
{
  GST_DEBUG ("cleanup_volume");

  gst_check_teardown_src_pad (volume);
  gst_check_teardown_sink_pad (volume);
  gst_check_teardown_element (volume);
}

GST_START_TEST (test_unity)
{
  GstElement *volume;
  GstBuffer *inbuffer, *outbuffer;
  gint16 in[2] = { 16384, -256 };

  volume = setup_volume ();
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_SUCCESS, "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  gst_buffer_set_caps (inbuffer, gst_caps_from_string (VOLUME_CAPS_STRING));
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (g_list_length (buffers) == 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 4) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_half)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  gint16 in[2] = { 16384, -256 };
  gint16 out[2] = { 8192, -128 };

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 0.5, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_SUCCESS, "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 4) == 0);
  gst_buffer_set_caps (inbuffer, gst_caps_from_string (VOLUME_CAPS_STRING));
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (g_list_length (buffers) == 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 4) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_double)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  gint16 in[2] = { 16384, -256 };
  gint16 out[2] = { 32767, -512 };      /* notice the clamped sample */

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 2.0, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_SUCCESS, "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 4) == 0);
  gst_buffer_set_caps (inbuffer, gst_caps_from_string (VOLUME_CAPS_STRING));
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (g_list_length (buffers) == 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 4) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_wrong_caps)
{
  GstElement *volume;
  GstBuffer *inbuffer, *outbuffer;
  gint16 in[2] = { 16384, -256 };
  GstBus *bus;
  GstMessage *message;

  volume = setup_volume ();
  bus = gst_bus_new ();

  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_SUCCESS, "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  gst_buffer_set_caps (inbuffer,
      gst_caps_from_string (VOLUME_WRONG_CAPS_STRING));
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_ref (inbuffer);

  /* set a bus here so we avoid getting state change messages */
  gst_element_set_bus (volume, bus);

  /* pushing gives an error because it can't negotiate with wrong caps */
  fail_unless_equals_int (gst_pad_push (mysrcpad, inbuffer),
      GST_FLOW_NOT_NEGOTIATED);
  /* ... and the buffer would have been lost if we didn't ref it ourselves */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_unref (inbuffer);
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* volume_set_caps should not have been called since basetransform caught
   * the negotiation problem */
  fail_if ((message = gst_bus_pop (bus)) != NULL);

  /* cleanup */
  gst_element_set_bus (volume, NULL);
  gst_object_unref (GST_OBJECT (bus));
  cleanup_volume (volume);
}

GST_END_TEST;


Suite *
volume_suite (void)
{
  Suite *s = suite_create ("volume");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_unity);
  tcase_add_test (tc_chain, test_half);
  tcase_add_test (tc_chain, test_double);
  tcase_add_test (tc_chain, test_wrong_caps);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = volume_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
