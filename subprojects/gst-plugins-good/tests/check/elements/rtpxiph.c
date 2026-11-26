/* GStreamer
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/check.h>
#include <gst/check/gstharness.h>
#include <gst/rtp/gstrtpbuffer.h>

/* A fragmented packet (F != 0) carries a 4 byte header followed by a 2 byte
 * length. A packet that is too short for that must be dropped without
 * touching a NULL subbuffer. */
static void
check_short_fragment (const gchar * element, const gchar * caps,
    guint8 hdr_flags)
{
  GstHarness *h = gst_harness_new (element);
  guint8 pkt[12 + 5] = { 0x80, 0x60, 0x00, 0x01, 0, 0, 0, 0, 0, 0, 0, 0,
    0x01, 0x02, 0x03, hdr_flags, 0x00
  };
  guint len;

  gst_harness_set_src_caps_str (h, caps);

  /* payload of 4 and 5 bytes: header only, header plus half the length */
  for (len = 16; len <= 17; len++) {
    fail_unless_equals_int (GST_FLOW_OK,
        gst_harness_push (h, gst_buffer_new_memdup (pkt, len)));
  }
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));

  gst_harness_teardown (h);
}

GST_START_TEST (test_vorbis_depay_short_fragment)
{
  /* F=1 (start), VDT=1 (config), 1 packet */
  check_short_fragment ("rtpvorbisdepay",
      "application/x-rtp,media=audio,clock-rate=44100,"
      "encoding-name=VORBIS,payload=96", 0x51);
}

GST_END_TEST;

GST_START_TEST (test_theora_depay_short_fragment)
{
  /* F=1 (start), TDT=1 (config), 1 packet */
  check_short_fragment ("rtptheoradepay",
      "application/x-rtp,media=video,clock-rate=90000,"
      "encoding-name=THEORA,payload=96", 0x51);
}

GST_END_TEST;

#define CHUNK_SIZE 512
#define CHUNK_FILL 0xaa

/* One packed header for ident 1, for the configuration= of the sink caps. The
 * depayloaders only pass the decoded headers on to their codec, so their
 * content is opaque here. The length field counts what follows it, including
 * the header count and the two size fields. */
static const guint8 conf_data[] = {
  0x00, 0x00, 0x00, 0x01,       /* one packed header */
  0x00, 0x00, 0x01,             /* ident */
  0x00, 0x0e,                   /* length */
  0x02,                         /* two size fields, three headers */
  0x04, 0x06,                   /* sizes of the first two, 4 for the last */
  0x80, 0x01, 0x02, 0x03,       /* identification header */
  0x03, 0x61, 0x62, 0x63, 0x64, 0x65,   /* comment header */
  0x05, 0x06, 0x07, 0x08        /* setup header */
};

/* The 4 octet payload header of an unfragmented (F=0) data packet carrying one
 * packet. */
#define XIPH_HDR_NONE   0x01
/* Same with F=1 (start), F=2 (continuation) and F=3 (end). */
#define XIPH_HDR_START  0x41
#define XIPH_HDR_CONT   0x81
#define XIPH_HDR_END    0xc1

static GstBuffer *
make_xiph_packet (guint8 hdr, guint16 seq, guint32 ts, gboolean marker,
    const guint8 * data, guint size, gboolean fragmented)
{
  guint payload_size = fragmented ? size + 2 : size;
  GstBuffer *buf;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint8 *payload;

  buf = gst_rtp_buffer_new_allocate (payload_size + 4, 0, 0);
  gst_rtp_buffer_map (buf, GST_MAP_WRITE, &rtp);
  gst_rtp_buffer_set_seq (&rtp, seq);
  gst_rtp_buffer_set_timestamp (&rtp, ts);
  gst_rtp_buffer_set_marker (&rtp, marker);
  payload = gst_rtp_buffer_get_payload (&rtp);
  /* ident 1 */
  payload[0] = 0x00;
  payload[1] = 0x00;
  payload[2] = 0x01;
  payload[3] = hdr;
  if (fragmented) {
    GST_WRITE_UINT16_BE (payload + 4, size);
    payload += 2;
  }
  memcpy (payload + 4, data, size);
  gst_rtp_buffer_unmap (&rtp);

  return buf;
}

static gchar *
caps_with_configuration (const gchar * caps_str)
{
  /* the base64 padding needs the quotes to survive the caps syntax */
  gchar *b64 = (gchar *) g_base64_encode (conf_data, sizeof (conf_data));
  gchar *caps =
      g_strdup_printf ("%s,configuration=(string)\"%s\"", caps_str, b64);

  g_free (b64);

  return caps;
}

static void
check_buffer_content (GstBuffer * out, const guint8 * expected, guint size)
{
  GstMapInfo map;

  fail_unless (out != NULL);
  fail_unless_equals_int ((gint) gst_buffer_get_size (out), size);
  gst_buffer_map (out, &map, GST_MAP_READ);
  fail_unless (memcmp (map.data, expected, size) == 0);
  gst_buffer_unmap (out, &map);
  gst_buffer_unref (out);
}

/* A sender that never completes a fragmented unit must not be able to grow the
 * assembly buffer without bound. */
