/* GStreamer RTP JPEG 2000 depayloader unit test
 *
 * Copyright (C) 2026 GStreamer project
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
#include <gst/rtp/gstrtpbuffer.h>

/* RFC 5371 reuses the 8 byte main header of RTP/JPEG. The type specific field
 * carries tp, MHF, mh_id and T in the first byte, a priority in the second one
 * and the tile number in the last two, followed by the type and the 24 bit
 * fragment offset.
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |tp |MHF|mh_id|T|   priority  |            tile number          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      type     |             fragment offset                   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
#define J2K_HEADER_SIZE 8

#define MHF_NONE  0x00
#define MHF_START 0x10
#define MHF_END   0x30
/* the flag that ends a payload unit */
#define MHF_PU_END 0x20
#define MH_ID     3

/* the type that starts a tile part inside a tile */
#define SOP_MARKER "\xff\x91"

/* the depayloader's built-in robustness bound, and the payload size of a
 * packet */
#define MAX_FRAGMENTATION_UNIT_SIZE (32 * 1024 * 1024)
#define FRAG_SIZE 512

#define RTP_CAPS_STR                                                      \
  "application/x-rtp,media=video,clock-rate=90000,"                       \
  "encoding-name=JPEG2000,sampling=GRAYSCALE"

/* a main header: SOC and a SIZ marker segment */
static const guint8 j2k_main_header[] = {
  0xff, 0x4f, 0xff, 0x51, 0x00, 0x06, 0x00, 0x00, 0x01
};

/* start of a tile: Psot 0 means the tile runs to the end of the codestream */
static const guint8 j2k_sot[] = {
  0xff, 0x90, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
};

static const guint8 j2k_eoc[] = { 0xff, 0xd9 };

static GstBuffer *
create_rtp_packet_id (const guint8 * data, gsize size, guint8 mh_flags,
    guint mh_id, guint16 tile, guint32 frag_offset, guint ts, guint seq,
    gboolean marker)
{
  GstBuffer *buffer;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint8 *payload;

  buffer = gst_rtp_buffer_new_allocate (size + J2K_HEADER_SIZE, 0, 0);
  gst_rtp_buffer_map (buffer, GST_MAP_WRITE, &rtp);
  gst_rtp_buffer_set_seq (&rtp, seq);
  gst_rtp_buffer_set_timestamp (&rtp, ts);
  gst_rtp_buffer_set_marker (&rtp, marker);

  payload = gst_rtp_buffer_get_payload (&rtp);
  payload[0] = mh_flags | (mh_id << 1);
  payload[1] = 0;               /* priority */
  GST_WRITE_UINT16_BE (&payload[2], tile);
  payload[4] = 0;               /* type */
  GST_WRITE_UINT24_BE (&payload[5], frag_offset);
  memcpy (&payload[J2K_HEADER_SIZE], data, size);

  gst_rtp_buffer_unmap (&rtp);

  return buffer;
}

static GstBuffer *
create_rtp_packet (const guint8 * data, gsize size, guint8 mh_flags,
    guint16 tile, guint32 frag_offset, guint ts, guint seq, gboolean marker)
{
  return create_rtp_packet_id (data, size, mh_flags, MH_ID, tile, frag_offset,
      ts, seq, marker);
}

static GstHarness *
setup_depayloader (void)
{
  GstHarness *h = gst_harness_new ("rtpj2kdepay");

  gst_harness_set_src_caps_str (h, RTP_CAPS_STR);

  return h;
}

/* A frame needs a main header, sent as a payload unit of its own that starts
 * with the first fragment and is closed by the last one. Only the first
 * fragment may start with the SOC marker, it would else be taken for the start
 * of a new frame. */
static void
push_main_header (GstHarness * h, guint ts, guint seq)
{
  guint start_size = 4;

  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          create_rtp_packet (j2k_main_header, start_size, MHF_START, 0, 0, ts,
              seq, FALSE)));
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          create_rtp_packet (j2k_main_header + start_size,
              sizeof (j2k_main_header) - start_size, MHF_END, 0, start_size, ts,
              seq + 1, FALSE)));
}

/* A tile starts with its SOT marker segment and ends with EOC. The fragment
 * offset counts up over the whole frame. */
