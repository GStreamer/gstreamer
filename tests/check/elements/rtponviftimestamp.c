/*
 * onviftimestamp.c
 *
 * Copyright (C) 2014 Axis Communications AB
 *  Author: Guillaume Desmottes <guillaume.desmottes@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <gst/check/gstcheck.h>
#include <gst/rtp/gstrtpbuffer.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstElement *element;
static GstPad *mysrcpad;
static GstPad *mysinkpad;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

#define NTP_OFFSET ((guint64) 1245)
#define TIMESTAMP ((GstClockTime) 42)

static void
setup (void)
{
  element = gst_check_setup_element ("rtponviftimestamp");

  mysinkpad = gst_check_setup_sink_pad (element, &sinktemplate);
  gst_pad_set_active (mysinkpad, TRUE);

  mysrcpad = gst_check_setup_src_pad (element, &srctemplate);
  gst_pad_set_active (mysrcpad, TRUE);
}


static void
cleanup (void)
{
  gst_check_drop_buffers ();

  gst_pad_set_active (mysrcpad, FALSE);
  gst_check_teardown_src_pad (element);
  mysrcpad = NULL;

  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_sink_pad (element);
  mysinkpad = NULL;

  gst_check_teardown_element (element);
  element = NULL;
}

static void
check_buffer_equal (GstBuffer * buf, GstBuffer * expected)
{
  GstMapInfo info_buf, info_expected;

  fail_if (buf == NULL);
  fail_if (expected == NULL);

  fail_unless (gst_buffer_map (buf, &info_buf, GST_MAP_READ));
  fail_unless (gst_buffer_map (expected, &info_expected, GST_MAP_READ));

  GST_LOG ("buffer: size %" G_GSIZE_FORMAT, info_buf.size);
  GST_LOG ("expected: size %" G_GSIZE_FORMAT, info_expected.size);
  GST_MEMDUMP ("buffer", info_buf.data, info_buf.size);
  GST_MEMDUMP ("expected", info_expected.data, info_expected.size);

  fail_unless_equals_uint64 (info_buf.size, info_expected.size);
  fail_unless_equals_int (memcmp (info_buf.data, info_expected.data,
          info_buf.size), 0);

  gst_buffer_unmap (buf, &info_buf);
  gst_buffer_unmap (expected, &info_expected);
}

/* Create a RTP buffer without the extension */
static GstBuffer *
create_rtp_buffer (GstClockTime timestamp, gboolean clean_point,
    gboolean discont)
{
  GstBuffer *buffer_in;
  GstRTPBuffer rtpbuffer_in = GST_RTP_BUFFER_INIT;

  buffer_in = gst_rtp_buffer_new_allocate (0, 0, 0);
  GST_BUFFER_PTS (buffer_in) = timestamp;

  if (!clean_point)
    GST_BUFFER_FLAG_SET (buffer_in, GST_BUFFER_FLAG_DELTA_UNIT);
  if (discont)
    GST_BUFFER_FLAG_SET (buffer_in, GST_BUFFER_FLAG_DISCONT);

  fail_unless (gst_rtp_buffer_map (buffer_in, GST_MAP_READ, &rtpbuffer_in));
  fail_if (gst_rtp_buffer_get_extension (&rtpbuffer_in));
  gst_rtp_buffer_unmap (&rtpbuffer_in);

  return buffer_in;
}

static guint64
convert_to_ntp (GstClockTime t)
{
  guint64 ntptime;

  /* convert to NTP time. upper 32 bits should contain the seconds
   * and the lower 32 bits, the fractions of a second. */
  ntptime = gst_util_uint64_scale (t, (G_GINT64_CONSTANT (1) << 32),
      GST_SECOND);

  return ntptime;
}

/* Create a copy of @buffer_in having the RTP extension */
static GstBuffer *
create_extension_buffer (GstBuffer * buffer_in, gboolean clean_point,
    gboolean end_contiguous, gboolean discont)
{
  GstBuffer *buffer_out;
  GstRTPBuffer rtpbuffer_out = GST_RTP_BUFFER_INIT;
  guint8 *data;
  guint8 flags = 0;

  buffer_out = gst_buffer_copy (buffer_in);

  fail_unless (gst_rtp_buffer_map (buffer_out, GST_MAP_READWRITE,
          &rtpbuffer_out));

  /* extension */
  fail_unless (gst_rtp_buffer_set_extension_data (&rtpbuffer_out, 0xABAC, 3));
  fail_unless (gst_rtp_buffer_get_extension (&rtpbuffer_out));
  fail_unless (gst_rtp_buffer_get_extension_data (&rtpbuffer_out, NULL,
          (gpointer) & data, NULL));

  /* NTP timestamp */
  GST_WRITE_UINT64_BE (data, convert_to_ntp (buffer_in->pts + NTP_OFFSET));

  /* C E D mbz */
  if (clean_point)
    flags |= (1 << 7);
  if (end_contiguous)
    flags |= (1 << 6);
  if (discont)
    flags |= (1 << 5);

  GST_WRITE_UINT8 (data + 8, flags);

  /* CSeq */
  GST_WRITE_UINT8 (data + 9, 0x78);

  memset (data + 10, 0, 4);

  gst_rtp_buffer_unmap (&rtpbuffer_out);

  return buffer_out;
}