static void
check_oversized_dropped (const gchar * element, const gchar * caps_str,
    guint limit)
{
  GstHarness *h = gst_harness_new (element);
  GstElement *depay = gst_harness_find_element (h, element);
  GstBus *bus = gst_bus_new ();
  GstMessage *msg;
  GstBuffer *buf;
  guint8 *group, *data, *big;
  gchar *caps;
  guint i, frag, seq, n_over;

  gst_element_set_bus (depay, bus);
  gst_object_unref (depay);

  caps = caps_with_configuration (caps_str);
  gst_harness_set_src_caps_str (h, caps);
  g_free (caps);
  while ((msg = gst_bus_pop (bus)))
    gst_message_unref (msg);

  /* one subpacket of CHUNK_SIZE bytes, prefixed with its length: as the
   * payload of a complete frame, and as the continuation of a fragmented one */
  group = g_malloc (CHUNK_SIZE + 2);
  GST_WRITE_UINT16_BE (group, CHUNK_SIZE);
  memset (group + 2, CHUNK_FILL, CHUNK_SIZE);
  data = group + 2;

  /* the first data packet activates the codebook and pushes its headers along,
   * so get that frame out of the way */
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          make_xiph_packet (XIPH_HDR_NONE, 1, 1000, TRUE, group,
              CHUNK_SIZE + 2, FALSE)));
  while ((buf = gst_harness_try_pull (h)))
    gst_buffer_unref (buf);

  /* a complete frame is one buffer out, and is the reference for the content */
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          make_xiph_packet (XIPH_HDR_NONE, 2, 2000, TRUE, group,
              CHUNK_SIZE + 2, FALSE)));
  fail_unless_equals_int (1, gst_harness_buffers_in_queue (h));
  check_buffer_content (gst_harness_pull (h), data, CHUNK_SIZE);

  /* a frame started here is only ever continued, until its assembly is well
   * over the limit: nothing comes out of any of them, not even the end packet
   * of the same timestamp */
  n_over = limit / CHUNK_SIZE + 2;
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          make_xiph_packet (XIPH_HDR_START, 3, 3000, FALSE, group,
              CHUNK_SIZE + 2, TRUE)));
  for (i = 1; i <= n_over; i++) {
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
            make_xiph_packet (XIPH_HDR_CONT, (guint16) (3 + i), 3000, FALSE,
                data, CHUNK_SIZE, TRUE)));
  }
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          make_xiph_packet (XIPH_HDR_END, (guint16) (4 + n_over), 3000, TRUE,
              group, CHUNK_SIZE + 2, TRUE)));
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));

  /* the next frame flows again */
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          make_xiph_packet (XIPH_HDR_NONE, (guint16) (5 + n_over), 4000, TRUE,
              group, CHUNK_SIZE + 2, FALSE)));
  fail_unless_equals_int (1, gst_harness_buffers_in_queue (h));
  check_buffer_content (gst_harness_pull (h), data, CHUNK_SIZE);

  /* a fragmented frame whose last checked fragment fills the limit exactly
   * still completes: limit / CHUNK_SIZE fragments of CHUNK_SIZE bytes reach
   * the limit at the last continuation, and the unchecked end packet adds its
   * own chunk */
  frag = limit / CHUNK_SIZE;
  seq = 16 + n_over;
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          make_xiph_packet (XIPH_HDR_START, (guint16) seq, 5000, FALSE, group,
              CHUNK_SIZE, TRUE)));
  for (i = 1; i < frag; i++) {
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
            make_xiph_packet (XIPH_HDR_CONT, (guint16) (seq + i), 5000, FALSE,
                data, CHUNK_SIZE, TRUE)));
  }
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          make_xiph_packet (XIPH_HDR_END, (guint16) (seq + frag), 5000, TRUE,
              data, CHUNK_SIZE, TRUE)));
  fail_unless_equals_int (1, gst_harness_buffers_in_queue (h));
  buf = gst_harness_pull (h);
  fail_unless_equals_int ((gint) gst_buffer_get_size (buf),
      (frag + 1) * CHUNK_SIZE);
  gst_buffer_unref (buf);

  /* an unfragmented packet is never assembled, so no limit applies: this one
   * is as large as the format's 16-bit sub-packet length field allows and
   * still goes out */
  big = g_malloc (65535 + 2);
  GST_WRITE_UINT16_BE (big, 65535);
  memset (big + 2, CHUNK_FILL, 65535);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          make_xiph_packet (XIPH_HDR_NONE, (guint16) (seq + frag + 1), 6000,
              TRUE, big, 65535 + 2, FALSE)));
  fail_unless_equals_int (1, gst_harness_buffers_in_queue (h));
  check_buffer_content (gst_harness_pull (h), big + 2, 65535);

  g_free (big);
  g_free (group);
  gst_harness_teardown (h);
  while ((msg = gst_bus_pop (bus)))
    gst_message_unref (msg);
  gst_object_unref (bus);
}

GST_START_TEST (test_theora_oversized_dropped)
{
  check_oversized_dropped ("rtptheoradepay",
      "application/x-rtp,media=video,clock-rate=90000,"
      "encoding-name=THEORA,payload=96", 32 * 1024 * 1024);
}

GST_END_TEST;

GST_START_TEST (test_vorbis_oversized_dropped)
{
  check_oversized_dropped ("rtpvorbisdepay",
      "application/x-rtp,media=audio,clock-rate=44100,"
      "encoding-name=VORBIS,payload=96", 64 * 1024);
}

GST_END_TEST;

static Suite *
rtpxiph_suite (void)
{
  Suite *s = suite_create ("rtpxiph");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_vorbis_depay_short_fragment);
  tcase_add_test (tc_chain, test_theora_depay_short_fragment);
  tcase_add_test (tc_chain, test_theora_oversized_dropped);
  tcase_add_test (tc_chain, test_vorbis_oversized_dropped);

  return s;
}

GST_CHECK_MAIN (rtpxiph);
