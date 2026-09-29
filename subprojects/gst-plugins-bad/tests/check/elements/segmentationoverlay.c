/*
 * GStreamer segmentationoverlay tests
 * Copyright (C) 2026 Collabora Ltd.
 *  author: Jeremy Whiting <jeremy.whiting@collabora.com>
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
#include <gst/check/gstharness.h>
#include <gst/video/video.h>

#include <gst/analytics/analytics.h>

#define FRAME_WIDTH 4
/* Mask rows are 1 pixel wide but padded to a 4 byte stride. */
#define MASK_STRIDE 4

/* Push one black BGRA frame carrying a segmentation mask that covers the
 * whole frame, one pixel wide and with the region id of each mask row given in
 * @mask_rows, and check each frame row shows the region of the mask row it is
 * scaled from: two frame rows must look the same exactly when their mask rows
 * carry the same region. */
static void
check_mask_rows_map_to_frame_rows (const guint8 * mask_rows,
    guint mask_height, guint frame_height)
{
  GstHarness *h;
  GstBuffer *in, *out, *mask;
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsSegmentationMtd seg_mtd;
  guint region_ids[256];
  gsize region_count = 0;
  guint8 *mask_data;
  gsize offset[GST_VIDEO_MAX_PLANES] = { 0 };
  gint stride[GST_VIDEO_MAX_PLANES] = { MASK_STRIDE };
  gsize frame_size = FRAME_WIDTH * frame_height * 4;
  guint32 *first_pixel;
  GstMapInfo map;
  gchar *caps;

  h = gst_harness_new ("segmentationoverlay");
  caps = g_strdup_printf ("video/x-raw,format=BGRA,width=%d,height=%u,"
      "framerate=30/1", FRAME_WIDTH, frame_height);
  gst_harness_set_caps_str (h, caps, caps);
  g_free (caps);

  mask_data = g_malloc0 (mask_height * MASK_STRIDE);
  for (guint r = 0; r < mask_height; r++) {
    gboolean seen = FALSE;

    mask_data[r * MASK_STRIDE] = mask_rows[r];
    for (gsize i = 0; i < region_count; i++)
      seen |= region_ids[i] == mask_rows[r];
    if (!seen)
      region_ids[region_count++] = mask_rows[r];
  }
  mask = gst_buffer_new_wrapped (mask_data, mask_height * MASK_STRIDE);
  gst_buffer_add_video_meta_full (mask, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_FORMAT_GRAY8, 1, mask_height, 1, offset, stride);

  in = gst_buffer_new_and_alloc (frame_size);
  gst_buffer_memset (in, 0, 0, frame_size);
  rmeta = gst_buffer_add_analytics_relation_meta (in);
  /* Takes ownership of the mask. */
  fail_unless (gst_analytics_relation_meta_add_segmentation_mtd (rmeta, mask,
          GST_SEGMENTATION_TYPE_SEMANTIC, region_count, region_ids, 0, 0,
          FRAME_WIDTH, frame_height, &seg_mtd));

  out = gst_harness_push_and_pull (h, in);
  fail_unless (out != NULL);

  /* Mask rows are 1 pixel wide, so the first pixel stands for the frame row. */
  first_pixel = g_new (guint32, frame_height);
  fail_unless (gst_buffer_map (out, &map, GST_MAP_READ));
  for (guint y = 0; y < frame_height; y++)
    memcpy (&first_pixel[y], map.data + y * FRAME_WIDTH * 4, 4);
  gst_buffer_unmap (out, &map);

  for (guint a = 0; a < frame_height; a++) {
    guint8 region_a = mask_rows[(a * mask_height) / frame_height];

    for (guint b = a + 1; b < frame_height; b++) {
      guint8 region_b = mask_rows[(b * mask_height) / frame_height];

      if (region_a == region_b) {
        fail_unless (first_pixel[a] == first_pixel[b],
            "frame rows %u and %u should both show region %u, got %08x and "
            "%08x", a, b, region_a, first_pixel[a], first_pixel[b]);
      } else {
        fail_if (first_pixel[a] == first_pixel[b],
            "frame rows %u and %u show the same color but should show "
            "regions %u and %u", a, b, region_a, region_b);
      }
    }
  }

  g_free (first_pixel);
  gst_buffer_unref (out);
  gst_harness_teardown (h);
}

/* Each mask row covers two frame rows. */
GST_START_TEST (test_mask_upscale_rows)
{
  static const guint8 mask_rows[] = { 1, 2, 3, 4 };

  check_mask_rows_map_to_frame_rows (mask_rows, G_N_ELEMENTS (mask_rows), 8);
}

GST_END_TEST;

/* Every other mask row is used. The last frame row repeats the region of the
 * first, so it must match it exactly (and cannot be left undrawn). */
GST_START_TEST (test_mask_downscale_rows)
{
  static const guint8 mask_rows[] = { 1, 1, 2, 2, 3, 3, 1, 1 };

  check_mask_rows_map_to_frame_rows (mask_rows, G_N_ELEMENTS (mask_rows), 4);
}

GST_END_TEST;

static Suite *
segmentationoverlay_suite (void)
{
  Suite *s;
  TCase *tc_chain;

  s = suite_create ("segmentationoverlay");
  tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_mask_upscale_rows);
  tcase_add_test (tc_chain, test_mask_downscale_rows);

  return s;
}

GST_CHECK_MAIN (segmentationoverlay);
