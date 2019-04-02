/* GStreamer
 *
 * Unit test for gstrtpbin sending rtp packets using GstBufferList.
 * Copyright (C) 2009 Branko Subasic <branko dot subasic at axis dot com>
 * Copyright 2019, Collabora Ltd.
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
create_original_buffer (void)
{
  if (original_buffer != NULL)
    return original_buffer;

  original_buffer =
      gst_buffer_new_wrapped ((guint8 *) payload, strlen (payload));
  fail_unless (original_buffer != NULL);

  GST_BUFFER_TIMESTAMP (original_buffer) =
      gst_clock_get_internal_time (gst_system_clock_obtain ());

  return original_buffer;
}

static GstBuffer *
create_rtp_packet_buffer (gconstpointer header, gint header_size,
    GstBuffer * payload_buffer, gint payload_offset, gint payload_size)
{
  GstBuffer *buffer;
  GstBuffer *sub_buffer;

  /* Create buffer with RTP header. */
  buffer = gst_buffer_new_allocate (NULL, header_size, NULL);
  gst_buffer_fill (buffer, 0, header, header_size);
  gst_buffer_copy_into (buffer, payload_buffer, GST_BUFFER_COPY_METADATA, 0,
      -1);

  /* Create the payload buffer and add it to the current buffer. */
  sub_buffer =
      gst_buffer_copy_region (payload_buffer, GST_BUFFER_COPY_MEMORY,
      payload_offset, payload_size);

  buffer = gst_buffer_append (buffer, sub_buffer);
  fail_if (buffer == NULL);

  return buffer;
}

static void
check_header (GstBuffer * buffer, guint index)
{
  GstMemory *memory;
  GstMapInfo info;
  gboolean ret;

  fail_if (buffer == NULL);
  fail_unless (index < 2);

  memory = gst_buffer_get_memory (buffer, 0);
  ret = gst_memory_map (memory, &info, GST_MAP_READ);
  fail_if (ret == FALSE);

  fail_unless (info.size == rtp_header_len[index]);

  /* Can't do a memcmp() on the whole header, cause the SSRC (bytes 8-11) will
   * most likely be changed in gstrtpbin.
   */
  fail_unless (info.data != NULL);
  fail_unless_equals_uint64 (*(guint64 *) info.data,
      *(guint64 *) rtp_header[index]);
  fail_unless (*(guint16 *) (info.data + 12) ==
      *(guint16 *) (rtp_header[index] + 12));

  gst_memory_unmap (memory, &info);
  gst_memory_unref (memory);
}

static void
check_payload (GstBuffer * buffer, guint index)
{
  GstMemory *memory;
  GstMapInfo info;
  gboolean ret;

  fail_if (buffer == NULL);
  fail_unless (index < 2);

  memory = gst_buffer_get_memory (buffer, 1);
  ret = gst_memory_map (memory, &info, GST_MAP_READ);
  fail_if (ret == FALSE);

  fail_unless (info.size == payload_len[index]);
  fail_if (info.data != (gpointer) (payload + payload_offset[index]));
  fail_if (memcmp (info.data, payload + payload_offset[index],
          payload_len[index]));

  gst_memory_unmap (memory, &info);
  gst_memory_unref (memory);
}

static void
check_packet (GstBufferList * list, guint list_index, guint packet_index)
{
  GstBuffer *buffer;

  fail_unless (list != NULL);

  fail_unless ((buffer = gst_buffer_list_get (list, list_index)) != NULL);
  fail_unless (gst_buffer_n_memory (buffer) == 2);

  fail_unless (GST_BUFFER_TIMESTAMP (buffer) ==
      GST_BUFFER_TIMESTAMP (original_buffer));

  check_header (buffer, packet_index);
  check_payload (buffer, packet_index);
}

/*
 * Used to verify that the chain_list function is actually implemented by the
 * element and called when executing the pipeline. This is needed because pads
 * always have a default chain_list handler which handle buffers in a buffer
 * list individually, and pushing a list to a pad can succeed even if no
 * chain_list handler has been set.
 */
static gboolean chain_list_func_called;

/* Create two packets with different payloads. */
static GstBufferList *
create_buffer_list (void)
{
  GstBufferList *list;
  GstBuffer *orig_buffer;
  GstBuffer *buffer;

  orig_buffer = create_original_buffer ();
  fail_if (orig_buffer == NULL);

  list = gst_buffer_list_new ();
  fail_if (list == NULL);

  /*** First packet. **/
  buffer =
      create_rtp_packet_buffer (&rtp_header[0], rtp_header_len[0], orig_buffer,
      payload_offset[0], payload_len[0]);
  gst_buffer_list_add (list, buffer);

  /***  Second packet. ***/
  buffer =
      create_rtp_packet_buffer (&rtp_header[1], rtp_header_len[1], orig_buffer,
      payload_offset[1], payload_len[1]);
  gst_buffer_list_add (list, buffer);

  return list;
}

