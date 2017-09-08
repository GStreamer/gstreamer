/* GStreamer
 *
 * Copyright (C) 2016 Pexip AS
 *   @author Stian Selnes <stian@pexip.com>
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

#define RTP_VP8_CAPS_STR \
  "application/x-rtp,media=video,encoding-name=VP8,clock-rate=90000,payload=96"

#define gst_buffer_new_from_array(array) gst_buffer_new_wrapped (       \
      g_memdup (vp8_bitstream_payload, sizeof (vp8_bitstream_payload)), \
      sizeof (vp8_bitstream_payload))

static void
add_vp8_meta (GstBuffer * buffer, gboolean use_temporal_scaling,
    gboolean layer_sync, guint layer_id, guint tl0picidx)
{
  GstCustomMeta *meta;
  GstStructure *s;

  meta = gst_buffer_add_custom_meta (buffer, "GstVP8Meta");
  fail_unless (meta != NULL);
  s = gst_custom_meta_get_structure (meta);
  gst_structure_set (s,
      "use-temporal-scaling", G_TYPE_BOOLEAN, use_temporal_scaling,
      "layer-sync", G_TYPE_BOOLEAN, layer_sync,
      "layer-id", G_TYPE_UINT, layer_id,
      "tl0picidx", G_TYPE_UINT, tl0picidx, NULL);
}

/* PictureID emum is not exported */
enum PictureID
{
  VP8_PAY_NO_PICTURE_ID = 0,
  VP8_PAY_PICTURE_ID_7BITS = 1,
  VP8_PAY_PICTURE_ID_15BITS = 2,
};

static const struct no_meta_test_data
{
  /* control inputs */
  enum PictureID pid;           /* picture ID type of test */
  gboolean vp8_payload_header_m_flag;

  /* expected outputs */
  guint vp8_payload_header_size;
  guint vp8_payload_control_value;
} no_meta_test_data[] = {
  {
  VP8_PAY_NO_PICTURE_ID, FALSE, 1, 0x10},       /* no picture ID single byte header, S set */
  {
  VP8_PAY_PICTURE_ID_7BITS, FALSE, 3, 0x90},    /* X bit to allow for I bit means header is three bytes, S and X set */
  {
  VP8_PAY_PICTURE_ID_15BITS, TRUE, 4, 0x90},    /* X bit to allow for I bit with M bit means header is four bytes, S, X and M set */
      /* repeated with non reference frame */
  {
  VP8_PAY_NO_PICTURE_ID, FALSE, 1, 0x30},       /* no picture ID single byte header, S set */
  {
  VP8_PAY_PICTURE_ID_7BITS, FALSE, 3, 0xB0},    /* X bit to allow for I bit means header is three bytes, S and X set */
  {
  VP8_PAY_PICTURE_ID_15BITS, TRUE, 4, 0xB0},    /* X bit to allow for I bit with M bit means header is four bytes, S, X and M set */
};

GST_START_TEST (test_pay_no_meta)
{
  guint8 vp8_bitstream_payload[] = {
    0x30, 0x00, 0x00, 0x9d, 0x01, 0x2a, 0xb0, 0x00, 0x90, 0x00, 0x06, 0x47,
    0x08, 0x85, 0x85, 0x88, 0x99, 0x84, 0x88, 0x21, 0x00
  };
  const struct no_meta_test_data *test_data = &no_meta_test_data[__i__];
  GstBuffer *buffer;
  GstMapInfo map = GST_MAP_INFO_INIT;
  GstHarness *h = gst_harness_new ("rtpvp8pay");
  gst_harness_set_src_caps_str (h, "video/x-vp8");

  /* check unknown picture id enum value */
  fail_unless (test_data->pid <= VP8_PAY_PICTURE_ID_15BITS);

  g_object_set (h->element, "picture-id-mode", test_data->pid,
      "picture-id-offset", 0x5A5A, NULL);

  buffer = gst_buffer_new_wrapped (g_memdup (vp8_bitstream_payload,
          sizeof (vp8_bitstream_payload)), sizeof (vp8_bitstream_payload));

  /* set droppable if N flag set */
  if ((test_data->vp8_payload_control_value & 0x20) != 0) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DROPPABLE);
  }

  buffer = gst_harness_push_and_pull (h, buffer);

  fail_unless (gst_buffer_map (buffer, &map, GST_MAP_READ));
  fail_unless (map.data != NULL);

  /* check buffer size and content */
  fail_unless_equals_int (map.size,
      12 + test_data->vp8_payload_header_size + sizeof (vp8_bitstream_payload));

  fail_unless_equals_int (test_data->vp8_payload_control_value, map.data[12]);

  if (test_data->vp8_payload_header_size > 2) {
    /* vp8 header extension byte must have I set */
    fail_unless_equals_int (0x80, map.data[13]);
    /* check picture id */
    if (test_data->pid == VP8_PAY_PICTURE_ID_7BITS) {
      fail_unless_equals_int (0x5a, map.data[14]);
    } else if (test_data->pid == VP8_PAY_PICTURE_ID_15BITS) {
      fail_unless_equals_int (0xDA, map.data[14]);
      fail_unless_equals_int (0x5A, map.data[15]);
    }
  }

  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  gst_harness_teardown (h);
}

