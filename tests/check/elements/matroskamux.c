/* GStreamer
 *
 * unit test for matroskamux
 *
 * Copyright (C) <2005> Michal Benes <michal.benes@xeris.cz>
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

#define AC3_CAPS_STRING "audio/x-ac3, " \
                        "channels = (int) 1, " \
                        "rate = (int) 8000"
#define VORBIS_CAPS_STRING "audio/x-vorbis, " \
                           "channels = (int) 1, " \
                           "rate = (int) 8000, " \
                           "streamheader=(buffer)<10, 2020, 303030>"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-matroska"));
static GstStaticPadTemplate srcvorbistemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VORBIS_CAPS_STRING));

static GstStaticPadTemplate srcac3template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (AC3_CAPS_STRING));


static GstPad *
setup_src_pad (GstElement * element,
    GstStaticPadTemplate * template, GstCaps * caps)
{
  GstPad *srcpad, *sinkpad;

  GST_DEBUG_OBJECT (element, "setting up sending pad");
  /* sending pad */
  srcpad = gst_pad_new_from_static_template (template, "src");
  fail_if (srcpad == NULL, "Could not create a srcpad");
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 1);
  gst_pad_set_active (srcpad, TRUE);

  if (!(sinkpad = gst_element_get_static_pad (element, "audio_%d")))
    sinkpad = gst_element_get_request_pad (element, "audio_%d");
  fail_if (sinkpad == NULL, "Could not get sink pad from %s",
      GST_ELEMENT_NAME (element));
  /* references are owned by: 1) us, 2) matroskamux, 3) collect pads */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 3);
  if (caps)
    fail_unless (gst_pad_set_caps (srcpad, caps));
  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK,
      "Could not link source and %s sink pads", GST_ELEMENT_NAME (element));
  gst_object_unref (sinkpad);   /* because we got it higher up */

  /* references are owned by: 1) matroskamux, 2) collect pads */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);

  return srcpad;
}

static void
teardown_src_pad (GstElement * element)
{
  GstPad *srcpad, *sinkpad;

  /* clean up floating src pad */
  if (!(sinkpad = gst_element_get_static_pad (element, "audio_0")))
    sinkpad = gst_element_get_request_pad (element, "audio_0");
  /* references are owned by: 1) us, 2) matroskamux, 3) collect pads */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 3);
  srcpad = gst_pad_get_peer (sinkpad);

  gst_pad_unlink (srcpad, sinkpad);

  /* references are owned by: 1) us, 2) matroskamux, 3) collect pads */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 3);
  gst_object_unref (sinkpad);
  /* one more ref is held by element itself */

  /* pad refs held by both creator and this function (through _get_peer) */
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 2);
  gst_object_unref (srcpad);
  gst_object_unref (srcpad);
}

static GstPad *
setup_sink_pad (GstElement * element, GstStaticPadTemplate * template,
    GstCaps * caps)
{
  GstPad *srcpad, *sinkpad;

  GST_DEBUG_OBJECT (element, "setting up receiving pad");
  /* receiving pad */
  sinkpad = gst_pad_new_from_static_template (template, "sink");

  fail_if (sinkpad == NULL, "Could not create a sinkpad");
  gst_pad_set_active (sinkpad, TRUE);

  srcpad = gst_element_get_static_pad (element, "src");
  fail_if (srcpad == NULL, "Could not get source pad from %s",
      GST_ELEMENT_NAME (element));
  if (caps)
    fail_unless (gst_pad_set_caps (sinkpad, caps));
  gst_pad_set_chain_function (sinkpad, gst_check_chain_func);

  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK,
      "Could not link %s source and sink pads", GST_ELEMENT_NAME (element));
  gst_object_unref (srcpad);    /* because we got it higher up */
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 2);

  return sinkpad;
}

static void
teardown_sink_pad (GstElement * element)
{
  GstPad *srcpad, *sinkpad;

  /* clean up floating sink pad */
  srcpad = gst_element_get_static_pad (element, "src");
  sinkpad = gst_pad_get_peer (srcpad);
  gst_pad_unlink (srcpad, sinkpad);

  /* pad refs held by both creator and this function (through _get_pad) */
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 3);
  gst_object_unref (srcpad);
  /* one more ref is held by element itself */

  /* pad refs held by both creator and this function (through _get_peer) */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);
  gst_object_unref (sinkpad);
  gst_object_unref (sinkpad);
}


static GstElement *
setup_matroskamux (GstStaticPadTemplate * srctemplate)
{
  GstElement *matroskamux;

  GST_DEBUG ("setup_matroskamux");
  matroskamux = gst_check_setup_element ("matroskamux");
  mysrcpad = setup_src_pad (matroskamux, srctemplate, NULL);
  mysinkpad = setup_sink_pad (matroskamux, &sinktemplate, NULL);

  return matroskamux;
}

static void
cleanup_matroskamux (GstElement * matroskamux)
{
  GST_DEBUG ("cleanup_matroskamux");
  gst_element_set_state (matroskamux, GST_STATE_NULL);

  teardown_src_pad (matroskamux);
  teardown_sink_pad (matroskamux);
  gst_check_teardown_element (matroskamux);
}

