/* GStreamer
 *
 * Copyright (c) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <gst/check/gstharness.h>
#include <gst/check/gstcheck.h>
#include <gst/video/video.h>

#define gst_caps_new_i420(w, h) gst_caps_new_i420_full (w, h, 30, 1, 1, 1)
static GstCaps *
gst_caps_new_i420_full (gint width, gint height, gint fps_n, gint fps_d,
    gint par_n, gint par_d)
{
  GstVideoInfo info;
  gst_video_info_init (&info);
  gst_video_info_set_format (&info, GST_VIDEO_FORMAT_I420, width, height);
  GST_VIDEO_INFO_FPS_N (&info) = fps_n;
  GST_VIDEO_INFO_FPS_D (&info) = fps_d;
  GST_VIDEO_INFO_PAR_N (&info) = par_n;
  GST_VIDEO_INFO_PAR_D (&info) = par_d;
  return gst_video_info_to_caps (&info);
}

static GstBuffer *
gst_harness_create_video_buffer_from_info (GstHarness * h, gint value,
    GstVideoInfo * info, GstClockTime timestamp, GstClockTime duration)
{
  GstBuffer *buf;
  gsize size;

  size = GST_VIDEO_INFO_SIZE (info);

  buf = gst_harness_create_buffer (h, size);
  gst_buffer_memset (buf, 0, value, size);
  g_assert (buf != NULL);

  gst_buffer_add_video_meta_full (buf,
      GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_INFO_FORMAT (info),
      GST_VIDEO_INFO_WIDTH (info),
      GST_VIDEO_INFO_HEIGHT (info),
      GST_VIDEO_INFO_N_PLANES (info), info->offset, info->stride);

  GST_BUFFER_PTS (buf) = timestamp;
  GST_BUFFER_DURATION (buf) = duration;

  return buf;
}

static GstBuffer *
gst_harness_create_video_buffer_full (GstHarness * h, gint value,
    guint width, guint height, GstClockTime timestamp, GstClockTime duration)
{
  GstVideoInfo info;

  gst_video_info_init (&info);
  gst_video_info_set_format (&info, GST_VIDEO_FORMAT_I420, width, height);

  return gst_harness_create_video_buffer_from_info (h, value, &info,
      timestamp, duration);
}

GST_START_TEST (test_encode_simple)
{
  gint i;
  GstHarness *h = gst_harness_new ("vp8enc");
  gst_harness_set_src_caps (h, gst_caps_new_i420_full (320, 240, 25, 1, 1, 1));

  for (i = 0; i < 20; i++) {
    GstBuffer *buffer = gst_harness_create_video_buffer_full (h, 0x0,
        320, 240, gst_util_uint64_scale (i, GST_SECOND, 25),
        gst_util_uint64_scale (1, GST_SECOND, 25));
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buffer));
  }

  for (i = 0; i < 20; i++) {
    GstBuffer *buffer = gst_harness_pull (h);

    if (i == 0)
      fail_if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT));

    fail_unless_equals_uint64 (GST_BUFFER_TIMESTAMP (buffer),
        gst_util_uint64_scale (i, GST_SECOND, 25));
    fail_unless_equals_uint64 (GST_BUFFER_DURATION (buffer),
        gst_util_uint64_scale (1, GST_SECOND, 25));

    gst_buffer_unref (buffer);
  }

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_encode_lag_in_frames)
{
  GstFlowReturn ret;
  GstBuffer *buffer;
  GstSegment seg;
  GstHarness *h = gst_harness_new ("vp8enc");
  g_object_set (h->element, "lag-in-frames", 5, NULL);
  gst_harness_set_src_caps (h, gst_caps_new_i420_full (320, 240, 25, 1, 1, 1));

  gst_segment_init (&seg, GST_FORMAT_TIME);
  seg.stop = gst_util_uint64_scale (20, GST_SECOND, 25);
  fail_unless (gst_harness_push_event (h, gst_event_new_segment (&seg)));

  buffer = gst_harness_create_video_buffer_full (h, 0x0,
      320, 240, gst_util_uint64_scale (0, GST_SECOND, 25),
      gst_util_uint64_scale (1, GST_SECOND, 25));

  ret = gst_harness_push (h, gst_buffer_ref (buffer));
  /* If libvpx was built with CONFIG_REALTIME_ONLY, then we'll receive
   * GST_FLOW_NOT_NEGOTIATED. Accept this, and skip the rest of this test
   * in that case. */
  fail_unless (ret == GST_FLOW_OK || ret == GST_FLOW_NOT_NEGOTIATED);

  if (ret == GST_FLOW_OK) {
    gint i;

    for (i = 1; i < 20; i++) {
      GST_BUFFER_TIMESTAMP (buffer) = gst_util_uint64_scale (i, GST_SECOND, 25);
      GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale (1, GST_SECOND, 25);
      fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
              gst_buffer_ref (buffer)));
    }

    fail_unless_equals_int (20, gst_harness_buffers_received (h));

    for (i = 0; i < 20; i++) {
      GstBuffer *outbuf = gst_harness_pull (h);

      if (i == 0)
        fail_if (GST_BUFFER_FLAG_IS_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT));

      fail_unless_equals_uint64 (GST_BUFFER_TIMESTAMP (outbuf),
          gst_util_uint64_scale (i, GST_SECOND, 25));
      fail_unless_equals_uint64 (GST_BUFFER_DURATION (outbuf),
          gst_util_uint64_scale (1, GST_SECOND, 25));

      gst_buffer_unref (outbuf);
    }
  }

  gst_buffer_unref (buffer);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_encode_simple_when_bitrate_set_to_zero)
{
  GstHarness *h = gst_harness_new_parse ("vp8enc target-bitrate=0");
  GstBuffer *buf;

  gst_harness_set_src_caps (h, gst_caps_new_i420 (320, 240));

  buf = gst_harness_create_video_buffer_full (h, 0x42,
      320, 240, 0, gst_util_uint64_scale (GST_SECOND, 1, 30));
  gst_harness_push (h, buf);
  gst_buffer_unref (gst_harness_pull (h));
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_autobitrate_changes_with_caps)
{
  gint bitrate = 0;
  GstHarness *h = gst_harness_new ("vp8enc");
  gst_harness_set_src_caps (h, gst_caps_new_i420_full (1280, 720, 30, 1, 1, 1));

  /* Default settings for 720p @ 30fps ~1.2Mbps */
  g_object_get (h->element, "target-bitrate", &bitrate, NULL);
  fail_unless_equals_int (bitrate, 1199000);

  /* Change bits-per-pixel 0.036 to give us ~1Mbps */
  g_object_set (h->element, "bits-per-pixel", 0.037, NULL);
  g_object_get (h->element, "target-bitrate", &bitrate, NULL);
  fail_unless_equals_int (bitrate, 1022000);

  /* Halving the framerate should halve the auto bitrate */
  gst_harness_set_src_caps (h, gst_caps_new_i420_full (1280, 720, 15, 1, 1, 1));
  g_object_get (h->element, "target-bitrate", &bitrate, NULL);
  fail_unless_equals_int (bitrate, 511000);

  /* Halving the resolution should quarter the auto bitrate */
  gst_harness_set_src_caps (h, gst_caps_new_i420_full (640, 360, 15, 1, 1, 1));
  g_object_get (h->element, "target-bitrate", &bitrate, NULL);
  fail_unless_equals_int (bitrate, 127000);

  gst_harness_teardown (h);
}