GST_END_TEST;

static const struct with_meta_test_data
{
  /* control inputs */
  enum PictureID pid;           /* picture ID type of test */
  gboolean vp8_payload_header_m_flag;
  gboolean use_temporal_scaling;
  gboolean y_flag;

  /* expected outputs */
  guint vp8_payload_header_size;
  guint vp8_payload_control_value;
  guint vp8_payload_extended_value;
} with_meta_test_data[] = {
  {
  VP8_PAY_NO_PICTURE_ID, FALSE, FALSE, FALSE, 1, 0x10, 0x80},   /* no picture ID single byte header, S set */
  {
  VP8_PAY_PICTURE_ID_7BITS, FALSE, FALSE, FALSE, 3, 0x90, 0x80},        /* X bit to allow for I bit means header is three bytes, S and X set */
  {
  VP8_PAY_PICTURE_ID_15BITS, TRUE, FALSE, FALSE, 4, 0x90, 0x80},        /* X bit to allow for I bit with M bit means header is four bytes, S, X and M set */
  {
  VP8_PAY_NO_PICTURE_ID, FALSE, TRUE, FALSE, 4, 0x90, 0x60},    /* no picture ID single byte header, S set */
  {
  VP8_PAY_PICTURE_ID_7BITS, FALSE, TRUE, FALSE, 5, 0x90, 0xE0}, /* X bit to allow for I bit means header is three bytes, S and X set */
  {
  VP8_PAY_PICTURE_ID_15BITS, TRUE, TRUE, FALSE, 6, 0x90, 0xE0}, /* X bit to allow for I bit with M bit means header is four bytes, S, X and M set */
  {
  VP8_PAY_NO_PICTURE_ID, FALSE, TRUE, TRUE, 4, 0x90, 0x60},     /* no picture ID single byte header, S set */
  {
  VP8_PAY_PICTURE_ID_7BITS, FALSE, TRUE, TRUE, 5, 0x90, 0xE0},  /* X bit to allow for I bit means header is three bytes, S and X set */
  {
  VP8_PAY_PICTURE_ID_15BITS, TRUE, TRUE, TRUE, 6, 0x90, 0xE0},  /* X bit to allow for I bit with M bit means header is four bytes, S, X and M set */
      /* repeated with non reference frame */
  {
  VP8_PAY_NO_PICTURE_ID, FALSE, FALSE, FALSE, 1, 0x30, 0x80},   /* no picture ID single byte header, S set */
  {
  VP8_PAY_PICTURE_ID_7BITS, FALSE, FALSE, FALSE, 3, 0xB0, 0x80},        /* X bit to allow for I bit means header is three bytes, S and X set */
  {
  VP8_PAY_PICTURE_ID_15BITS, TRUE, FALSE, FALSE, 4, 0xB0, 0x80},        /* X bit to allow for I bit with M bit means header is four bytes, S, X and M set */
  {
  VP8_PAY_NO_PICTURE_ID, FALSE, TRUE, FALSE, 4, 0xB0, 0x60},    /* no picture ID single byte header, S set */
  {
  VP8_PAY_PICTURE_ID_7BITS, FALSE, TRUE, FALSE, 5, 0xB0, 0xE0}, /* X bit to allow for I bit means header is three bytes, S and X set */
  {
  VP8_PAY_PICTURE_ID_15BITS, TRUE, TRUE, FALSE, 6, 0xB0, 0xE0}, /* X bit to allow for I bit with M bit means header is four bytes, S, X and M set */
  {
  VP8_PAY_NO_PICTURE_ID, FALSE, TRUE, TRUE, 4, 0xB0, 0x60},     /* no picture ID single byte header, S set */
  {
  VP8_PAY_PICTURE_ID_7BITS, FALSE, TRUE, TRUE, 5, 0xB0, 0xE0},  /* X bit to allow for I bit means header is three bytes, S and X set */
  {
  VP8_PAY_PICTURE_ID_15BITS, TRUE, TRUE, TRUE, 6, 0xB0, 0xE0},  /* X bit to allow for I bit with M bit means header is four bytes, S, X and M set */
};

