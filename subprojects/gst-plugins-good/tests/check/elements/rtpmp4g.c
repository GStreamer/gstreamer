/* GStreamer
 * Copyright (C) 2026 GStreamer Project
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

#include <gst/check/check.h>
#include <gst/check/gstharness.h>
#include <gst/rtp/gstrtpbuffer.h>

/* RFC 3640 generic mode with sizelength=13, indexlength=3,
 * indexdeltalength=3: each AU header is 16 bits, AU-headers-length is 2
 * bytes, so 2 header bytes precede the AU data of every packet. */
#define RTP_MP4G_CAPS_STR                                                       \
  "application/x-rtp,media=(string)audio,"                                      \
  "encoding-name=(string)MPEG4-GENERIC,clock-rate=(int)44100,payload=(int)96,"  \
  "mode=(string)AAC-hbr,sizelength=(string)13,indexlength=(string)3,"           \
  "indexdeltalength=(string)3"

/* With maxdisplacement the depayloader stops forcing a flush of the reorder
 * queue when the AU-Index stops growing, so skipped AUs stay queued. */
#define RTP_MP4G_DISPLACEMENT_CAPS_STR RTP_MP4G_CAPS_STR                 \
  ",maxdisplacement=(string)8"

/* The built-in bound of the depayloader; the sizelength of the caps above
 * caps a declared AU-size at 8191, so the fragmented AUs dribble 512-byte
 * fragments up to this bound. */
#define MAX_FRAGMENTATION_UNIT_SIZE (1024 * 1024)
#define FRAG_SIZE 512
/* FRAG_SIZE bytes more than the bound put together from 512-byte
 * fragments */
#define OVER_LIMIT_FRAGS (MAX_FRAGMENTATION_UNIT_SIZE / FRAG_SIZE + 1)
/* 512-byte stale AUs that pass the bound in the reorder queue; the last
 * one of them trips it */
#define NUM_STALE_AUS (MAX_FRAGMENTATION_UNIT_SIZE / FRAG_SIZE + 1)

/* Build one RTP packet carrying an AU fragment: 2 bytes of
 * AU-headers-length, one 16-bit AU-header (AU-size in the high 13 bits,
 * AU-Index in the low 3 bits), then size bytes of data. */
static GstBuffer *
make_au_packet (guint au_size, guint au_index, const guint8 * data,
    gsize size, guint32 rtp_ts, guint16 seq, gboolean marker)
{
  GstBuffer *buf = gst_rtp_buffer_new_allocate (4 + size, 0, 0);
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint8 *payload;

  gst_rtp_buffer_map (buf, GST_MAP_WRITE, &rtp);
  gst_rtp_buffer_set_seq (&rtp, seq);
  gst_rtp_buffer_set_timestamp (&rtp, rtp_ts);
  gst_rtp_buffer_set_marker (&rtp, marker);
  payload = gst_rtp_buffer_get_payload (&rtp);
  payload[0] = 0x00;
  payload[1] = 0x10;            /* one AU-header of 16 bits */
  payload[2] = (guint8) ((au_size >> 5) & 0xf8);
  payload[3] = (guint8) ((au_size << 3) | (au_index & 0x7));
  memcpy (payload + 4, data, size);
  gst_rtp_buffer_unmap (&rtp);

  GST_BUFFER_PTS (buf) = 0;
  GST_BUFFER_DTS (buf) = GST_CLOCK_TIME_NONE;

  return buf;
}