static guint
push_tile (GstHarness * h, guint16 tile, guint32 offset, guint ts, guint seq,
    guint num_frags, gboolean marker)
{
  guint8 frag[FRAG_SIZE];
  guint i;

  for (i = 0; i < num_frags; i++) {
    gboolean last = i == num_frags - 1;

    memset (frag, 0xaa, sizeof (frag));
    if (i == 0)
      memcpy (frag, j2k_sot, sizeof (j2k_sot));
    if (last)
      memcpy (frag + FRAG_SIZE - sizeof (j2k_eoc), j2k_eoc, sizeof (j2k_eoc));

    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
            create_rtp_packet (frag, FRAG_SIZE, MHF_NONE, tile,
                offset + i * FRAG_SIZE, ts, seq, last && marker)));
    seq++;
  }

  return seq;
}

GST_START_TEST (test_rtpj2kdepay_single_packet_frame)
{
  GstHarness *h = setup_depayloader ();
  guint8 frame[sizeof (j2k_sot) + 4 + sizeof (j2k_eoc)];
  GstBuffer *buffer;

  memcpy (frame, j2k_sot, sizeof (j2k_sot));
  memset (frame + sizeof (j2k_sot), 0xaa, 4);
  memcpy (frame + sizeof (j2k_sot) + 4, j2k_eoc, sizeof (j2k_eoc));

  push_main_header (h, 0, 0);

  buffer = create_rtp_packet (frame, sizeof (frame), MHF_NONE, 0,
      sizeof (j2k_main_header), 0, 2, TRUE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buffer));

  fail_unless_equals_int (1, gst_harness_buffers_received (h));

  buffer = gst_harness_pull (h);
  fail_unless_equals_int (gst_buffer_get_size (buffer),
      sizeof (j2k_main_header) + sizeof (frame));
  gst_buffer_unref (buffer);

  gst_harness_teardown (h);
}

GST_END_TEST;

/* Tiles are assembled into their frame, which is pushed when the marker of the
 * last packet of the frame ends it. A sender that keeps sending tiles for a
 * frame that never ends must not grow that frame beyond the
 * depayloader's bound. */
GST_START_TEST (test_rtpj2kdepay_tile_frame_size_limit)
{
#define NUM_COMPLETE_FRAMES 3
#define TILES_PER_FRAME (1 + NUM_COMPLETE_FRAMES)
  GstHarness *h = setup_depayloader ();
  guint frame, seq = 0;

  /* frames within the limit come through */
  for (frame = 0; frame < NUM_COMPLETE_FRAMES; frame++) {
    guint ts = frame * 3000;

    push_main_header (h, ts, seq);
    seq = push_tile (h, 1, sizeof (j2k_main_header), ts, seq + 2,
        MAX_FRAGMENTATION_UNIT_SIZE / 2 / FRAG_SIZE, TRUE);

    fail_unless_equals_int (frame + 1, gst_harness_buffers_received (h));
  }

  /* one frame bigger than the limit, its tiles are dropped instead of being
   * assembled, so nothing is pushed, not even its closing packet */
  push_main_header (h, 100000, seq);
  seq += 2;
  for (frame = 0; frame < TILES_PER_FRAME; frame++) {
    seq = push_tile (h, 1 + frame,
        sizeof (j2k_main_header) + frame * MAX_FRAGMENTATION_UNIT_SIZE, 100000,
        seq, MAX_FRAGMENTATION_UNIT_SIZE / FRAG_SIZE,
        frame == TILES_PER_FRAME - 1);

    fail_unless_equals_int (NUM_COMPLETE_FRAMES,
        gst_harness_buffers_received (h));
  }

  gst_harness_teardown (h);
#undef NUM_COMPLETE_FRAMES
#undef TILES_PER_FRAME
}

GST_END_TEST;

/* Fragments of a main header are kept until the last one of the header is
 * seen. A sender that starts a header and never ends it must not grow the
 * payload unit beyond the depayloader's bound. */
GST_START_TEST (test_rtpj2kdepay_main_header_size_limit)
{
#define NUM_FRAGS 40
  GstHarness *h = setup_depayloader ();
  guint8 frag_start[FRAG_SIZE], frag_cont[FRAG_SIZE];
  guint i;

  memset (frag_start, 0xaa, sizeof (frag_start));
  memcpy (frag_start, j2k_main_header, sizeof (j2k_main_header));
  memset (frag_cont, 0xaa, sizeof (frag_cont));

  /* a header that is started but never ends, nothing to flush the frame. Only
   * the first fragment may start with the SOC marker. */
  for (i = 0; i < NUM_FRAGS; i++) {
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
            create_rtp_packet (i ? frag_cont : frag_start, FRAG_SIZE,
                i ? MHF_NONE : MHF_START, 0, i * FRAG_SIZE, 0, i, FALSE)));

    fail_unless_equals_int (0, gst_harness_buffers_received (h));
  }

  /* the next frame is assembled as usual */
  push_main_header (h, 100000, NUM_FRAGS);
  push_tile (h, 1, sizeof (j2k_main_header), 100000, NUM_FRAGS + 2,
      MAX_FRAGMENTATION_UNIT_SIZE / 2 / FRAG_SIZE, TRUE);

  fail_unless_equals_int (1, gst_harness_buffers_received (h));

  gst_harness_teardown (h);
