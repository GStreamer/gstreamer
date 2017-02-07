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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <unistd.h>

#include <gst/check/gstcheck.h>
#include <gst/base/gstadapter.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysrcpad, *mysinkpad;

#define AC3_CAPS_STRING "audio/x-ac3, " \
                        "channels = (int) 1, " \
                        "rate = (int) 8000"
#define VORBIS_TMPL_CAPS_STRING "audio/x-vorbis, " \
                                "channels = (int) 1, " \
                                "rate = (int) 8000"
/* streamheader shouldn't be in the template caps, only in the actual caps */
#define VORBIS_CAPS_STRING VORBIS_TMPL_CAPS_STRING \
                           ", streamheader=(buffer)<10, 2020, 303030>"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-matroska; audio/x-matroska"));
static GstStaticPadTemplate srcvorbistemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VORBIS_TMPL_CAPS_STRING));

static GstStaticPadTemplate srcac3template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (AC3_CAPS_STRING));


static GstPad *
setup_src_pad (GstElement * element, GstStaticPadTemplate * template)
{
  GstPad *srcpad, *sinkpad;

  GST_DEBUG_OBJECT (element, "setting up sending pad");
  /* sending pad */
  srcpad = gst_pad_new_from_static_template (template, "src");
  fail_if (srcpad == NULL, "Could not create a srcpad");
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 1);
  gst_pad_set_active (srcpad, TRUE);

  if (!(sinkpad = gst_element_get_static_pad (element, "audio_%u")))
    sinkpad = gst_element_get_request_pad (element, "audio_%u");
  fail_if (sinkpad == NULL, "Could not get sink pad from %s",
      GST_ELEMENT_NAME (element));
  /* references are owned by: 1) us, 2) matroskamux, 3) collect pads */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 3);
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
setup_sink_pad (GstElement * element, GstStaticPadTemplate * template)
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
  g_object_set (matroskamux, "version", 1, NULL);
  mysrcpad = setup_src_pad (matroskamux, srctemplate);
  mysinkpad = setup_sink_pad (matroskamux, &sinktemplate);

  fail_unless (gst_element_set_state (matroskamux,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

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
  fail_unless (gst_buffer_get_size (buffer) == data_size);
  fail_unless (gst_buffer_memcmp (buffer, 0, data, data_size) == 0);
}

GST_START_TEST (test_ebml_header)
{
  GstElement *matroskamux;
  GstBuffer *inbuffer, *outbuffer;
  GstAdapter *adapter;
  int num_buffers;
  int i;
  gint available;
  GstCaps *caps;
  guint8 data[] =
      { 0x1a, 0x45, 0xdf, 0xa3, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14,
    0x42, 0x82, 0x89, 0x6d, 0x61, 0x74, 0x72, 0x6f, 0x73, 0x6b, 0x61, 0x00,
    0x42, 0x87, 0x81, 0x01,
    0x42, 0x85, 0x81, 0x01
  };

  matroskamux = setup_matroskamux (&srcac3template);

  caps = gst_caps_from_string (srcac3template.static_caps.string);
  gst_check_setup_events (mysrcpad, matroskamux, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  inbuffer = gst_buffer_new_allocate (NULL, 1, 0);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  num_buffers = g_list_length (buffers);
  fail_unless (num_buffers >= 1,
      "expected at least 5 buffers, but got only %d", num_buffers);

  adapter = gst_adapter_new ();
  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);
    buffers = g_list_remove (buffers, outbuffer);

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);

    gst_adapter_push (adapter, outbuffer);
  }

  available = gst_adapter_available (adapter);
  fail_unless (available >= sizeof (data));
  outbuffer = gst_adapter_take_buffer (adapter, sizeof (data));
  g_object_unref (adapter);

  check_buffer_data (outbuffer, data, sizeof (data));
  gst_buffer_unref (outbuffer);

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

  caps = gst_caps_from_string (VORBIS_CAPS_STRING);
  gst_check_setup_events (mysrcpad, matroskamux, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  inbuffer = gst_buffer_new_allocate (NULL, 1, 0);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  num_buffers = g_list_length (buffers);

  for (i = 0; i < num_buffers; ++i) {
    gint j;
    gsize buffer_size;

    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);
    buffer_size = gst_buffer_get_size (outbuffer);
    buffers = g_list_remove (buffers, outbuffer);

    if (!vorbis_header_found && buffer_size >= sizeof (data)) {
      for (j = 0; j <= buffer_size - sizeof (data); j++) {
        if (gst_buffer_memcmp (outbuffer, j, data, sizeof (data)) == 0) {
          vorbis_header_found = TRUE;
          break;
        }
      }
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
  guint8 *indata;
  GstCaps *caps;
  int num_buffers;
  int i;
  guint8 data0[] = { 0x1f, 0x43, 0xb6, 0x75, 0x01, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xe7, 0x81, 0x01
  };
  guint8 data1[] = { 0xab, 0x81, 0x1f };
  guint8 data2[] = { 0xa0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07, 0xa1, 0x85, 0x81, 0x00, 0x00, 0x00
  };
  guint8 data3[] = { 0x42 };

  matroskamux = setup_matroskamux (&srcac3template);

  caps = gst_caps_from_string (AC3_CAPS_STRING);
  gst_check_setup_events (mysrcpad, matroskamux, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  /* Generate the header */
  inbuffer = gst_buffer_new_allocate (NULL, 1, 0);
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
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
  indata = g_malloc (1);
  inbuffer = gst_buffer_new_wrapped (indata, 1);
  indata[0] = 0x42;
  GST_BUFFER_TIMESTAMP (inbuffer) = 1000000;
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  num_buffers = g_list_length (buffers);
  fail_unless (num_buffers >= 4);

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

GST_START_TEST (test_reset)
{
  GstElement *matroskamux;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  int num_buffers;
  int i;
  GstCaps *caps;

  matroskamux = setup_matroskamux (&srcac3template);

  caps = gst_caps_from_string (srcac3template.static_caps.string);
  gst_check_setup_events (mysrcpad, matroskamux, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  inbuffer = gst_buffer_new_allocate (NULL, 1, 0);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  num_buffers = g_list_length (buffers);
  fail_unless (num_buffers >= 1,
      "expected at least 1 buffer, but got only %d", num_buffers);

  fail_unless (gst_element_set_state (matroskamux,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  fail_unless (gst_element_set_state (matroskamux,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_allocate (NULL, 1, 0);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  num_buffers = g_list_length (buffers);
  fail_unless (num_buffers >= 2,
      "expected at least 2 buffers, but got only %d", num_buffers);

  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);
    buffers = g_list_remove (buffers, outbuffer);

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
  }

  cleanup_matroskamux (matroskamux);
  g_list_free (buffers);
  buffers = NULL;
}

GST_END_TEST;

GST_START_TEST (test_link_webmmux_webm_sink)
{
  static GstStaticPadTemplate webm_sinktemplate =
      GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/webm; audio/webm"));
  GstElement *mux;

  mux = gst_check_setup_element ("webmmux");
  mysinkpad = setup_sink_pad (mux, &webm_sinktemplate);
  fail_unless (mysinkpad != NULL);

  fail_unless (gst_element_set_state (mux,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  gst_element_set_state (mux, GST_STATE_NULL);

  teardown_sink_pad (mux);
  gst_check_teardown_element (mux);
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
  tcase_add_test (tc_chain, test_reset);
  tcase_add_test (tc_chain, test_link_webmmux_webm_sink);

  return s;
}

GST_CHECK_MAIN (matroskamux);