GST_START_TEST (test_mp4gdepay_max_fragmentation_unit_size)
{
  GstHarness *h = gst_harness_new ("rtpmp4gdepay");
  GstBuffer *buf;
  guint8 data[FRAG_SIZE];
  guint i;

  memset (data, 0xAB, sizeof (data));

  gst_harness_set_src_caps_str (h, RTP_MP4G_CAPS_STR);

  /* Step 1: a small complete AU passes through */
  buf = make_au_packet (16, 1, data, 16, 1000, 0, TRUE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  fail_unless_equals_int (1, gst_harness_buffers_received (h));
  buf = gst_harness_pull (h);
  fail_unless_equals_int (16, gst_buffer_get_size (buf));
  fail_unless (gst_buffer_memcmp (buf, 0, data, 16) == 0);
  gst_buffer_unref (buf);

  /* Step 2: an AU fragmented past the limit is dropped while incomplete.
   * The depayloader performs a full reset every time it drops, and the
   * packet carrying the marker bit is never checked, so the fragments
   * accumulated since the last drop are pushed out as one AU. */
  buf = make_au_packet (8191, 2, data, 32, 2000, 1, FALSE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));
  /* the accumulator passes the limit at fragment
   * MAX_FRAGMENTATION_UNIT_SIZE / FRAG_SIZE and resets there, so the
   * fragments after the last reset join the unchecked marker packet */
  for (i = 0; i < OVER_LIMIT_FRAGS; i++) {
    buf = make_au_packet (8191, 2, data, FRAG_SIZE, 2000, 2 + i, FALSE);
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  }
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));
  buf = make_au_packet (8191, 2, data, FRAG_SIZE, 2000, 2 + i, TRUE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  fail_unless_equals_int (1, gst_harness_buffers_in_queue (h));
  buf = gst_harness_pull (h);
  /* the fragments left in the accumulator after the last drop plus this
   * unchecked marker fragment, never the fully assembled oversized one */
  fail_unless_equals_int ((OVER_LIMIT_FRAGS %
          (MAX_FRAGMENTATION_UNIT_SIZE / FRAG_SIZE) + 1) * FRAG_SIZE,
      gst_buffer_get_size (buf));
  gst_buffer_unref (buf);

  /* Step 3: the depayloader recovers and depayloads the next AU */
  buf = make_au_packet (16, 3, data, 16, 3000, 2 + OVER_LIMIT_FRAGS + 1, TRUE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  fail_unless_equals_int (1, gst_harness_buffers_in_queue (h));
  buf = gst_harness_pull (h);
  fail_unless_equals_int (16, gst_buffer_get_size (buf));
  fail_unless (gst_buffer_memcmp (buf, 0, data, 16) == 0);
  gst_buffer_unref (buf);

  gst_harness_teardown (h);
}

GST_END_TEST;

/* A complete AU whose AU-Index is not the expected one waits in the reorder
 * queue. A sender that keeps skipping that index must not grow the queue
 * beyond the depayloader's bound. */
GST_START_TEST (test_mp4gdepay_max_reorder_queue)
{
  GstHarness *h = gst_harness_new ("rtpmp4gdepay");
  GstBuffer *buf;
  guint8 data[FRAG_SIZE];
  guint i;

  memset (data, 0xAB, sizeof (data));

  gst_harness_set_src_caps_str (h, RTP_MP4G_DISPLACEMENT_CAPS_STR);

  /* an AU with index 7 is depayloaded; no index below it is expected and
   * with maxdisplacement none of them flushes the reorder queue either */
  buf = make_au_packet (16, 7, data, 16, 1000, 0, TRUE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  fail_unless_equals_int (1, gst_harness_buffers_in_queue (h));
  buf = gst_harness_pull (h);
  fail_unless_equals_int (16, gst_buffer_get_size (buf));
  fail_unless (gst_buffer_memcmp (buf, 0, data, 16) == 0);
  gst_buffer_unref (buf);

  /* AUs that all carry index 6 never match the expected index and pile up.
   * NUM_STALE_AUS of them pass the bound, the last one trips it, the
   * depayloader resets, and nothing is pushed, not even by the packet that
   * trips it. */
  for (i = 0; i < NUM_STALE_AUS; i++) {
    buf = make_au_packet (FRAG_SIZE, 6, data, FRAG_SIZE, 2000 + i, 1 + i, TRUE);
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
    fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));
  }

  /* the reset expects no index in particular, the next AU is depayloaded */
  buf = make_au_packet (16, 4, data, 16, 2000 + NUM_STALE_AUS,
      1 + NUM_STALE_AUS, TRUE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  fail_unless_equals_int (1, gst_harness_buffers_in_queue (h));
  buf = gst_harness_pull (h);
  fail_unless_equals_int (16, gst_buffer_get_size (buf));
  fail_unless (gst_buffer_memcmp (buf, 0, data, 16) == 0);
  gst_buffer_unref (buf);

  gst_harness_teardown (h);
}

GST_END_TEST;

/* A fragmented AU whose last checked fragment fills the limit exactly is
 * not over it: the checks are strict, so the AU must still depayload.
 * This pins the accumulator boundary against an accidental >= comparison,
 * shows a fragment past the limit dropping the AU, and shows the depayer
 * recovering for the next AU. */
GST_START_TEST (test_mp4gdepay_fragment_at_limit)
{
  GstHarness *h = gst_harness_new ("rtpmp4gdepay");
  GstBuffer *buf;
  guint8 data[FRAG_SIZE];
  guint i, seq = 0;

  memset (data, 0xAB, sizeof (data));

  gst_harness_set_src_caps_str (h, RTP_MP4G_CAPS_STR);

  /* as many fragments of 512 bytes as fill the limit exactly: the last
   * one checked reaches the limit; the AU headers in the fragments do not
   * count towards it */
  for (i = 0; i < MAX_FRAGMENTATION_UNIT_SIZE / FRAG_SIZE; i++) {
    buf = make_au_packet (FRAG_SIZE, 1, data, FRAG_SIZE, 2000, seq++, FALSE);
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
    fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));
  }

  /* the closing fragment is never checked and its data joins the AU, so
   * the frame comes out one fragment larger than the limit */
  buf = make_au_packet (FRAG_SIZE, 1, data, FRAG_SIZE, 2000, seq++, TRUE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  fail_unless_equals_int (1, gst_harness_buffers_in_queue (h));
  buf = gst_harness_pull (h);
  fail_unless_equals_int (MAX_FRAGMENTATION_UNIT_SIZE + FRAG_SIZE,
      gst_buffer_get_size (buf));
  fail_unless (gst_buffer_memcmp (buf, 0, data, sizeof (data)) == 0);
  gst_buffer_unref (buf);

  /* a fragment past the limit on a fresh AU trips the check: the AU is
   * dropped and, with the depayloader reset, the next complete AU
   * depayloads */
  for (i = 0; i < OVER_LIMIT_FRAGS; i++) {
    buf = make_au_packet (FRAG_SIZE, 2, data, FRAG_SIZE, 4000, seq++, FALSE);
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
    fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));
  }
  buf = make_au_packet (16, 2, data, 16, 6000, seq, TRUE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  fail_unless_equals_int (1, gst_harness_buffers_in_queue (h));
  buf = gst_harness_pull (h);
  fail_unless_equals_int (16, gst_buffer_get_size (buf));
  fail_unless (gst_buffer_memcmp (buf, 0, data, 16) == 0);
  gst_buffer_unref (buf);

  gst_harness_teardown (h);
}