static void
do_one_buffer_test_apply (gboolean clean_point, gboolean discont)
{
  GstBuffer *buffer_in, *buffer_out;

  g_object_set (element, "ntp-offset", NTP_OFFSET, "cseq", 0x12345678,
      "set-e-bit", FALSE, NULL);

  ASSERT_SET_STATE (element, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);

  buffer_in = create_rtp_buffer (TIMESTAMP, clean_point, discont);
  buffer_out = create_extension_buffer (buffer_in, clean_point, FALSE, discont);

  /* push initial events */
  gst_check_setup_events (mysrcpad, element, NULL, GST_FORMAT_TIME);

  /* Push buffer */
  fail_unless_equals_int (gst_pad_push (mysrcpad, buffer_in), GST_FLOW_OK);

  check_buffer_equal ((GstBuffer *) buffers->data, buffer_out);
  gst_buffer_unref (buffer_out);

  ASSERT_SET_STATE (element, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
}

static void
do_two_buffers_test_apply (gboolean end_contiguous)
{
  GstBuffer *buffer_in, *buffer_out;
  GList *node;

  g_object_set (element, "ntp-offset", NTP_OFFSET, "cseq", 0x12345678,
      "set-e-bit", TRUE, NULL);

  ASSERT_SET_STATE (element, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);

  buffer_in = create_rtp_buffer (TIMESTAMP, FALSE, FALSE);
  buffer_out = create_extension_buffer (buffer_in, FALSE, end_contiguous,
      FALSE);

  /* push initial events */
  gst_check_setup_events (mysrcpad, element, NULL, GST_FORMAT_TIME);

  /* Push buffer */
  fail_unless (gst_pad_push (mysrcpad, buffer_in) == GST_FLOW_OK,
      "failed pushing buffer");

  /* The buffer hasn't been pushed it as the element is waiting for the next
   * buffer. */
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* A second buffer is pushed, it has the DISCONT flag if we want that the
   * first one has the 'E' bit set. */
  buffer_in = create_rtp_buffer (TIMESTAMP + 1, FALSE, end_contiguous);

  fail_unless_equals_int (gst_pad_push (mysrcpad, buffer_in), GST_FLOW_OK);

  /* The first buffer has now been pushed out */
  fail_unless_equals_int (g_list_length (buffers), 1);

  node = g_list_last (buffers);
  check_buffer_equal ((GstBuffer *) node->data, buffer_out);
  gst_buffer_unref (buffer_out);

  /* Push EOS */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  /* The second buffer has been pushed out */
  fail_unless_equals_int (g_list_length (buffers), 2);

  /* Latest buffer always has the 'E' flag */
  buffer_out = create_extension_buffer (buffer_in, FALSE, TRUE, end_contiguous);
  node = g_list_last (buffers);
  check_buffer_equal ((GstBuffer *) node->data, buffer_out);
  gst_buffer_unref (buffer_out);

  ASSERT_SET_STATE (element, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
}

GST_START_TEST (test_apply_discont)
{
  do_one_buffer_test_apply (FALSE, TRUE);
}

GST_END_TEST;

GST_START_TEST (test_apply_not_discont)
{
  do_one_buffer_test_apply (FALSE, FALSE);
}

GST_END_TEST;

GST_START_TEST (test_apply_clean_point)
{
  do_one_buffer_test_apply (TRUE, FALSE);
}

GST_END_TEST;

GST_START_TEST (test_apply_no_e_bit)
{
  do_two_buffers_test_apply (FALSE);
}

GST_END_TEST;

GST_START_TEST (test_apply_e_bit)
{
  do_two_buffers_test_apply (TRUE);
}

GST_END_TEST;

static Suite *
onviftimestamp_suite (void)
{
  Suite *s = suite_create ("onviftimestamp");
  TCase *tc_chain;

  tc_chain = tcase_create ("apply");

  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, setup, cleanup);

  tcase_add_test (tc_chain, test_apply_discont);
  tcase_add_test (tc_chain, test_apply_not_discont);
  tcase_add_test (tc_chain, test_apply_clean_point);
  tcase_add_test (tc_chain, test_apply_no_e_bit);
  tcase_add_test (tc_chain, test_apply_e_bit);

  return s;
}

GST_CHECK_MAIN (onviftimestamp);