static void
check_buffer_data (GstBuffer * buffer, void *data, size_t data_size)
{
  fail_unless (GST_BUFFER_SIZE (buffer) == data_size);
  fail_unless (memcmp (data, GST_BUFFER_DATA (buffer), data_size) == 0);
}

GST_START_TEST (test_ebml_header)
{
  GstElement *matroskamux;
  GstBuffer *inbuffer, *outbuffer;
  int num_buffers;
  int i;
  guint8 data0[12] =
      { 0x1a, 0x45, 0xdf, 0xa3, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff
  };
  guint8 data1[12] =
      { 0x42, 0x82, 0x89, 0x6d, 0x61, 0x74, 0x72, 0x6f, 0x73, 0x6b, 0x61,
    0x00
  };
  guint8 data2[4] = { 0x42, 0x87, 0x81, 0x01 };
  guint8 data3[4] = { 0x42, 0x85, 0x81, 0x01 };
  guint8 data4[8] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14 };

  matroskamux = setup_matroskamux (&srcac3template);
  fail_unless (gst_element_set_state (matroskamux,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (1);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  num_buffers = g_list_length (buffers);
  fail_unless (num_buffers >= 5,
      "expected at least 5 buffers, but got only %d", num_buffers);

  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);
    buffers = g_list_remove (buffers, outbuffer);

    switch (i) {
      case 0:
        check_buffer_data (outbuffer, data0, sizeof (data0));
        break;
      case 1:
        check_buffer_data (outbuffer, data1, sizeof (data1));
        break;
      case 2:
        check_buffer_data (outbuffer, data2, sizeof (data2));
        break;
      case 3:
        check_buffer_data (outbuffer, data3, sizeof (data3));
        break;
      case 4:
        check_buffer_data (outbuffer, data4, sizeof (data4));
        break;
      default:
        break;
    }

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  cleanup_matroskamux (matroskamux);
  g_list_free (buffers);
  buffers = NULL;
}

GST_END_TEST;


GST_START_TEST (test_vorbis_header)
{
  GstElement *matroskamux;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  int num_buffers;
  int i;
  gboolean vorbis_header_found = FALSE;
  guint8 data[12] =
      { 0x63, 0xa2, 0x89, 0x02, 0x01, 0x02, 0x10, 0x20, 0x20, 0x30, 0x30,
    0x30
  };

  matroskamux = setup_matroskamux (&srcvorbistemplate);
  fail_unless (gst_element_set_state (matroskamux,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (1);
  caps = gst_caps_from_string (VORBIS_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  num_buffers = g_list_length (buffers);

  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);
    buffers = g_list_remove (buffers, outbuffer);

    if (!vorbis_header_found && GST_BUFFER_SIZE (outbuffer) == sizeof (data)) {
      vorbis_header_found =
          memcmp (GST_BUFFER_DATA (outbuffer), data, sizeof (data));
    }

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  fail_unless (vorbis_header_found);

  cleanup_matroskamux (matroskamux);
  g_list_free (buffers);
  buffers = NULL;
}

GST_END_TEST;


GST_START_TEST (test_block_group)
{
  GstElement *matroskamux;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  int num_buffers;
  int i;
  guint8 data0[9] = { 0xa0, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
  guint8 data1[2] = { 0xa1, 0x85 };
  guint8 data2[4] = { 0x81, 0x00, 0x01, 0x00 };
  guint8 data3[1] = { 0x42 };
  guint8 data4[8] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07 };

  matroskamux = setup_matroskamux (&srcac3template);
  fail_unless (gst_element_set_state (matroskamux,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  /* Generate the header */
  inbuffer = gst_buffer_new_and_alloc (1);
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  caps = gst_caps_from_string (AC3_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  fail_unless_equals_int (gst_pad_push (mysrcpad, inbuffer), GST_FLOW_OK);
  num_buffers = g_list_length (buffers);

  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);
    buffers = g_list_remove (buffers, outbuffer);

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  g_list_free (buffers);
  buffers = NULL;

  /* Now push a buffer */
  inbuffer = gst_buffer_new_and_alloc (1);
  GST_BUFFER_DATA (inbuffer)[0] = 0x42;
  GST_BUFFER_TIMESTAMP (inbuffer) = 1000000;
  caps = gst_caps_from_string (AC3_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  num_buffers = g_list_length (buffers);
  fail_unless (num_buffers >= 5);

  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);
    buffers = g_list_remove (buffers, outbuffer);

    switch (i) {
      case 0:
        check_buffer_data (outbuffer, data0, sizeof (data0));
        break;
      case 1:
        check_buffer_data (outbuffer, data1, sizeof (data1));
        break;
      case 2:
        check_buffer_data (outbuffer, data2, sizeof (data2));
        break;
      case 3:
        check_buffer_data (outbuffer, data3, sizeof (data3));
        break;
      case 4:
        check_buffer_data (outbuffer, data4, sizeof (data4));
        break;
      default:
        break;
    }

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  g_list_free (buffers);
  buffers = NULL;

  cleanup_matroskamux (matroskamux);
}

GST_END_TEST;

static Suite *
matroskamux_suite (void)
{
  Suite *s = suite_create ("matroskamux");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_ebml_header);
  tcase_add_test (tc_chain, test_vorbis_header);
  tcase_add_test (tc_chain, test_block_group);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = matroskamux_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
