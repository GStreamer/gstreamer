/* GStreamer
 *
 * unit test for vorbisdec
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

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysrcpad, *mysinkpad;

/* a valid first header packet */
guchar identification_header[30] = {
  1,                            /* packet_type */
  'v', 'o', 'r', 'b', 'i', 's',
  0, 0, 0, 0,                   /* vorbis_version */
  2,                            /* audio_channels */
  0x44, 0xac, 0, 0,             /* sample_rate */
  0xff, 0xff, 0xff, 0xff,       /* bitrate_maximum */
  0x00, 0xee, 0x02, 0x00,       /* bitrate_nominal */
  0xff, 0xff, 0xff, 0xff,       /* bitrate_minimum */
  0xb8,                         /* blocksize_0, blocksize_1 */
  0x01,                         /* framing_flag */
};

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GstFlowReturn
chain_func (GstPad * pad, GstBuffer * buffer)
{
  GST_DEBUG ("chain_func: received buffer %p", buffer);
  buffers = g_list_append (buffers, buffer);

  return GST_FLOW_OK;
}

GstElement *
setup_vorbisdec ()
{
  GstElement *vorbisdec;
  GstPad *srcpad, *sinkpad;

  GST_DEBUG ("setup_vorbisdec");

  vorbisdec = gst_element_factory_make ("vorbisdec", "vorbisdec");
  fail_if (vorbisdec == NULL, "Could not create a vorbisdec");

  /* sending pad */
  mysrcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&srctemplate),
      "src");
  fail_if (mysrcpad == NULL, "Could not create a mysrcpad");
  ASSERT_OBJECT_REFCOUNT (mysrcpad, "mysrcpad", 1);

  sinkpad = gst_element_get_pad (vorbisdec, "sink");
  fail_if (sinkpad == NULL, "Could not get source pad from vorbisdec");
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);
  gst_pad_set_caps (mysrcpad, NULL);
  fail_unless (gst_pad_link (mysrcpad, sinkpad) == GST_PAD_LINK_OK,
      "Could not link source and vorbisdec sink pads");
  gst_object_unref (sinkpad);   /* because we got it higher up */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 1);

  /* receiving pad */
  mysinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sinktemplate),
      "sink");
  fail_if (mysinkpad == NULL, "Could not create a mysinkpad");

  srcpad = gst_element_get_pad (vorbisdec, "src");
  fail_if (srcpad == NULL, "Could not get source pad from vorbisdec");
  gst_pad_set_caps (mysinkpad, NULL);
  gst_pad_set_chain_function (mysinkpad, chain_func);

  fail_unless (gst_pad_link (srcpad, mysinkpad) == GST_PAD_LINK_OK,
      "Could not link vorbisdec source and mysink pads");
  gst_object_unref (srcpad);    /* because we got it higher up */
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 1);

  return vorbisdec;
}

void
cleanup_vorbisdec (GstElement * vorbisdec)
{
  GstPad *srcpad, *sinkpad;

  GST_DEBUG ("cleanup_vorbisdec");

  fail_unless (gst_element_set_state (vorbisdec, GST_STATE_NULL) ==
      GST_STATE_SUCCESS, "could not set to null");
  ASSERT_OBJECT_REFCOUNT (vorbisdec, "vorbisdec", 1);

  /* clean up floating src pad */
  sinkpad = gst_element_get_pad (vorbisdec, "sink");
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);

  gst_pad_unlink (mysrcpad, sinkpad);

  /* pad refs held by both creator and this function (through _get) */
  ASSERT_OBJECT_REFCOUNT (mysrcpad, "srcpad", 1);
  gst_object_unref (mysrcpad);
  mysrcpad = NULL;

  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);
  gst_object_unref (sinkpad);
  /* one more ref is held by vorbisdec itself */

  /* clean up floating sink pad */
  srcpad = gst_element_get_pad (vorbisdec, "src");
  gst_pad_unlink (srcpad, mysinkpad);

  /* pad refs held by both creator and this function (through _get) */
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 2);
  gst_object_unref (srcpad);
  /* one more ref is held by vorbisdec itself */

  ASSERT_OBJECT_REFCOUNT (mysinkpad, "mysinkpad", 1);
  gst_object_unref (mysinkpad);
  mysinkpad = NULL;

  ASSERT_OBJECT_REFCOUNT (vorbisdec, "vorbisdec", 1);
  gst_object_unref (vorbisdec);
}

GST_START_TEST (test_empty_identification_header)
{
  GstElement *vorbisdec;
  GstBuffer *inbuffer, *outbuffer;
  GstBus *bus;
  GstMessage *message;
  GError *error;
  gchar *debug;

  vorbisdec = setup_vorbisdec ();
  bus = gst_bus_new ();

  fail_unless (gst_element_set_state (vorbisdec,
          GST_STATE_PLAYING) == GST_STATE_SUCCESS, "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (0);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* set a bus here so we avoid getting state change messages */
  gst_element_set_bus (vorbisdec, bus);

  fail_unless_equals_int (gst_pad_push (mysrcpad, inbuffer), GST_FLOW_ERROR);
  /* ... but it ends up being collected on the global buffer list */
  fail_unless_equals_int (g_list_length (buffers), 0);

  fail_if ((message = gst_bus_pop (bus)) == NULL);
  fail_unless_message_error (message, STREAM, DECODE);
  gst_message_unref (message);
  gst_element_set_bus (vorbisdec, NULL);

  /* cleanup */
  gst_object_unref (GST_OBJECT (bus));
  cleanup_vorbisdec (vorbisdec);
}

GST_END_TEST;


GST_START_TEST (test_unity)
{
  GstElement *vorbisdec;
  GstBuffer *inbuffer, *outbuffer;
  gint16 in[2] = { 16384, -256 };

  vorbisdec = setup_vorbisdec ();
  fail_unless (gst_element_set_state (vorbisdec,
          GST_STATE_PLAYING) == GST_STATE_SUCCESS, "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (30);
  memcpy (GST_BUFFER_DATA (inbuffer), identification_header, 30);
  //FIXME: add a test for wrong channels, like so:
  //GST_BUFFER_DATA (inbuffer)[12] = 7;
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_ref (inbuffer);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and nothing ends up on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_unref (inbuffer);
  fail_unless (g_list_length (buffers) == 0);

  /* cleanup */
  cleanup_vorbisdec (vorbisdec);
}

GST_END_TEST;

Suite *
vorbisdec_suite (void)
{
  Suite *s = suite_create ("vorbisdec");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_unity);
  tcase_add_test (tc_chain, test_empty_identification_header);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = vorbisdec_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