GST_START_TEST (test_pay_with_meta)
{
  guint8 vp8_bitstream_payload[] = {
    0x30, 0x00, 0x00, 0x9d, 0x01, 0x2a, 0xb0, 0x00, 0x90, 0x00, 0x06, 0x47,
    0x08, 0x85, 0x85, 0x88, 0x99, 0x84, 0x88, 0x21, 0x00
  };
  const struct with_meta_test_data *test_data = &with_meta_test_data[__i__];
  GstBuffer *buffer;
  GstCustomMeta *meta;
  GstMapInfo map = GST_MAP_INFO_INIT;
  GstHarness *h = gst_harness_new ("rtpvp8pay");
  gst_harness_set_src_caps_str (h, "video/x-vp8");

  /* check for unknown picture id enum value */
  fail_unless (test_data->pid <= VP8_PAY_PICTURE_ID_15BITS);

  g_object_set (h->element, "picture-id-mode", test_data->pid,
      "picture-id-offset", 0x5A5A, NULL);

  /* Push a buffer in */
  buffer = gst_buffer_new_wrapped (g_memdup (vp8_bitstream_payload,
          sizeof (vp8_bitstream_payload)), sizeof (vp8_bitstream_payload));
  add_vp8_meta (buffer, test_data->use_temporal_scaling, test_data->y_flag,
      2, 255);
  /* set droppable if N flag set */
  if ((test_data->vp8_payload_control_value & 0x20) != 0) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DROPPABLE);
  }

  buffer = gst_harness_push_and_pull (h, buffer);

  fail_unless (gst_buffer_map (buffer, &map, GST_MAP_READ));
  fail_unless (map.data != NULL);

  meta = gst_buffer_get_custom_meta (buffer, "GstVP8Meta");
  fail_unless (meta == NULL);

  /* check buffer size and content */
  fail_unless_equals_int (map.size,
      12 + test_data->vp8_payload_header_size + sizeof (vp8_bitstream_payload));
  fail_unless_equals_int (test_data->vp8_payload_control_value, map.data[12]);

  if (test_data->vp8_payload_header_size > 1) {
    int hdridx = 13;
    fail_unless_equals_int (test_data->vp8_payload_extended_value,
        map.data[hdridx++]);

    /* check picture ID */
    if (test_data->pid == VP8_PAY_PICTURE_ID_7BITS) {
      fail_unless_equals_int (0x5A, map.data[hdridx++]);
    } else if (test_data->pid == VP8_PAY_PICTURE_ID_15BITS) {
      fail_unless_equals_int (0xDA, map.data[hdridx++]);
      fail_unless_equals_int (0x5A, map.data[hdridx++]);
    }

    if (test_data->use_temporal_scaling) {
      /* check temporal layer 0 picture ID value */
      fail_unless_equals_int (255, map.data[hdridx++]);
      /* check temporal layer ID value */
      fail_unless_equals_int (2, (map.data[hdridx] >> 6) & 0x3);

      if (test_data->y_flag) {
        fail_unless_equals_int (1, (map.data[hdridx] >> 5) & 1);
      } else {
        fail_unless_equals_int (0, (map.data[hdridx] >> 5) & 1);
      }
    }
  }

  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_pay_continuous_picture_id_and_tl0picidx)
{
  guint8 vp8_bitstream_payload[] = {
    0x30, 0x00, 0x00, 0x9d, 0x01, 0x2a, 0xb0, 0x00, 0x90, 0x00, 0x06, 0x47,
    0x08, 0x85, 0x85, 0x88, 0x99, 0x84, 0x88, 0x21, 0x00
  };
  GstHarness *h = gst_harness_new ("rtpvp8pay");
  const gint header_len_without_tl0picidx = 3;
  const gint header_len_with_tl0picidx = 5;
  const gint packet_len_without_tl0picidx = 12 + header_len_without_tl0picidx +
      sizeof (vp8_bitstream_payload);
  const gint packet_len_with_tl0picidx = 12 + header_len_with_tl0picidx +
      sizeof (vp8_bitstream_payload);
  const gint picid_offset = 14;
  const gint tl0picidx_offset = 15;
  GstBuffer *buffer;
  GstMapInfo map;

  g_object_set (h->element, "picture-id-mode", VP8_PAY_PICTURE_ID_7BITS,
      "picture-id-offset", 0, NULL);
  gst_harness_set_src_caps_str (h, "video/x-vp8");

  /* First, push a frame without temporal scalability meta */
  buffer = gst_buffer_new_from_array (vp8_bitstream_payload);
  buffer = gst_harness_push_and_pull (h, buffer);
  fail_unless (gst_buffer_map (buffer, &map, GST_MAP_READ));
  fail_unless_equals_int (map.size, packet_len_without_tl0picidx);
  fail_unless_equals_int (map.data[picid_offset], 0x00);
  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  /* Push a frame for temporal layer 0 with meta */
  buffer = gst_buffer_new_from_array (vp8_bitstream_payload);
  add_vp8_meta (buffer, TRUE, TRUE, 0, 0);

  buffer = gst_harness_push_and_pull (h, buffer);
  fail_unless (gst_buffer_map (buffer, &map, GST_MAP_READ));
  fail_unless_equals_int (map.size, packet_len_with_tl0picidx);
  fail_unless_equals_int (map.data[picid_offset], 0x01);
  fail_unless_equals_int (map.data[tl0picidx_offset], 0x00);
  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  /* Push a frame for temporal layer 1 with meta */
  buffer = gst_buffer_new_from_array (vp8_bitstream_payload);
  add_vp8_meta (buffer, TRUE, TRUE, 1, 0);
  buffer = gst_harness_push_and_pull (h, buffer);
  fail_unless (gst_buffer_map (buffer, &map, GST_MAP_READ));
  fail_unless_equals_int (map.size, packet_len_with_tl0picidx);
  fail_unless_equals_int (map.data[picid_offset], 0x02);
  fail_unless_equals_int (map.data[tl0picidx_offset], 0x00);
  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  /* Push next frame for temporal layer 0 with meta */
  buffer = gst_buffer_new_from_array (vp8_bitstream_payload);
  add_vp8_meta (buffer, TRUE, TRUE, 0, 1);
  buffer = gst_harness_push_and_pull (h, buffer);
  fail_unless (gst_buffer_map (buffer, &map, GST_MAP_READ));
  fail_unless_equals_int (map.size, packet_len_with_tl0picidx);
  fail_unless_equals_int (map.data[picid_offset], 0x03);
  fail_unless_equals_int (map.data[tl0picidx_offset], 0x01);
  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  /* Another frame for temporal layer 0, but now the meta->tl0picidx has been
   * reset to 0 (simulating an encoder reset). Payload must ensure tl0picidx
   * is increasing. */
  buffer = gst_buffer_new_from_array (vp8_bitstream_payload);
  add_vp8_meta (buffer, TRUE, TRUE, 0, 0);
  buffer = gst_harness_push_and_pull (h, buffer);
  fail_unless (gst_buffer_map (buffer, &map, GST_MAP_READ));
  fail_unless_equals_int (map.size, packet_len_with_tl0picidx);
  fail_unless_equals_int (map.data[picid_offset], 0x04);
  fail_unless_equals_int (map.data[tl0picidx_offset], 0x02);
  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  /* If we receive a frame without meta, we should continue to increase and
   * add tl0picidx (assuming TID=0) in order to maximize interop. */
  buffer = gst_buffer_new_from_array (vp8_bitstream_payload);
  buffer = gst_harness_push_and_pull (h, buffer);
  fail_unless (gst_buffer_map (buffer, &map, GST_MAP_READ));
  fail_unless_equals_int (map.size, packet_len_with_tl0picidx);
  fail_unless_equals_int (map.data[picid_offset], 0x05);
  fail_unless_equals_int (map.data[tl0picidx_offset], 0x03);
  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_pay_tl0picidx_split_buffer)
{
  guint8 vp8_bitstream_payload[] = {
    0x30, 0x00, 0x00, 0x9d, 0x01, 0x2a, 0xb0, 0x00, 0x90, 0x00, 0x06, 0x47,
    0x08, 0x85, 0x85, 0x88, 0x99, 0x84, 0x88, 0x21, 0x00
  };
  GstHarness *h =
      gst_harness_new_parse
      ("rtpvp8pay mtu=28 picture-id-mode=1 picture-id-offset=0");
  const gint header_len = 12 + 5;       /* RTP + VP8 payload header */
  const gint picid_offset = 14;
  const gint tl0picidx_offset = 15;
  guint output_bytes_left;
  GstBuffer *buffer;
  GstMapInfo map;

  gst_harness_set_src_caps_str (h, "video/x-vp8");

  /* Push a frame for temporal layer 0 with meta */
  buffer = gst_buffer_new_from_array (vp8_bitstream_payload);
  add_vp8_meta (buffer, TRUE, TRUE, 0, 0);
  gst_harness_push (h, buffer);

  /* Expect it to be split into multiple buffers to fit the MTU */
  output_bytes_left = sizeof (vp8_bitstream_payload);
  while (output_bytes_left > 0) {
    const gint expected = MIN (output_bytes_left, 28 - header_len);
    const gint packet_len = header_len + expected;
    output_bytes_left -= expected;

    buffer = gst_harness_pull (h);
    fail_unless (gst_buffer_map (buffer, &map, GST_MAP_READ));
    fail_unless_equals_int (map.size, packet_len);
    fail_unless_equals_int (map.data[picid_offset], 0x00);
    fail_unless_equals_int (map.data[tl0picidx_offset], 0x00);
    gst_buffer_unmap (buffer, &map);
    gst_buffer_unref (buffer);
  }

  /* Push a frame for temporal layer 1 with meta */
  buffer = gst_buffer_new_from_array (vp8_bitstream_payload);
  add_vp8_meta (buffer, TRUE, TRUE, 1, 0);
  gst_harness_push (h, buffer);

  /* Expect it to be split into multiple buffers to fit the MTU */
  output_bytes_left = sizeof (vp8_bitstream_payload);
  while (output_bytes_left > 0) {
    const gint expected = MIN (output_bytes_left, 28 - header_len);
    const gint packet_len = header_len + expected;
    output_bytes_left -= expected;

    buffer = gst_harness_pull (h);
    fail_unless (gst_buffer_map (buffer, &map, GST_MAP_READ));
    fail_unless_equals_int (map.size, packet_len);
    fail_unless_equals_int (map.data[picid_offset], 0x01);
    fail_unless_equals_int (map.data[tl0picidx_offset], 0x00);
    gst_buffer_unmap (buffer, &map);
    gst_buffer_unref (buffer);
  }

  /* Push another frame for temporal layer 0 with meta */
  buffer = gst_buffer_new_from_array (vp8_bitstream_payload);
  add_vp8_meta (buffer, TRUE, TRUE, 0, 0);
  gst_harness_push (h, buffer);

  /* Expect it to be split into multiple buffers to fit the MTU */
  output_bytes_left = sizeof (vp8_bitstream_payload);
  while (output_bytes_left > 0) {
    const gint expected = MIN (output_bytes_left, 28 - header_len);
    const gint packet_len = header_len + expected;
    output_bytes_left -= expected;

    buffer = gst_harness_pull (h);
    fail_unless (gst_buffer_map (buffer, &map, GST_MAP_READ));
    fail_unless_equals_int (map.size, packet_len);
    fail_unless_equals_int (map.data[picid_offset], 0x02);
    fail_unless_equals_int (map.data[tl0picidx_offset], 0x01);
    gst_buffer_unmap (buffer, &map);
    gst_buffer_unref (buffer);
  }

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
rtpvp8_suite (void)
{
  Suite *s = suite_create ("rtpvp8");
  TCase *tc_chain;
  static const gchar *tags[] = { NULL };

  /* Register custom GstVP8Meta manually */
  gst_meta_register_custom ("GstVP8Meta", tags, NULL, NULL, NULL);

  suite_add_tcase (s, (tc_chain = tcase_create ("vp8pay")));
  tcase_add_loop_test (tc_chain, test_pay_no_meta, 0,
      G_N_ELEMENTS (no_meta_test_data));
  tcase_add_loop_test (tc_chain, test_pay_with_meta, 0,
      G_N_ELEMENTS (with_meta_test_data));
  tcase_add_test (tc_chain, test_pay_continuous_picture_id_and_tl0picidx);
  tcase_add_test (tc_chain, test_pay_tl0picidx_split_buffer);

  return s;
}

GST_CHECK_MAIN (rtpvp8);