GST_END_TEST;

#define verify_meta(buffer, usets, ybit, tid, tl0picindex)               \
  G_STMT_START {                                                         \
    gboolean use_temporal_scaling, layer_sync;                           \
    guint temporal_layer_id, tl0picidx;                                  \
    GstCustomMeta *meta = gst_buffer_get_custom_meta (buffer,            \
        "GstVP8Meta");                                                   \
    fail_unless (meta != NULL);                                          \
    fail_unless (gst_structure_get (meta->structure,                     \
          "use-temporal-scaling", G_TYPE_BOOLEAN, &use_temporal_scaling, \
          "layer-sync", G_TYPE_BOOLEAN, &layer_sync,                     \
          "layer-id", G_TYPE_UINT, &temporal_layer_id,                   \
          "tl0picidx", G_TYPE_UINT, &tl0picidx, NULL));                  \
    fail_unless_equals_int (usets, use_temporal_scaling);                \
    fail_unless_equals_int (ybit, layer_sync);                           \
    fail_unless_equals_int (tid, temporal_layer_id);                     \
    fail_unless_equals_int (tl0picindex, tl0picidx);                     \
  } G_STMT_END

static void
configure_vp8ts (GstHarness * h)
{
  gint i;
  GValue layer_sync_flags = G_VALUE_INIT;
  GValueArray *decimators = g_value_array_new (3);
  GValueArray *layer_ids = g_value_array_new (4);
  GValueArray *bitrates = g_value_array_new (3);
  GValue ival = { 0, }, bval = {
    0,
  };

  gst_value_array_init (&layer_sync_flags, 8);

  g_value_init (&ival, G_TYPE_INT);
  for (i = 0; i < 3; i++) {
    /* 7.5, 15, 30fps */
    static const gint d[] = { 4, 2, 1 };
    g_value_set_int (&ival, d[i]);
    g_value_array_append (decimators, &ival);
  }

  for (i = 0; i < 4; i++) {
    static const gint d[] = { 0, 2, 1, 2 };
    g_value_set_int (&ival, d[i]);
    g_value_array_append (layer_ids, &ival);
  }

  for (i = 0; i < 3; i++) {
    /* Split 512kbps 40%, 20%, 40% */
    static const gint d[] = { 204800, 307200, 512000 };
    g_value_set_int (&ival, d[i]);
    g_value_array_append (bitrates, &ival);
  }

  gst_util_set_object_arg (G_OBJECT (h->element),
      "temporal-scalability-layer-flags",
      /* layer 0 */
      "<no-ref-golden+no-upd-golden+no-upd-alt,"
      /* layer 2 (sync) */
      "no-ref-golden+no-upd-last+no-upd-golden+no-upd-alt+no-upd-entropy,"
      /* layer 1 (sync) */
      "no-ref-golden+no-upd-last+no-upd-alt,"
      /* layer 2 */
      "no-upd-last+no-upd-golden+no-upd-alt+no-upd-entropy,"
      /* layer 0 */
      "no-ref-golden+no-upd-golden+no-upd-alt,"
      /* layer 2 */
      "no-upd-last+no-upd-golden+no-upd-alt+no-upd-entropy,"
      /* layer 1 */
      "no-upd-last+no-upd-alt,"
      /* layer 2 */
      "no-upd-last+no-upd-golden+no-upd-alt+no-upd-entropy>");

  g_value_init (&bval, G_TYPE_BOOLEAN);
  for (i = 0; i < 8; i++) {
    /* Reflect pattern above */
    static const gboolean d[] = {
      FALSE,
      TRUE,
      TRUE,
      FALSE,
      FALSE,
      FALSE,
      FALSE,
      FALSE
    };
    g_value_set_boolean (&bval, d[i]);
    gst_value_array_append_value (&layer_sync_flags, &bval);
  }

  g_object_set_property (G_OBJECT (h->element),
      "temporal-scalability-layer-sync-flags", &layer_sync_flags);

  g_object_set (h->element,
      "temporal-scalability-number-layers", decimators->n_values,
      "temporal-scalability-periodicity", layer_ids->n_values,
      "temporal-scalability-rate-decimator", decimators,
      "temporal-scalability-layer-id", layer_ids,
      "temporal-scalability-target-bitrate", bitrates,
      "error-resilient", 1, NULL);

  g_value_array_free (decimators);
  g_value_array_free (layer_ids);
  g_value_array_free (bitrates);
  g_value_unset (&layer_sync_flags);
}