/* Check that the correct packets have been pushed out of the element. */
static GstFlowReturn
sink_chain_list (GstPad * pad, GstObject * parent, GstBufferList * list)
{
  GstCaps *current_caps;
  GstCaps *caps;

  chain_list_func_called = TRUE;

  current_caps = gst_pad_get_current_caps (pad);
  fail_unless (current_caps != NULL);

  caps = gst_caps_from_string (TEST_CAPS);
  fail_unless (caps != NULL);

  fail_unless (gst_caps_is_equal (caps, current_caps));
  gst_caps_unref (caps);
  gst_caps_unref (current_caps);

  fail_unless (GST_IS_BUFFER_LIST (list));
  fail_unless (gst_buffer_list_length (list) == 2);

  fail_unless (gst_buffer_list_get (list, 0));
  check_packet (list, 0, 0);

  fail_unless (gst_buffer_list_get (list, 1));
  check_packet (list, 1, 1);

  gst_buffer_list_unref (list);

  return GST_FLOW_OK;
}

/* Get the stats of the **first** source of the given type (get_sender) */
static void
get_session_source_stats (GstElement * rtpbin, guint session,
    gboolean get_sender, GstStructure ** source_stats)
{
  GstElement *rtpsession;
  GstStructure *stats;
  GValueArray *stats_arr;
  guint i;

  g_signal_emit_by_name (rtpbin, "get-session", session, &rtpsession);
  fail_if (rtpsession == NULL);

  g_object_get (rtpsession, "stats", &stats, NULL);
  stats_arr =
      g_value_get_boxed (gst_structure_get_value (stats, "source-stats"));
  g_assert (stats_arr != NULL);
  fail_unless (stats_arr->n_values >= 1);

  *source_stats = NULL;
  for (i = 0; i < stats_arr->n_values; i++) {
    GstStructure *tmp_source_stats;
    gboolean is_sender;

    tmp_source_stats = g_value_dup_boxed (&stats_arr->values[i]);
    gst_structure_get (tmp_source_stats, "is-sender", G_TYPE_BOOLEAN,
        &is_sender, NULL);

    /* Return the stats of the **first** source of the given type. */
    if (is_sender == get_sender) {
      *source_stats = tmp_source_stats;
      break;
    }
    gst_structure_free (tmp_source_stats);
  }

  gst_structure_free (stats);
  gst_object_unref (rtpsession);
}

GST_START_TEST (test_bufferlist)
{
  GstElement *rtpbin;
  GstPad *srcpad;
  GstPad *sinkpad;
  GstCaps *caps;
  GstBufferList *list;
  GstStructure *stats;
  guint64 packets_sent;
  guint64 packets_received;

  list = create_buffer_list ();
  fail_unless (list != NULL);

  rtpbin = gst_check_setup_element ("rtpbin");

  srcpad =
      gst_check_setup_src_pad_by_name (rtpbin, &srctemplate, "send_rtp_sink_0");
  fail_if (srcpad == NULL);
  sinkpad =
      gst_check_setup_sink_pad_by_name (rtpbin, &sinktemplate,
      "send_rtp_src_0");
  fail_if (sinkpad == NULL);

  gst_pad_set_chain_list_function (sinkpad,
      GST_DEBUG_FUNCPTR (sink_chain_list));

  gst_pad_set_active (srcpad, TRUE);
  gst_pad_set_active (sinkpad, TRUE);

  caps = gst_caps_from_string (TEST_CAPS);
  gst_check_setup_events (srcpad, rtpbin, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  gst_element_set_state (rtpbin, GST_STATE_PLAYING);

  chain_list_func_called = FALSE;
  fail_unless (gst_pad_push_list (srcpad, list) == GST_FLOW_OK);
  fail_if (chain_list_func_called == FALSE);

  /* make sure that stats about the number of sent packets are OK too */
  get_session_source_stats (rtpbin, 0, TRUE, &stats);
  fail_if (stats == NULL);

  gst_structure_get (stats,
      "packets-sent", G_TYPE_UINT64, &packets_sent,
      "packets-received", G_TYPE_UINT64, &packets_received, NULL);
  fail_unless (packets_sent == 2);
  fail_unless (packets_received == 2);
  gst_structure_free (stats);

  gst_pad_set_active (sinkpad, FALSE);
  gst_pad_set_active (srcpad, FALSE);

  gst_check_teardown_pad_by_name (rtpbin, "send_rtp_src_0");
  gst_check_teardown_pad_by_name (rtpbin, "send_rtp_sink_0");
  gst_check_teardown_element (rtpbin);
}

GST_END_TEST;

static Suite *
bufferlist_suite (void)
{
  Suite *s = suite_create ("BufferList");

  TCase *tc_chain = tcase_create ("general");

  /* time out after 30s. */
  tcase_set_timeout (tc_chain, 10);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_bufferlist);

  return s;
}

GST_CHECK_MAIN (bufferlist);
