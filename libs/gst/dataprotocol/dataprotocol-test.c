/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * dataprotocol-test.c: Test functions for GStreamer Data Protocol
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

#include <gst/gst.h>
#include <gst/dataprotocol/dataprotocol.h>
#include "dp-private.h"

#include <string.h>             /* memcmp */

/* test our method of reading and writing headers using TO/FROM_BE */
static int
conversion_test ()
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
    read_two = GUINT16_FROM_BE (*((guint16 *) (array + i)));
    expect_two = array[i] * (1 << 8) + array[i + 1];
    if (read_two != expect_two) {
      g_print ("GUINT16_FROM_BE %d: read %d != %d\n", i, read_two, expect_two);
      return -1;
    }
  }

  /* write 8 16 bit */
  for (i = 0; i < 8; ++i) {
    *((guint16 *) & write_array[i]) = GUINT16_TO_BE (read_two);
    if (memcmp (array + 7, write_array + i, 2) != 0) {
      gst_dp_dump_byte_array (write_array + i, 2);
      gst_dp_dump_byte_array (array + 7, 2);
      return -1;
    }
  }

  /* read 5 32 bits */
  for (i = 0; i < 5; ++i) {
    read_four = GUINT32_FROM_BE (*((guint32 *) (array + i)));
    expect_four = array[i] * (1 << 24) + array[i + 1] * (1 << 16)
        + array[i + 2] * (1 << 8) + array[i + 3];
    if (read_four != expect_four) {
      g_print ("GUINT32_FROM_BE %d: read %d != %d\n", i, read_four,
          expect_four);
      return -1;
    }
  }

  /* read 2 64 bits */
  for (i = 0; i < 2; ++i) {
    read_eight = GUINT64_FROM_BE (*((guint64 *) (array + i)));
    expect_eight = array[i] * (1LL << 56) + array[i + 1] * (1LL << 48)
        + array[i + 2] * (1LL << 40) + array[i + 3] * (1LL << 32)
        + array[i + 4] * (1 << 24) + array[i + 5] * (1 << 16)
        + array[i + 6] * (1 << 8) + array[i + 7];
    ;
    if (read_eight != expect_eight) {
      g_print ("GUINT64_FROM_BE %d: read %" G_GUINT64_FORMAT
          " != %" G_GUINT64_FORMAT "\n", i, read_eight, expect_eight);
      return -1;
    }
  }

  /* write 1 64 bit */
  *((guint64 *) & write_array[0]) = GUINT64_TO_BE (read_eight);
  if (memcmp (array + 1, write_array, 8) != 0) {
    gst_dp_dump_byte_array (write_array, 8);
    gst_dp_dump_byte_array (array + 1, 8);
    return -1;
  }

  return 0;
}

/* test creation of header from buffer and back again */
static int
buffer_test ()
{
  GstBuffer *buffer;
  GstBuffer *newbuffer;

  guint header_length;
  guint8 *header;

  /* create buffer */
  g_print ("Creating a new 8-byte buffer with ts 0.5 sec, dur 1 sec\n");
  buffer = gst_buffer_new_and_alloc (8);
  GST_BUFFER_TIMESTAMP (buffer) = (GstClockTime) (GST_SECOND * 0.5);
  GST_BUFFER_DURATION (buffer) = (GstClockTime) GST_SECOND;
  GST_BUFFER_OFFSET (buffer) = (guint64) 10;
  GST_BUFFER_OFFSET_END (buffer) = (guint64) 19;
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_IN_CAPS);
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_SUBBUFFER);
  memmove (GST_BUFFER_DATA (buffer), "a buffer", 8);

  /* create a buffer with CRC checking */
  if (!gst_dp_header_from_buffer (buffer, GST_DP_HEADER_FLAG_CRC,
          &header_length, &header)) {
    g_print ("Could not create header from buffer.");
    exit (1);
  }

  /* validate the header */
  g_return_val_if_fail (gst_dp_validate_header (header_length, header), -1);
  /* create a new, empty buffer with the right size */
  newbuffer = gst_dp_buffer_from_header (header_length, header);
  /* read/copy the data */
  memmove (GST_BUFFER_DATA (newbuffer), GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer));
  /* validate the buffer */
  g_return_val_if_fail (gst_dp_validate_payload (header_length, header,
          GST_BUFFER_DATA (newbuffer)), -1);

  g_return_val_if_fail (newbuffer, -1);
  g_return_val_if_fail (GST_IS_BUFFER (newbuffer), -1);
  g_print ("new buffer timestamp: %" GST_TIME_FORMAT "\n",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (newbuffer)));
  g_print ("new buffer duration: %" GST_TIME_FORMAT "\n",
      GST_TIME_ARGS (GST_BUFFER_DURATION (newbuffer)));
  g_print ("new buffer offset: %" G_GUINT64_FORMAT "\n",
      GST_BUFFER_OFFSET (newbuffer));
  g_print ("new buffer offset_end: %" G_GUINT64_FORMAT "\n",
      GST_BUFFER_OFFSET_END (newbuffer));
  if (GST_BUFFER_TIMESTAMP (newbuffer) != GST_BUFFER_TIMESTAMP (buffer)) {
    g_error ("Timestamps don't match !");
  }
  if (GST_BUFFER_DURATION (newbuffer) != GST_BUFFER_DURATION (buffer)) {
    g_error ("Durations don't match !");
  }
  if (GST_BUFFER_OFFSET (newbuffer) != GST_BUFFER_OFFSET (buffer)) {
    g_error ("Offsets don't match !");
  }
  if (GST_BUFFER_OFFSET_END (newbuffer) != GST_BUFFER_OFFSET_END (buffer)) {
    g_error ("Offset ends don't match !");
  }
  if (GST_BUFFER_FLAG_IS_SET (newbuffer, GST_BUFFER_SUBBUFFER)) {
    g_error ("GST_BUFFER_SUBBUFFER flag should not have been copied !");
  }
  if (!GST_BUFFER_FLAG_IS_SET (newbuffer, GST_BUFFER_IN_CAPS)) {
    g_error ("GST_BUFFER_IN_CAPS flag should have been copied !");
  }
  g_free (header);

  return 0;
}

