/* GStreamer
 *
 * unit test for data protocol
 *
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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

#include "config.h"

#include <gst/check/gstcheck.h>

#ifndef GST_REMOVE_DEPRECATED
#undef GST_DISABLE_DEPRECATED
#endif

#include <gst/dataprotocol/dataprotocol.h>
#include "libs/gst/dataprotocol/dp-private.h"   /* private header */

/* test our method of reading and writing headers using TO/FROM_BE */
GST_START_TEST (test_conversion)
{
  guint8 array[9];
  guint8 write_array[9];
  guint16 read_two, expect_two;
  guint32 read_four, expect_four;
  guint64 read_eight, expect_eight;
  int i;

  for (i = 0; i < 9; ++i) {
    array[i] = i * 0x10;
  }

  /* read 8 16 bits */
  for (i = 0; i < 8; ++i) {
    read_two = GST_READ_UINT16_BE (array + i);
    expect_two = array[i] * (1 << 8) + array[i + 1];
    fail_unless (read_two == expect_two,
        "GST_READ_UINT16_BE %d: read %d != %d", i, read_two, expect_two);
  }

  /* write 8 16 bits */
  for (i = 0; i < 8; ++i) {
    GST_WRITE_UINT16_BE (&write_array[i], read_two);
    fail_unless (memcmp (array + 7, write_array + i, 2) == 0,
        "GST_WRITE_UINT16_BE %d: memcmp failed", i);
  }

  /* read 5 32 bits */
  for (i = 0; i < 5; ++i) {
    read_four = GST_READ_UINT32_BE (array + i);
    expect_four = array[i] * (1 << 24) + array[i + 1] * (1 << 16)
        + array[i + 2] * (1 << 8) + array[i + 3];
    fail_unless (read_four == expect_four,
        "GST_READ_UINT32_BE %d: read %d != %d", i, read_four, expect_four);
  }

  /* read 2 64 bits */
  for (i = 0; i < 2; ++i) {
    read_eight = GST_READ_UINT64_BE (array + i);
    expect_eight = array[i] * (1LL << 56) + array[i + 1] * (1LL << 48)
        + array[i + 2] * (1LL << 40) + array[i + 3] * (1LL << 32)
        + array[i + 4] * (1 << 24) + array[i + 5] * (1 << 16)
        + array[i + 6] * (1 << 8) + array[i + 7];
    fail_unless (read_eight == expect_eight,
        "GST_READ_UINT64_BE %d: read %" G_GUINT64_FORMAT
        " != %" G_GUINT64_FORMAT, i, read_eight, expect_eight);
  }

  /* write 1 64 bit */
  GST_WRITE_UINT64_BE (&write_array[0], read_eight);
  fail_unless (memcmp (array + 1, write_array, 8) == 0,
      "GST_WRITE_UINT64_BE: memcmp failed");
}

GST_END_TEST;

#ifndef GST_REMOVE_DEPRECATED   /* these tests use deprecated API, that we disable by default */