#undef NUM_FRAGS
}

GST_END_TEST;

/* Fragments of a main header are kept until the last one carries MHF & 2.
 * A sender dribbling header fragments that never end it must not produce
 * output or grow the depayloader without bound: RFC 5371 fragment offsets
 * are 24 bits wide, so once they stop being contiguous the depayloader
 * drops the payload unit and loses sync until a new SOC. The next frame
 * then depayloads as usual. */
GST_START_TEST (test_rtpj2kdepay_main_header_dribble)
{
#define HEADER_NUM_FRAGS 32770
#define HEADER_FRAG_SIZE 512
  GstHarness *h = setup_depayloader ();
  guint8 frag_start[HEADER_FRAG_SIZE], frag_cont[HEADER_FRAG_SIZE];
  guint8 frame[sizeof (j2k_sot) + 4 + sizeof (j2k_eoc)];
  GstBuffer *buffer;
  guint i, seq = 0;

  memset (frag_start, 0xaa, sizeof (frag_start));
  memcpy (frag_start, j2k_main_header, sizeof (j2k_main_header));
  memset (frag_cont, 0xaa, sizeof (frag_cont));

  /* more than 16 MiB of header fragments, one over the point where the
   * 24 bit fragment offset wraps and the depayloader loses sync; only the
   * first fragment may start with SOC, and none of them produces output */
  for (i = 0; i < HEADER_NUM_FRAGS; i++) {
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
            create_rtp_packet (i ? frag_cont : frag_start, HEADER_FRAG_SIZE,
                i ? MHF_NONE : MHF_START, 0, i * HEADER_FRAG_SIZE, 0, seq++,
                FALSE)));

    fail_unless_equals_int (0, gst_harness_buffers_received (h));
  }

  /* the next frame is assembled as usual */
  push_main_header (h, 100000, seq);
  memcpy (frame, j2k_sot, sizeof (j2k_sot));
  memset (frame + sizeof (j2k_sot), 0xaa, 4);
  memcpy (frame + sizeof (j2k_sot) + 4, j2k_eoc, sizeof (j2k_eoc));
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          create_rtp_packet (frame, sizeof (frame), MHF_NONE, 0,
              sizeof (j2k_main_header), 100000, seq + 2, TRUE)));

  fail_unless_equals_int (1, gst_harness_buffers_received (h));

  buffer = gst_harness_pull (h);
  fail_unless_equals_int (gst_buffer_get_size (buffer),
      sizeof (j2k_main_header) + sizeof (frame));
  gst_buffer_unref (buffer);

  gst_harness_teardown (h);
#undef HEADER_NUM_FRAGS
#undef HEADER_FRAG_SIZE
}

GST_END_TEST;

/* A tile must stay bounded even when the sender keeps re-anchoring the
 * fragment offset chain: a non-contiguous packet clears the pending
 * payload unit and re-anchors the chain at its own offset, and a payload
 * unit opened with SOP on the same tile restores the sync without
 * flushing the tile. The tile adapter is then the only bound on the
 * loop, so dribble payload units and watch it out. */
