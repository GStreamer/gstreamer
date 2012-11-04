/* GStreamer
 *
 * Unit test for gstrtpbin sending rtp packets using GstBufferList.
 * Copyright (C) 2009 Branko Subasic <branko dot subasic at axis dot com>
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

#include <gst/check/gstcheck.h>

#include <gst/rtp/gstrtpbuffer.h>


#if 0

/* This test makes sure that RTP packets sent as buffer lists are sent through
 * the rtpbin as they are supposed to, and not corrupted in any way.
 */


#define TEST_CAPS \
  "application/x-rtp, "                \
  "media=(string)video, "              \
  "clock-rate=(int)90000, "            \
  "encoding-name=(string)H264, "       \
  "profile-level-id=(string)4d4015, "  \
  "payload=(int)96, "                  \
  "ssrc=(guint)2633237432, "           \
  "clock-base=(guint)1868267015, "     \
  "seqnum-base=(guint)54229"


/* RTP headers and the first 2 bytes of the payload (FU indicator and FU header)
 */
static const guint8 rtp_header[2][14] = {
  {0x80, 0x60, 0xbb, 0xb7, 0x5c, 0xe9, 0x09,
      0x0d, 0xf5, 0x9c, 0x43, 0x55, 0x1c, 0x86},
  {0x80, 0x60, 0xbb, 0xb8, 0x5c, 0xe9, 0x09,
      0x0d, 0xf5, 0x9c, 0x43, 0x55, 0x1c, 0x46}
};

static const guint rtp_header_len[] = {
  sizeof rtp_header[0],
  sizeof rtp_header[1]
};

static GstBuffer *header_buffer[2] = { NULL, NULL };


/* Some payload.
 */
static const char *payload =
    "0123456789ABSDEF0123456789ABSDEF0123456789ABSDEF0123456789ABSDEF0123456789ABSDEF"
    "0123456789ABSDEF0123456789ABSDEF0123456789ABSDEF0123456789ABSDEF0123456789ABSDEF"
    "0123456789ABSDEF0123456789ABSDEF0123456789ABSDEF0123456789ABSDEF0123456789ABSDEF"
    "0123456789ABSDEF0123456789ABSDEF0123456789ABSDEF0123456789ABSDEF0123456789ABSDEF"
    "0123456789ABSDEF0123456789ABSDEF0123456789ABSDEF0123456789ABSDEF0123456789ABSDEF"
    "0123456789ABSDEF0123456789ABSDEF0123456789ABSDEF0123456789ABSDEF0123456789ABSDEF"
    "0123456789ABSDEF0123456";

static const guint payload_offset[] = {
  0, 498
};

static const guint payload_len[] = {
  498, 5
};


static GstBuffer *original_buffer = NULL;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp"));


static GstBuffer *
_create_original_buffer (void)
{
  GstCaps *caps;

  if (original_buffer != NULL)
    return original_buffer;

  original_buffer = gst_buffer_new ();
  fail_unless (original_buffer != NULL);

  gst_buffer_set_data (original_buffer, (guint8 *) payload, strlen (payload));
  GST_BUFFER_TIMESTAMP (original_buffer) =
      gst_clock_get_internal_time (gst_system_clock_obtain ());

  caps = gst_caps_from_string (TEST_CAPS);
  fail_unless (caps != NULL);
  gst_buffer_set_caps (original_buffer, caps);
  gst_caps_unref (caps);

  return original_buffer;
}

static GstBufferList *
_create_buffer_list (void)
{
  GstBufferList *list;
  GstBufferListIterator *it;
  GstBuffer *orig_buffer;
  GstBuffer *buffer;

  orig_buffer = _create_original_buffer ();
  fail_if (orig_buffer == NULL);

  list = gst_buffer_list_new ();
  fail_if (list == NULL);

  it = gst_buffer_list_iterate (list);
  fail_if (it == NULL);

  /*** First group, i.e. first packet. **/
  gst_buffer_list_iterator_add_group (it);

  /* Create buffer with RTP header and add it to the 1st group */
  buffer = gst_buffer_new ();
  GST_BUFFER_MALLOCDATA (buffer) = g_memdup (&rtp_header[0], rtp_header_len[0]);
  GST_BUFFER_DATA (buffer) = GST_BUFFER_MALLOCDATA (buffer);
  GST_BUFFER_SIZE (buffer) = rtp_header_len[0];
  gst_buffer_copy_metadata (buffer, orig_buffer, GST_BUFFER_COPY_ALL);
  header_buffer[0] = buffer;
  gst_buffer_list_iterator_add (it, buffer);

  /* Create the payload buffer and add it to the 1st group
   */
  buffer =
      gst_buffer_create_sub (orig_buffer, payload_offset[0], payload_len[0]);
  fail_if (buffer == NULL);
  gst_buffer_list_iterator_add (it, buffer);


  /***  Second group, i.e. second packet. ***/

  /* Create a new group to hold the rtp header and the payload */
  gst_buffer_list_iterator_add_group (it);

  /* Create buffer with RTP header and add it to the 2nd group */
  buffer = gst_buffer_new ();
  GST_BUFFER_MALLOCDATA (buffer) = g_memdup (&rtp_header[1], rtp_header_len[1]);
  GST_BUFFER_DATA (buffer) = GST_BUFFER_MALLOCDATA (buffer);
  GST_BUFFER_SIZE (buffer) = rtp_header_len[1];
  gst_buffer_copy_metadata (buffer, orig_buffer, GST_BUFFER_COPY_ALL);
  header_buffer[1] = buffer;

  /* Add the rtp header to the buffer list */
  gst_buffer_list_iterator_add (it, buffer);

  /* Create the payload buffer and add it to the 2d group
   */
  buffer =
      gst_buffer_create_sub (orig_buffer, payload_offset[1], payload_len[1]);
  fail_if (buffer == NULL);
  gst_buffer_list_iterator_add (it, buffer);

  gst_buffer_list_iterator_free (it);

  return list;
}


