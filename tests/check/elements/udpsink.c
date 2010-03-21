/* GStreamer RTP payloader unit tests
 * Copyright (C) 2009 Axis Communications <dev-gstreamer@axis.com>
 * @author Ognyan Tonchev <ognyan@axis.com>
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
#include <gst/check/gstcheck.h>
#include <gst/base/gstbasesink.h>
#include <stdlib.h>
#include <unistd.h>

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define RTP_HEADER_SIZE 12
#define RTP_PAYLOAD_SIZE 1024

/*
 * Number of bytes received in the render function when using buffer lists
 */
static guint render_list_bytes_received;

/*
 * Render function for testing udpsink with buffer lists
 */
static GstFlowReturn
udpsink_render (GstBaseSink * sink, GstBufferList * list)
{
  GstBufferListIterator *it;

  fail_if (!list);

  /*
   * Count the size of the rtp header and the payload in the buffer list.
   */

  it = gst_buffer_list_iterate (list);

  /* Loop through all groups */
  while (gst_buffer_list_iterator_next_group (it)) {
    GstBuffer *buf;
    /* Loop through all buffers in the current group */
    while ((buf = gst_buffer_list_iterator_next (it))) {
      guint size;

      size = GST_BUFFER_SIZE (buf);
      GST_DEBUG ("rendered %u bytes", size);

      render_list_bytes_received += size;
    }
  }

  gst_buffer_list_iterator_free (it);

  return GST_FLOW_OK;
}

static void
_set_render_function (GstElement * bsink)
{
  GstBaseSinkClass *bsclass;
  bsclass = GST_BASE_SINK_GET_CLASS ((GstBaseSink *) bsink);
  /* Add callback function for the buffer list tests */
  bsclass->render_list = udpsink_render;
}

static GstBufferList *
_create_buffer_list (guint * data_size)
{
  GstBufferList *list;
  GstBufferListIterator *it;
  GstBuffer *rtp_buffer;
  GstBuffer *data_buffer;

  list = gst_buffer_list_new ();
  it = gst_buffer_list_iterate (list);

  /*** First group, i.e. first packet. **/

  /* Create the RTP header buffer */
  rtp_buffer = gst_buffer_new ();
  GST_BUFFER_MALLOCDATA (rtp_buffer) = g_malloc (RTP_HEADER_SIZE);
  GST_BUFFER_DATA (rtp_buffer) = GST_BUFFER_MALLOCDATA (rtp_buffer);
  GST_BUFFER_SIZE (rtp_buffer) = RTP_HEADER_SIZE;
  memset (GST_BUFFER_DATA (rtp_buffer), 0, RTP_HEADER_SIZE);

  /* Create the buffer that holds the payload */
  data_buffer = gst_buffer_new ();
  GST_BUFFER_MALLOCDATA (data_buffer) = g_malloc (RTP_PAYLOAD_SIZE);
  GST_BUFFER_DATA (data_buffer) = GST_BUFFER_MALLOCDATA (data_buffer);
  GST_BUFFER_SIZE (data_buffer) = RTP_PAYLOAD_SIZE;
  memset (GST_BUFFER_DATA (data_buffer), 0, RTP_PAYLOAD_SIZE);

  /* Create a new group to hold the rtp header and the payload */
  gst_buffer_list_iterator_add_group (it);
  gst_buffer_list_iterator_add (it, rtp_buffer);
  gst_buffer_list_iterator_add (it, data_buffer);

  /***  Second group, i.e. second packet. ***/

  /* Create the RTP header buffer */
  rtp_buffer = gst_buffer_new ();
  GST_BUFFER_MALLOCDATA (rtp_buffer) = g_malloc (RTP_HEADER_SIZE);
  GST_BUFFER_DATA (rtp_buffer) = GST_BUFFER_MALLOCDATA (rtp_buffer);
  GST_BUFFER_SIZE (rtp_buffer) = RTP_HEADER_SIZE;
  memset (GST_BUFFER_DATA (rtp_buffer), 0, RTP_HEADER_SIZE);

  /* Create the buffer that holds the payload */
  data_buffer = gst_buffer_new ();
  GST_BUFFER_MALLOCDATA (data_buffer) = g_malloc (RTP_PAYLOAD_SIZE);
  GST_BUFFER_DATA (data_buffer) = GST_BUFFER_MALLOCDATA (data_buffer);
  GST_BUFFER_SIZE (data_buffer) = RTP_PAYLOAD_SIZE;
  memset (GST_BUFFER_DATA (data_buffer), 0, RTP_PAYLOAD_SIZE);

  /* Create a new group to hold the rtp header and the payload */
  gst_buffer_list_iterator_add_group (it);
  gst_buffer_list_iterator_add (it, rtp_buffer);
  gst_buffer_list_iterator_add (it, data_buffer);

  /* Calculate the size of the data */
  *data_size = 2 * RTP_HEADER_SIZE + 2 * RTP_PAYLOAD_SIZE;

  gst_buffer_list_iterator_free (it);

  return list;
}

static void
udpsink_test (gboolean use_buffer_lists)
{
  GstElement *udpsink;
  GstPad *srcpad;
  GstBufferList *list;
  guint data_size;

  list = _create_buffer_list (&data_size);

  udpsink = gst_check_setup_element ("udpsink");
  if (use_buffer_lists)
    _set_render_function (udpsink);

  srcpad = gst_check_setup_src_pad_by_name (udpsink, &srctemplate, "sink");

  gst_element_set_state (udpsink, GST_STATE_PLAYING);

  gst_pad_push_event (srcpad, gst_event_new_new_segment_full (FALSE, 1.0, 1.0,
          GST_FORMAT_TIME, 0, -1, 0));

  gst_pad_push_list (srcpad, list);

  gst_check_teardown_pad_by_name (udpsink, "sink");
  gst_check_teardown_element (udpsink);

  if (use_buffer_lists)
    fail_if (data_size != render_list_bytes_received);
}

GST_START_TEST (test_udpsink)
{
  udpsink_test (FALSE);
}

GST_END_TEST;
GST_START_TEST (test_udpsink_bufferlist)
{
  udpsink_test (TRUE);
}

GST_END_TEST;

/*
 * Creates the test suite.
 *
 * Returns: pointer to the test suite.
 */
static Suite *
udpsink_suite (void)
{
  Suite *s = suite_create ("udpsink_test");

  TCase *tc_chain = tcase_create ("linear");

  /* Set timeout to 60 seconds. */
  tcase_set_timeout (tc_chain, 60);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_udpsink);
  tcase_add_test (tc_chain, test_udpsink_bufferlist);
  return s;
}

GST_CHECK_MAIN (udpsink)