static int
caps_test ()
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
  g_print ("Created caps: %s\n", string);
  //g_assert (GST_IS_CAPS (caps));
  if (!gst_dp_packet_from_caps (caps, 0, &header_length, &header, &payload)) {
    g_print ("Could not create packet from caps.");
    exit (1);
  }

  /* validate the packet */
  g_return_val_if_fail (gst_dp_validate_packet (header_length, header, payload),
      FALSE);
  newcaps = gst_dp_caps_from_packet (header_length, header, payload);
  g_return_val_if_fail (newcaps, -1);
  //g_return_val_if_fail (GST_IS_CAPS (newcaps), -1);
  newstring = gst_caps_to_string (newcaps);
  g_print ("Received caps: %s\n", newstring);
  if (strcmp (string, newstring) != 0)
    return -1;
  g_free (string);
  g_free (newstring);

  return 0;
}

static int
event_test ()
{
  GstEvent *send;
  GstEvent *receive;
  guint header_length;
  guint8 *header, *payload;

  g_print ("Testing EOS event at 1s\n");
  send = gst_event_new (GST_EVENT_EOS);
  GST_EVENT_TIMESTAMP (send) = GST_SECOND;
  if (!gst_dp_packet_from_event (send, GST_DP_HEADER_FLAG_CRC, &header_length,
          &header, &payload)) {
    g_warning ("Could not create packet from eos event");
    return 1;
  }
  receive = gst_dp_event_from_packet (header_length, header, payload);

  g_print ("EOS, timestamp %" GST_TIME_FORMAT "\n",
      GST_TIME_ARGS (GST_EVENT_TIMESTAMP (receive)));
  g_assert (GST_EVENT_TYPE (receive) == GST_EVENT_EOS);
  g_assert (GST_EVENT_TIMESTAMP (receive) == GST_SECOND);
  gst_event_unref (send);
  gst_event_unref (receive);

  g_print ("Testing FLUSH event at 2s\n");
  send = gst_event_new (GST_EVENT_FLUSH);
  GST_EVENT_TIMESTAMP (send) = GST_SECOND * 2;
  if (!gst_dp_packet_from_event (send, GST_DP_HEADER_FLAG_CRC, &header_length,
          &header, &payload)) {
    g_warning ("Could not create packet from flush event");
    return 1;
  }
  receive = gst_dp_event_from_packet (header_length, header, payload);

  g_print ("Flush, timestamp %" GST_TIME_FORMAT "\n",
      GST_TIME_ARGS (GST_EVENT_TIMESTAMP (receive)));
  g_assert (GST_EVENT_TYPE (receive) == GST_EVENT_FLUSH);
  g_assert (GST_EVENT_TIMESTAMP (receive) == GST_SECOND * 2);
  gst_event_unref (send);
  gst_event_unref (receive);

  g_print ("Testing SEEK event with 1 second at 3 seconds\n");
  send = gst_event_new_seek (GST_FORMAT_TIME, GST_SECOND);
  GST_EVENT_TIMESTAMP (send) = GST_SECOND * 3;
  if (!gst_dp_packet_from_event (send, GST_DP_HEADER_FLAG_CRC, &header_length,
          &header, &payload)) {
    g_warning ("Could not create packet from seek event");
    return 1;
  }
  receive = gst_dp_event_from_packet (header_length, header, payload);

  g_print ("Seek, timestamp %" GST_TIME_FORMAT ", to %" GST_TIME_FORMAT "\n",
      GST_TIME_ARGS (GST_EVENT_TIMESTAMP (receive)),
      GST_TIME_ARGS (GST_EVENT_SEEK_OFFSET (receive)));
  g_assert (GST_EVENT_TYPE (receive) == GST_EVENT_SEEK);
  g_assert (GST_EVENT_TIMESTAMP (receive) == GST_SECOND * 3);
  g_assert (GST_EVENT_SEEK_FORMAT (receive) == GST_FORMAT_TIME);
  g_assert (GST_EVENT_SEEK_OFFSET (receive) == GST_SECOND);
  gst_event_unref (send);
  gst_event_unref (receive);


  return 0;
}

int
main (int argc, char *argv[])
{
  int ret;

  gst_init (&argc, &argv);
  gst_dp_init ();

  g_print ("\nconversion test\n\n");
  ret = conversion_test ();
  if (ret != 0)
    return ret;

  g_print ("\nbuffer test\n\n");
  ret = buffer_test ();
  if (ret != 0)
    return ret;

  g_print ("\ncaps test\n\n");
  ret = caps_test ();
  if (ret != 0)
    return ret;

  g_print ("\nevent test\n\n");
  ret = event_test ();
  if (ret != 0)
    return ret;


  g_print ("\nall tests worked.\n\n");
  return 0;
}