GST_START_TEST (test_encode_temporally_scaled)
{
  gint i;
  struct
  {
    gboolean ybit;
    gint tid;
    gint tl0picidx;
    gboolean droppable;
  } expected[] = {
    {
        TRUE, 0, 1, FALSE},     /* This is an intra */
    {
        TRUE, 2, 1, TRUE}, {
        TRUE, 1, 1, FALSE}, {
        FALSE, 2, 1, TRUE}, {
        FALSE, 0, 2, FALSE}, {
        FALSE, 2, 2, TRUE}, {
        FALSE, 1, 2, FALSE}, {
        FALSE, 2, 2, TRUE}, {
        FALSE, 0, 3, FALSE}, {
        TRUE, 2, 3, TRUE}, {
        TRUE, 1, 3, FALSE}, {
        FALSE, 2, 3, TRUE}, {
        FALSE, 0, 4, FALSE}, {
        FALSE, 2, 4, TRUE}, {
        FALSE, 1, 4, FALSE}, {
        FALSE, 2, 4, TRUE},
  };
  GstHarness *h = gst_harness_new ("vp8enc");
  gst_harness_set_src_caps (h, gst_caps_new_i420 (320, 240));
  configure_vp8ts (h);

  for (i = 0; i < 16; i++) {
    GstBuffer *in, *out;

    in = gst_harness_create_video_buffer_full (h, 0x42,
        320, 240, gst_util_uint64_scale (i, GST_SECOND, 30),
        gst_util_uint64_scale (1, GST_SECOND, 30));
    gst_harness_push (h, in);

    out = gst_harness_pull (h);
    /* Ensure first frame is encoded as an intra */
    if (i == 0)
      fail_if (GST_BUFFER_FLAG_IS_SET (out, GST_BUFFER_FLAG_DELTA_UNIT));
    else
      fail_unless (GST_BUFFER_FLAG_IS_SET (out, GST_BUFFER_FLAG_DELTA_UNIT));
    fail_unless_equals_int (expected[i].droppable,
        GST_BUFFER_FLAG_IS_SET (out, GST_BUFFER_FLAG_DROPPABLE));
    verify_meta (out, TRUE, expected[i].ybit, expected[i].tid,
        expected[i].tl0picidx);
    gst_buffer_unref (out);
  }

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_encode_fresh_meta)
{
  gint i;
  GstBuffer *buffer;
  GstHarness *h = gst_harness_new ("vp8enc");
  GstCustomMeta *meta;
  gst_harness_set_src_caps (h, gst_caps_new_i420_full (320, 240, 25, 1, 1, 1));

  buffer = gst_harness_create_video_buffer_full (h, 0x0,
      320, 240, gst_util_uint64_scale (0, GST_SECOND, 25),
      gst_util_uint64_scale (1, GST_SECOND, 25));

  /* Attach bogus meta to input buffer */
  meta = gst_buffer_add_custom_meta (buffer, "GstVP8Meta");
  gst_structure_set (meta->structure,
      "use-temporal-scaling", G_TYPE_BOOLEAN, FALSE,
      "layer-sync", G_TYPE_BOOLEAN, FALSE,
      "layer-id", G_TYPE_UINT, 0, "tl0picidx", G_TYPE_UINT, 0, NULL);

  for (i = 0; i < 2; i++) {
    GstBuffer *out;

    fail_unless_equals_int (GST_FLOW_OK,
        gst_harness_push (h, gst_buffer_ref (buffer)));

    out = gst_harness_pull (h);
    /* Ensure that output buffer has fresh meta */
    verify_meta (out, FALSE, (i == 0), 0, i + 1);
    gst_buffer_unref (out);
  }

  gst_buffer_unref (buffer);

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
vp8enc_suite (void)
{
  Suite *s = suite_create ("vp8enc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_encode_simple);
  tcase_add_test (tc_chain, test_encode_lag_in_frames);
  tcase_add_test (tc_chain, test_encode_simple_when_bitrate_set_to_zero);
  tcase_add_test (tc_chain, test_autobitrate_changes_with_caps);
  tcase_add_test (tc_chain, test_encode_temporally_scaled);
  tcase_add_test (tc_chain, test_encode_fresh_meta);

  return s;
}

GST_CHECK_MAIN (vp8enc);