GST_START_TEST (test_rtpj2kdepay_tile_size_limit)
{
#define PU_BODY_SIZE 65532
#define PU_END_SIZE 4
#define PU_TOTAL_SIZE (PU_BODY_SIZE + PU_END_SIZE)
#define PU_NUM 513
  GstHarness *h = setup_depayloader ();
  guint8 body[PU_BODY_SIZE], end[PU_END_SIZE];
  guint8 frame[sizeof (j2k_sot) + 4 + sizeof (j2k_eoc)];
  guint i, seq = 2;

  push_main_header (h, 0, seq);
  seq += 2;

  /* 513 payload units on one tile, opened with SOT and SOP: the first
   * packet of every unit jumps the fragment offset, and the last packet
   * of each unit, contiguous with the first, ends the payload unit and
   * appends it to the tile; the last unit passes the bound, the tile is
   * dropped and nothing is pushed */
  memset (body, 0xaa, sizeof (body));
  memset (end, 0xbb, sizeof (end));
  memcpy (body, j2k_sot, sizeof (j2k_sot));
  for (i = 0; i < PU_NUM; i++) {
    guint32 base = i % 2 ? 0x800000u : 0x100000u;

    if (i > 0)
      memcpy (body, SOP_MARKER, 2);

    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
            create_rtp_packet (body, PU_BODY_SIZE, MHF_NONE, 1, base, 0,
                seq++, FALSE)));
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
            create_rtp_packet (end, PU_END_SIZE, MHF_PU_END, 1,
                base + PU_BODY_SIZE, 0, seq++, FALSE)));

    fail_unless_equals_int (0, gst_harness_buffers_received (h));
  }

  /* the next frame is assembled as usual */
  push_main_header (h, 100000, seq);
  memcpy (frame, j2k_sot, sizeof (j2k_sot));
  memset (frame + sizeof (j2k_sot), 0xaa, 4);
  memcpy (frame + sizeof (j2k_sot) + 4, j2k_eoc, sizeof (j2k_eoc));
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          create_rtp_packet (frame, sizeof (frame), MHF_NONE, 0,
              sizeof (j2k_main_header), 100000, seq + 2, TRUE)));

  fail_unless_equals_int (1, gst_harness_buffers_received (h));
  gst_harness_teardown (h);
#undef PU_BODY_SIZE
#undef PU_END_SIZE
#undef PU_TOTAL_SIZE
#undef PU_NUM
}

GST_END_TEST;

/* A frame of exactly the limit bytes is not over it: the checks are
 * strict, so it completes and is pushed whole. This pins the boundary
 * against an accidental >= comparison.
 *
 * The frame is a big main header and a tile of whole payload units. The
 * offsets are 24 bits wide, so the units jump between two bases, each
 * ending with its end flag so the jump never drops pending data. The
 * last unit ends with the codestream marker. */
GST_START_TEST (test_rtpj2kdepay_frame_at_limit)
{
#define PU_NUM 511
#define PU_BODY_SIZE 65532
#define PU_END_SIZE 4
#define HEADER_SIZE \
  (MAX_FRAGMENTATION_UNIT_SIZE - PU_NUM * (PU_BODY_SIZE + PU_END_SIZE))
#define HEADER_FRAGS (HEADER_SIZE / FRAG_SIZE)
  GstHarness *h = setup_depayloader ();
  guint8 body[PU_BODY_SIZE], end[PU_END_SIZE];
  guint8 frag_start[FRAG_SIZE], frag_cont[FRAG_SIZE];
  const guint8 end_last[PU_END_SIZE] = { 0xbb, 0xbb, 0xff, 0xd9 };
  GstBuffer *buffer;
  guint i, seq = 0;

  /* a main header of 64 KiB, ended by its last fragment */
  memset (frag_start, 0xaa, sizeof (frag_start));
  memcpy (frag_start, j2k_main_header, sizeof (j2k_main_header));
  memset (frag_cont, 0xaa, sizeof (frag_cont));
  for (i = 0; i < HEADER_FRAGS; i++) {
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
            create_rtp_packet (i ? frag_cont : frag_start, FRAG_SIZE,
                i == HEADER_FRAGS - 1 ? MHF_END : (i ? MHF_NONE : MHF_START),
                0, i * FRAG_SIZE, 0, seq++, FALSE)));
  }

  /* the tile: payload units of 64 KiB on one tile, opened with SOT, the
   * last one ending the codestream and the frame */
  memset (body, 0xaa, sizeof (body));
  memset (end, 0xbb, sizeof (end));
  memcpy (body, j2k_sot, sizeof (j2k_sot));
  for (i = 0; i < PU_NUM; i++) {
    guint32 base = i % 2 ? 0x800000u : 0x100000u;

    if (i > 0)
      memcpy (body, SOP_MARKER, 2);

    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
            create_rtp_packet (body, PU_BODY_SIZE, MHF_NONE, 1, base, 0,
                seq++, FALSE)));
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
            create_rtp_packet (i == PU_NUM - 1 ? end_last : end, PU_END_SIZE,
                MHF_PU_END, 1, base + PU_BODY_SIZE, 0, seq++,
                i == PU_NUM - 1)));

    /* the marker on the last packet ends the frame */
    fail_unless_equals_int (i == PU_NUM - 1 ? 1 : 0,
        gst_harness_buffers_received (h));
  }

  fail_unless_equals_int (1, gst_harness_buffers_received (h));
  buffer = gst_harness_pull (h);
  fail_unless_equals_int (MAX_FRAGMENTATION_UNIT_SIZE,
      gst_buffer_get_size (buffer));
  gst_buffer_unref (buffer);

  gst_harness_teardown (h);