/* test creation of header from buffer and back again */
GST_START_TEST (test_buffer)
{
  GstBuffer *buffer;
  GstBuffer *newbuffer;

  guint header_length;
  guint8 *header;

  /* create buffer */
  GST_DEBUG ("Creating a new 8-byte buffer with ts 0.5 sec, dur 1 sec");
  buffer = gst_buffer_new_and_alloc (8);
  GST_BUFFER_TIMESTAMP (buffer) = (GstClockTime) (GST_SECOND * 0.5);
  GST_BUFFER_DURATION (buffer) = (GstClockTime) GST_SECOND;
  GST_BUFFER_OFFSET (buffer) = (guint64) 10;
  GST_BUFFER_OFFSET_END (buffer) = (guint64) 19;
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_IN_CAPS);
  memmove (GST_BUFFER_DATA (buffer), "a buffer", 8);

  /* create a buffer with CRC checking */
  fail_unless (gst_dp_header_from_buffer (buffer, GST_DP_HEADER_FLAG_CRC,
          &header_length, &header), "Could not create header from buffer.");

  /* validate the header */
  fail_unless (gst_dp_validate_header (header_length, header),
      "Could not validate header");
  /* create a new, empty buffer with the right size */
  newbuffer = gst_dp_buffer_from_header (header_length, header);
  fail_unless (newbuffer != NULL, "Could not create a new buffer from header");
  fail_unless (GST_IS_BUFFER (newbuffer), "Created buffer is not a GstBuffer");
  /* read/copy the data */
  memmove (GST_BUFFER_DATA (newbuffer), GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer));
  /* validate the buffer */
  fail_unless (gst_dp_validate_payload (header_length, header,
          GST_BUFFER_DATA (newbuffer)), "Could not validate payload");

  GST_DEBUG ("new buffer timestamp: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (newbuffer)));
  GST_DEBUG ("new buffer duration: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_DURATION (newbuffer)));
  GST_DEBUG ("new buffer offset: %" G_GUINT64_FORMAT,
      GST_BUFFER_OFFSET (newbuffer));
  GST_DEBUG ("new buffer offset_end: %" G_GUINT64_FORMAT,
      GST_BUFFER_OFFSET_END (newbuffer));
  fail_unless (GST_BUFFER_TIMESTAMP (newbuffer) ==
      GST_BUFFER_TIMESTAMP (buffer), "Timestamps don't match !");
  fail_unless (GST_BUFFER_DURATION (newbuffer) == GST_BUFFER_DURATION (buffer),
      "Durations don't match !");
  fail_unless (GST_BUFFER_OFFSET (newbuffer) == GST_BUFFER_OFFSET (buffer),
      "Offsets don't match !");
  fail_unless (GST_BUFFER_OFFSET_END (newbuffer) ==
      GST_BUFFER_OFFSET_END (buffer), "Offset ends don't match !");
  fail_unless (GST_BUFFER_FLAG_IS_SET (newbuffer, GST_BUFFER_FLAG_IN_CAPS),
      "GST_BUFFER_IN_CAPS flag should have been copied !");

  /* clean up */
  gst_buffer_unref (buffer);
  gst_buffer_unref (newbuffer);
  g_free (header);
}

GST_END_TEST;

GST_START_TEST (test_caps)
{
  gchar *string, *newstring;
  GstCaps *caps, *newcaps;

  guint header_length;
  guint8 *header, *payload;

  caps = gst_caps_from_string ("audio/x-raw-float, "
      "rate = (int) [ 11025, 48000 ], "
      "channels = (int) [ 1, 2 ], " "endianness = (int) BYTE_ORDER, "
      "width = (int) 32, " "buffer-frames = (int) 0");
  string = gst_caps_to_string (caps);
  GST_DEBUG ("Created caps: %s", string);
  fail_unless (gst_dp_packet_from_caps (caps, 0, &header_length, &header,
          &payload), "Could not create packet from caps.");

  /* validate the packet */
  fail_unless (gst_dp_validate_packet (header_length, header, payload),
      "Could not validate packet");
  newcaps = gst_dp_caps_from_packet (header_length, header, payload);
  fail_unless (newcaps != NULL, "Could not create caps from packet");
  fail_unless (GST_IS_CAPS (newcaps));
  newstring = gst_caps_to_string (newcaps);
  GST_DEBUG ("Received caps: %s", newstring);
  fail_unless (strcmp (string, newstring) == 0,
      "Created caps do not match original caps");

  /* cleanup */
  gst_caps_unref (caps);
  gst_caps_unref (newcaps);
  g_free (header);
  g_free (payload);
  g_free (string);
  g_free (newstring);
}

GST_END_TEST;

GST_START_TEST (test_event)
{
  GstEvent *send;
  GstEvent *receive;
  guint header_length;
  guint8 *header, *payload;

  GST_DEBUG ("Testing EOS event at 1s");
  send = gst_event_new_eos ();
  GST_EVENT_TIMESTAMP (send) = GST_SECOND;
  fail_unless (gst_dp_packet_from_event (send, GST_DP_HEADER_FLAG_CRC,
          &header_length, &header, &payload),
      "Could not create packet from eos event");

  receive = gst_dp_event_from_packet (header_length, header, payload);

  GST_DEBUG ("EOS, timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_EVENT_TIMESTAMP (receive)));
  fail_unless (GST_EVENT_TYPE (receive) == GST_EVENT_EOS,
      "Received event is not EOS");
  fail_unless (GST_EVENT_TIMESTAMP (receive) == GST_SECOND,
      "EOS timestamp is not 1.0 sec");

  /* clean up */
  g_free (header);
  g_free (payload);
  gst_event_unref (send);
  gst_event_unref (receive);

  GST_DEBUG ("Testing FLUSH event at 2s");
  send = gst_event_new_flush_start ();
  GST_EVENT_TIMESTAMP (send) = GST_SECOND * 2;
  fail_unless (gst_dp_packet_from_event (send, GST_DP_HEADER_FLAG_CRC,
          &header_length, &header, &payload),
      "Could not create packet from flush event");

  receive = gst_dp_event_from_packet (header_length, header, payload);

  GST_DEBUG ("Flush, timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_EVENT_TIMESTAMP (receive)));
  fail_unless (GST_EVENT_TYPE (receive) == GST_EVENT_FLUSH_START,
      "Received event is not flush");
  fail_unless (GST_EVENT_TIMESTAMP (receive) == GST_SECOND * 2,
      "Flush timestamp is not 2.0 sec");

  /* clean up */
  g_free (header);
  g_free (payload);
  gst_event_unref (send);
  gst_event_unref (receive);

  GST_DEBUG ("Testing SEEK event with 1 second at 3 seconds");
  send =
      gst_event_new_seek (1.0, GST_FORMAT_TIME, 0, GST_SEEK_TYPE_SET,
      GST_SECOND, GST_SEEK_TYPE_NONE, 0);
  GST_EVENT_TIMESTAMP (send) = GST_SECOND * 3;
  fail_unless (gst_dp_packet_from_event (send, GST_DP_HEADER_FLAG_CRC,
          &header_length, &header, &payload),
      "Could not create packet from seek event");

  receive = gst_dp_event_from_packet (header_length, header, payload);

  {
    gdouble rate;
    GstFormat format;
    GstSeekFlags flags;
    GstSeekType cur_type, stop_type;
    gint64 cur, stop;

    gst_event_parse_seek (receive, &rate, &format, &flags,
        &cur_type, &cur, &stop_type, &stop);

    GST_DEBUG ("Seek, timestamp %" GST_TIME_FORMAT ", to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_EVENT_TIMESTAMP (receive)), GST_TIME_ARGS (cur));
    fail_unless (GST_EVENT_TYPE (receive) == GST_EVENT_SEEK,
        "Returned event is not seek");
    fail_unless (GST_EVENT_TIMESTAMP (receive) == GST_SECOND * 3,
        "Seek timestamp is not 3.0 sec");
    fail_unless (format == GST_FORMAT_TIME, "Seek format is not time");
    fail_unless (cur == GST_SECOND, "Seek cur is not 1.0 sec");
  }

  /* clean up */
  g_free (header);
  g_free (payload);
  gst_event_unref (send);
  gst_event_unref (receive);
}

GST_END_TEST;

/* try to segfault the thing by passing NULLs, short headers, etc.. */
GST_START_TEST (test_memory)
{
  guint8 foo[5];
  GstBuffer *buffer;
  GstCaps *caps;
  GstEvent *event;
  guint length;
  guint8 *header;
  guint8 *payload;

  /* check 0 sized input, data pointer can be NULL or anything. CRC is always 0,
   * though. */
  fail_if (gst_dp_crc (NULL, 0) != 0);
  fail_if (gst_dp_crc (foo, 0) != 0);

  /* this is very invalid input and gives a warning. */
  ASSERT_CRITICAL (gst_dp_crc (NULL, 1));
  ASSERT_CRITICAL (gst_dp_header_payload_length (NULL));
  ASSERT_CRITICAL (gst_dp_header_payload_type (NULL));

  /* wrong */
  ASSERT_CRITICAL (gst_dp_header_from_buffer (NULL, 0, NULL, NULL));

  /* empty buffer has NULL as data pointer */
  buffer = gst_buffer_new_and_alloc (0);

  /* no place to store the length and/or header data */
  ASSERT_CRITICAL (gst_dp_header_from_buffer (buffer, 0, NULL, NULL));
  ASSERT_CRITICAL (gst_dp_header_from_buffer (buffer, 0, &length, NULL));

  /* this should work fine */
  fail_if (gst_dp_header_from_buffer (buffer, 0, &length, &header) != TRUE);
  fail_unless (length != 0);
  fail_unless (header != NULL);

  /* this should validate */
  fail_if (gst_dp_validate_header (length, header) == FALSE);

  /* NULL header pointer */
  ASSERT_CRITICAL (gst_dp_validate_header (length, NULL));
  /* short header */
  ASSERT_CRITICAL (gst_dp_validate_header (5, header));

  g_free (header);

  /* this should work and not crash trying to calc a CRC on a 0 sized buffer */
  fail_if (gst_dp_header_from_buffer (buffer,
          GST_DP_HEADER_FLAG_CRC_HEADER | GST_DP_HEADER_FLAG_CRC_PAYLOAD,
          &length, &header) != TRUE);

  /* this should validate */
  fail_if (gst_dp_validate_header (length, header) == FALSE);

  /* there was no payload, NULL as payload data should validate the CRC
   * checks and all. */
  fail_if (gst_dp_validate_payload (length, header, NULL) == FALSE);

  /* and the whole packet as well */
  fail_if (gst_dp_validate_packet (length, header, NULL) == FALSE);

  /* some bogus length */
  ASSERT_CRITICAL (gst_dp_validate_packet (5, header, NULL));
  gst_buffer_unref (buffer);

  /* create buffer from header data, integrity tested elsewhere */
  buffer = gst_dp_buffer_from_header (length, header);
  fail_if (buffer == NULL);
  gst_buffer_unref (buffer);
  g_free (header);

  ASSERT_CRITICAL (gst_dp_packet_from_caps (NULL, 0, NULL, NULL, NULL));

  /* some caps stuff */
  caps = gst_caps_new_empty ();
  ASSERT_CRITICAL (gst_dp_packet_from_caps (caps, 0, NULL, NULL, NULL));
  ASSERT_CRITICAL (gst_dp_packet_from_caps (caps, 0, &length, NULL, NULL));
  ASSERT_CRITICAL (gst_dp_packet_from_caps (caps, 0, &length, &header, NULL));

  fail_if (gst_dp_packet_from_caps (caps, 0, &length, &header,
          &payload) != TRUE);
  fail_if (strcmp ((const gchar *) payload, "EMPTY") != 0);
  gst_caps_unref (caps);

  caps = gst_dp_caps_from_packet (length, header, payload);
  fail_if (caps == NULL);
  gst_caps_unref (caps);

  g_free (header);
  g_free (payload);

  /* some event stuff */
  event = gst_event_new_eos ();
  ASSERT_CRITICAL (gst_dp_packet_from_event (event, 0, NULL, NULL, NULL));
  ASSERT_CRITICAL (gst_dp_packet_from_event (event, 0, &length, NULL, NULL));
  ASSERT_CRITICAL (gst_dp_packet_from_event (event, 0, &length, &header, NULL));

  /* payload is not NULL from previous test and points to freed memory, very
   * invalid. */
  fail_if (payload == NULL);
  fail_if (gst_dp_packet_from_event (event, 0, &length, &header,
          &payload) != TRUE);

  /* the EOS event has no payload */
  fail_if (payload != NULL);
  gst_event_unref (event);

  event = gst_dp_event_from_packet (length, header, payload);
  fail_if (event == NULL);
  fail_if (GST_EVENT_TYPE (event) != GST_EVENT_EOS);
  gst_event_unref (event);

  g_free (header);
  g_free (payload);
}

GST_END_TEST;

#endif

static Suite *
gst_dp_suite (void)
{
  Suite *s = suite_create ("data protocol");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, gst_dp_init, NULL);
  tcase_add_test (tc_chain, test_conversion);
#ifndef GST_REMOVE_DEPRECATED
  tcase_add_test (tc_chain, test_buffer);
  tcase_add_test (tc_chain, test_caps);
  tcase_add_test (tc_chain, test_event);
  tcase_add_test (tc_chain, test_memory);
#endif

  return s;
}

GST_CHECK_MAIN (gst_dp);