GST_END_TEST;

/* Stale AUs reaching the limit exactly in the reorder queue must all
 * survive: delivering the missing index flushes the whole queue. This pins
 * the queue boundary against an accidental >= comparison. */
GST_START_TEST (test_mp4gdepay_reorder_at_limit)
{
  GstHarness *h = gst_harness_new ("rtpmp4gdepay");
  GstBuffer *buf;
  guint8 data[FRAG_SIZE];
  guint i;

  memset (data, 0xAB, sizeof (data));

  gst_harness_set_src_caps_str (h, RTP_MP4G_DISPLACEMENT_CAPS_STR);

  /* an AU with index 7 is depayloaded; with maxdisplacement nothing else
   * flushes the reorder queue on its own */
  buf = make_au_packet (16, 7, data, 16, 1000, 0, TRUE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  fail_unless_equals_int (1, gst_harness_buffers_in_queue (h));
  buf = gst_harness_pull (h);
  gst_buffer_unref (buf);

  /* stale AUs of 512 bytes with index 6 fill the queue to exactly the
   * limit and are kept */
  for (i = 0; i < MAX_FRAGMENTATION_UNIT_SIZE / FRAG_SIZE; i++) {
    buf = make_au_packet (FRAG_SIZE, 6, data, FRAG_SIZE, 2000 + i, 1 + i, TRUE);
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
    fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));
  }

  /* a header with index 7 is not behind the largest index seen yet, which
   * flushes the reorder queue: all the stale AUs survive plus this AU
   * itself */
  buf = make_au_packet (16, 7, data, 16, 2000 +
      MAX_FRAGMENTATION_UNIT_SIZE / FRAG_SIZE,
      1 + MAX_FRAGMENTATION_UNIT_SIZE / FRAG_SIZE, TRUE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  fail_unless_equals_int (MAX_FRAGMENTATION_UNIT_SIZE / FRAG_SIZE + 1,
      gst_harness_buffers_in_queue (h));
  for (i = 0; i < MAX_FRAGMENTATION_UNIT_SIZE / FRAG_SIZE + 1; i++) {
    buf = gst_harness_pull (h);
    gst_buffer_unref (buf);
  }

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
rtpmp4g_suite (void)
{
  Suite *s = suite_create ("rtpmp4g");
  TCase *tc_chain;

  suite_add_tcase (s, (tc_chain = tcase_create ("general")));
  tcase_add_test (tc_chain, test_mp4gdepay_max_fragmentation_unit_size);
  tcase_add_test (tc_chain, test_mp4gdepay_fragment_at_limit);
  tcase_add_test (tc_chain, test_mp4gdepay_max_reorder_queue);
  tcase_add_test (tc_chain, test_mp4gdepay_reorder_at_limit);

  return s;
}

GST_CHECK_MAIN (rtpmp4g);
