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

guchar comment_header[] = {
  3,                            /* packet_type */
  'v', 'o', 'r', 'b', 'i', 's',
  2, 0, 0, 0,                   /* vendor_length */
  'm', 'e',
  1, 0, 0, 0,                   /* user_comment_list_length */
  9, 0, 0, 0,                   /* length comment[0] */
  'A', 'R', 'T', 'I', 'S', 'T', '=', 'm', 'e',
  0x01,                         /* framing bit */
};

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GstElement *
setup_vorbisdec ()
{
  GstElement *vorbisdec;

  GST_DEBUG ("setup_vorbisdec");
  vorbisdec = gst_check_setup_element ("vorbisdec");
  mysrcpad = gst_check_setup_src_pad (vorbisdec, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (vorbisdec, &sinktemplate, NULL);

  return vorbisdec;
}

void
cleanup_vorbisdec (GstElement * vorbisdec)
{
  GST_DEBUG ("cleanup_vorbisdec");

  gst_check_teardown_src_pad (vorbisdec);
  gst_check_teardown_sink_pad (vorbisdec);
  gst_check_teardown_element (vorbisdec);
}

GST_START_TEST (test_wrong_channels_identification_header)
{
  GstElement *vorbisdec;
  GstBuffer *inbuffer, *outbuffer;
  GstBus *bus;
  GstMessage *message;

  vorbisdec = setup_vorbisdec ();
  fail_unless (gst_element_set_state (vorbisdec,
          GST_STATE_PLAYING) == GST_STATE_SUCCESS, "could not set to playing");
  bus = gst_bus_new ();

  inbuffer = gst_buffer_new_and_alloc (30);
  memcpy (GST_BUFFER_DATA (inbuffer), identification_header, 30);
  /* set the channel count to 7, which is not supported */
  GST_BUFFER_DATA (inbuffer)[11] = 7;
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_ref (inbuffer);

  gst_element_set_bus (vorbisdec, bus);
  /* pushing gives away my reference ... */
  fail_unless_equals_int (gst_pad_push (mysrcpad, inbuffer), GST_FLOW_ERROR);
  /* ... and nothing ends up on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_unref (inbuffer);
  fail_unless_equals_int (g_list_length (buffers), 0);

  fail_if ((message = gst_bus_pop (bus)) == NULL);
  fail_unless_message_error (message, STREAM, NOT_IMPLEMENTED);
  gst_message_unref (message);
  gst_element_set_bus (vorbisdec, NULL);

  /* cleanup */
  gst_object_unref (GST_OBJECT (bus));
  cleanup_vorbisdec (vorbisdec);
}

GST_END_TEST;


GST_START_TEST (test_empty_identification_header)
{
  GstElement *vorbisdec;
  GstBuffer *inbuffer, *outbuffer;
  GstBus *bus;
  GstMessage *message;

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

/* FIXME: also tests comment header */
GST_START_TEST (test_identification_header)
{
  GstElement *vorbisdec;
  GstBuffer *inbuffer, *outbuffer;
  GstBus *bus;
  GstMessage *message;
  GstTagList *tag_list;
  gchar *artist;

  vorbisdec = setup_vorbisdec ();
  fail_unless (gst_element_set_state (vorbisdec,
          GST_STATE_PLAYING) == GST_STATE_SUCCESS, "could not set to playing");
  bus = gst_bus_new ();

  inbuffer = gst_buffer_new_and_alloc (30);
  memcpy (GST_BUFFER_DATA (inbuffer), identification_header, 30);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_ref (inbuffer);

  gst_element_set_bus (vorbisdec, bus);
  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and nothing ends up on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_unref (inbuffer);
  fail_unless (g_list_length (buffers) == 0);
  fail_if ((message = gst_bus_pop (bus)) != NULL);

  inbuffer = gst_buffer_new_and_alloc (sizeof (comment_header));
  memcpy (GST_BUFFER_DATA (inbuffer), comment_header, sizeof (comment_header));
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_ref (inbuffer);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and nothing ends up on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_unref (inbuffer);
  fail_unless (g_list_length (buffers) == 0);
  /* there's a tag message waiting */
  fail_if ((message = gst_bus_pop (bus)) == NULL);
  gst_message_parse_tag (message, &tag_list);
  fail_unless_equals_int (gst_tag_list_get_tag_size (tag_list, GST_TAG_ARTIST),
      1);
  fail_unless (gst_tag_list_get_string (tag_list, GST_TAG_ARTIST, &artist));
  fail_unless_equals_string (artist, "me");
  fail_unless_equals_int (gst_tag_list_get_tag_size (tag_list, "album"), 0);
  gst_tag_list_free (tag_list);
  gst_message_unref (message);

  /* cleanup */
  gst_element_set_bus (vorbisdec, NULL);
  gst_object_unref (GST_OBJECT (bus));
  cleanup_vorbisdec (vorbisdec);
}

GST_END_TEST;

Suite *
vorbisdec_suite (void)
{
  Suite *s = suite_create ("vorbisdec");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_empty_identification_header);
  tcase_add_test (tc_chain, test_wrong_channels_identification_header);
  tcase_add_test (tc_chain, test_identification_header);

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