static void
_check_header (GstBuffer * buffer, guint index)
{
  guint8 *data;

  fail_if (buffer == NULL);
  fail_unless (index < 2);

  fail_unless (GST_BUFFER_SIZE (buffer) == rtp_header_len[index]);

  /* Can't do a memcmp() on the whole header, cause the SSRC (bytes 8-11) will
   * most likely be changed in gstrtpbin.
   */
  fail_unless ((data = GST_BUFFER_DATA (buffer)) != NULL);
  fail_unless_equals_uint64 (*(guint64 *) data, *(guint64 *) rtp_header[index]);
  fail_unless (*(guint16 *) (data + 12) ==
      *(guint16 *) (rtp_header[index] + 12));
}


static void
_check_payload (GstBuffer * buffer, guint index)
{
  fail_if (buffer == NULL);
  fail_unless (index < 2);

  fail_unless (GST_BUFFER_SIZE (buffer) == payload_len[index]);
  fail_if (GST_BUFFER_DATA (buffer) !=
      (gpointer) (payload + payload_offset[index]));
  fail_if (memcmp (GST_BUFFER_DATA (buffer), payload + payload_offset[index],
          payload_len[index]));
}


static void
_check_group (GstBufferListIterator * it, guint index, GstCaps * caps)
{
  GstBuffer *buffer;

  fail_unless (it != NULL);
  fail_unless (gst_buffer_list_iterator_n_buffers (it) == 2);
  fail_unless (caps != NULL);

  fail_unless ((buffer = gst_buffer_list_iterator_next (it)) != NULL);

  fail_unless (GST_BUFFER_TIMESTAMP (buffer) ==
      GST_BUFFER_TIMESTAMP (original_buffer));

  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (original_buffer),
          GST_BUFFER_CAPS (buffer)));

  _check_header (buffer, index);

  fail_unless ((buffer = gst_buffer_list_iterator_next (it)) != NULL);
  _check_payload (buffer, index);
}


static GstFlowReturn
_sink_chain_list (GstPad * pad, GstBufferList * list)
{
  GstCaps *caps;
  GstBufferListIterator *it;

  caps = gst_caps_from_string (TEST_CAPS);
  fail_unless (caps != NULL);

  fail_unless (GST_IS_BUFFER_LIST (list));
  fail_unless (gst_buffer_list_n_groups (list) == 2);

  it = gst_buffer_list_iterate (list);
  fail_if (it == NULL);

  fail_unless (gst_buffer_list_iterator_next_group (it));
  _check_group (it, 0, caps);

  fail_unless (gst_buffer_list_iterator_next_group (it));
  _check_group (it, 1, caps);

  gst_caps_unref (caps);
  gst_buffer_list_iterator_free (it);

  gst_buffer_list_unref (list);

  return GST_FLOW_OK;
}


static void
_set_chain_functions (GstPad * pad)
{
  gst_pad_set_chain_list_function (pad, _sink_chain_list);
}


GST_START_TEST (test_bufferlist)
{
  GstElement *rtpbin;
  GstPad *sinkpad;
  GstPad *srcpad;
  GstBufferList *list;

  list = _create_buffer_list ();
  fail_unless (list != NULL);

  rtpbin = gst_check_setup_element ("gstrtpbin");

  srcpad =
      gst_check_setup_src_pad_by_name (rtpbin, &srctemplate, "send_rtp_sink_0");
  fail_if (srcpad == NULL);
  sinkpad =
      gst_check_setup_sink_pad_by_name (rtpbin, &sinktemplate,
      "send_rtp_src_0");
  fail_if (sinkpad == NULL);

  _set_chain_functions (sinkpad);

  gst_pad_set_active (sinkpad, TRUE);
  gst_element_set_state (rtpbin, GST_STATE_PLAYING);
  fail_unless (gst_pad_push_list (srcpad, list) == GST_FLOW_OK);
  gst_pad_set_active (sinkpad, FALSE);

  gst_check_teardown_pad_by_name (rtpbin, "send_rtp_src_0");
  gst_check_teardown_pad_by_name (rtpbin, "send_rtp_sink_0");
  gst_check_teardown_element (rtpbin);
}

GST_END_TEST;

#endif


static Suite *
bufferlist_suite (void)
{
  Suite *s = suite_create ("BufferList");

  TCase *tc_chain = tcase_create ("general");

  /* time out after 30s. */
  tcase_set_timeout (tc_chain, 10);

  suite_add_tcase (s, tc_chain);
#if 0
  tcase_add_test (tc_chain, test_bufferlist);
#endif

  return s;
}

GST_CHECK_MAIN (bufferlist);