#undef PU_NUM
#undef PU_BODY_SIZE
#undef PU_END_SIZE
#undef HEADER_SIZE
#undef HEADER_FRAGS
}

GST_END_TEST;

/* One payload unit beyond the frame at the limit: the tile is the limit and
 * stays, but the frame it assembles into is over it and is dropped, and the
 * depayloader accepts the next frame even under another main header id, the
 * frame being gone and the id only reset with a frame that is pushed. */
GST_START_TEST (test_rtpj2kdepay_frame_over_limit)
{
#define PU_NUM 512
#define PU_BODY_SIZE 65532
#define PU_END_SIZE 4
#define HEADER_FRAGS 128
  GstHarness *h = setup_depayloader ();
  guint8 body[PU_BODY_SIZE], end[PU_END_SIZE];
  guint8 frag_start[FRAG_SIZE], frag_cont[FRAG_SIZE];
  guint8 tilebuf[sizeof (j2k_sot) + 4 + sizeof (j2k_eoc)];
  guint i, seq = 0;

  memset (frag_start, 0xaa, sizeof (frag_start));
  memcpy (frag_start, j2k_main_header, sizeof (j2k_main_header));
  memset (frag_cont, 0xaa, sizeof (frag_cont));
  for (i = 0; i < HEADER_FRAGS; i++) {
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
            create_rtp_packet (i ? frag_cont : frag_start, FRAG_SIZE,
                i == HEADER_FRAGS - 1 ? MHF_END : (i ? MHF_NONE : MHF_START),
                0, i * FRAG_SIZE, 0, seq++, FALSE)));
  }

  memset (body, 0xaa, sizeof (body));
  memset (end, 0xbb, sizeof (end));
  memcpy (body, j2k_sot, sizeof (j2k_sot));
  for (i = 0; i < PU_NUM; i++) {
    guint32 base = i % 2 ? 0x800000u : 0x100000u;

    if (i > 0)
      memcpy (body, SOP_MARKER, 2);

    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
            create_rtp_packet (body, PU_BODY_SIZE, MHF_NONE, 1, base, 0,
                seq++, FALSE)));
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
            create_rtp_packet (end, PU_END_SIZE, MHF_PU_END, 1,
                base + PU_BODY_SIZE, 0, seq++, i == PU_NUM - 1)));

    fail_unless_equals_int (0, gst_harness_buffers_received (h));
  }

  /* the next frame under another main header id depayloads */
  memcpy (tilebuf, j2k_sot, sizeof (j2k_sot));
  memset (tilebuf + sizeof (j2k_sot), 0xaa, 4);
  memcpy (tilebuf + sizeof (j2k_sot) + 4, j2k_eoc, sizeof (j2k_eoc));
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          create_rtp_packet_id (j2k_main_header, 4, MHF_START, 4, 0, 0,
              200000, seq, FALSE)));
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          create_rtp_packet_id (j2k_main_header + 4,
              sizeof (j2k_main_header) - 4, MHF_END, 4, 0, 4, 200000, seq + 1,
              FALSE)));
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          create_rtp_packet_id (tilebuf, sizeof (tilebuf), MHF_NONE, 4, 1,
              sizeof (j2k_main_header), 200000, seq + 2, TRUE)));

  fail_unless_equals_int (1, gst_harness_buffers_received (h));

  gst_harness_teardown (h);
#undef PU_NUM
#undef PU_BODY_SIZE
#undef PU_END_SIZE
#undef HEADER_FRAGS
}

GST_END_TEST;


static Suite *
rtpj2k_suite (void)
{
  Suite *s = suite_create ("rtpj2k");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_rtpj2kdepay_single_packet_frame);
  tcase_add_test (tc_chain, test_rtpj2kdepay_tile_frame_size_limit);
  tcase_add_test (tc_chain, test_rtpj2kdepay_main_header_size_limit);
  tcase_add_test (tc_chain, test_rtpj2kdepay_main_header_dribble);
  tcase_add_test (tc_chain, test_rtpj2kdepay_tile_size_limit);
  tcase_add_test (tc_chain, test_rtpj2kdepay_frame_at_limit);
  tcase_add_test (tc_chain, test_rtpj2kdepay_frame_over_limit);

  return s;
}

GST_CHECK_MAIN (rtpj2k);
