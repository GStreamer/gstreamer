/* GStreamer
 * Copyright (C) 2025 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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
#include "../../ext/vulkan/base/gsth26xgopmapper.h"

GstH26XGOPMapper *mapper = NULL;

static void
setup (void)
{
  mapper = gst_h26x_gop_mapper_new ();
}

static void
teardown (void)
{
  g_object_unref (mapper);
}

/* Test for a GOP of 32 pictures with only I frames */
GST_START_TEST (test_intra_only_gop_32_frames)
{
  GstH26XGOP *gop;
  guint i;
  gboolean ret;
  GstH26XGOPParameters params = {
    .idr_period = 32,           /* GOP size */
    .ip_period = 0,             /* 0 means intra-only */
    .i_period = 0,              /* Not used for intra-only */
    .num_bframes = 0,
    .b_pyramid = FALSE,         /* No B pyramid */
    .highest_pyramid_level = 0,
    .num_iframes = 0,
  };

  fail_unless (mapper != NULL, "Failed to create mapper");

  /* Set parameters */
  ret = gst_h26x_gop_mapper_set_params (mapper, &params);
  fail_unless (ret == TRUE, "Failed to set parameters for intra-only GOP");

  /* Generate GOP map */
  gst_h26x_gop_mapper_generate (mapper);

  /* Check all frames are I frames */
  gst_h26x_gop_mapper_reset_index (mapper);
  for (i = 0; i < params.idr_period; i++) {
    gop = gst_h26x_gop_mapper_get_next (mapper);
    fail_unless (gop != NULL, "Expected GOP frame at index %u but got NULL", i);

    /* All frames should be I frames in intra-only stream */
    fail_unless (GST_H26X_GOP_IS (gop, I),
        "Frame at index %u should be I frame but got %d", i, gop->type);

    /* First frame should be reference (IDR), others should not be reference */
    if (i == 0) {
      fail_unless (gop->is_ref == TRUE,
          "First frame (index 0) should be reference frame (IDR)");
    } else {
      fail_unless (gop->is_ref == FALSE,
          "Frame at index %u should not be reference frame", i);
    }
  }

  /* Should begin after 32 */
  fail_unless (gst_h26x_gop_mapper_is_last_current_index (mapper),
      "Expected GOP frame is last");
  gop = gst_h26x_gop_mapper_get_next (mapper);
  fail_unless (gop && GST_H26X_GOP_IS (gop, I) && gop->is_ref == TRUE,
      "Expected IDR GOP frame");

  /* Test reset and iterate again */
  gst_h26x_gop_mapper_reset_index (mapper);
  for (i = 0; i < 5; i++) {
    gop = gst_h26x_gop_mapper_get_next (mapper);
    fail_unless (gop != NULL, "Expected GOP frame after reset at index %u", i);
    fail_unless (GST_H26X_GOP_IS (gop, I),
        "Frame after reset at index %u should be I frame", i);
  }

  /* Test set_current_index */
  gst_h26x_gop_mapper_set_current_index (mapper, 15);
  fail_unless (gst_h26x_gop_mapper_get_current_index (mapper) == 15,
      "Current index should be 15 after set_current_index(15)");

  gop = gst_h26x_gop_mapper_get_next (mapper);
  fail_unless (gop != NULL, "Expected GOP frame at index 15");
  fail_unless (GST_H26X_GOP_IS (gop, I), "Frame at index 15 should be I frame");
}

GST_END_TEST;

/* Test for parameter validation */
GST_START_TEST (test_parameter_validation)
{
  GstH26XGOPParameters params = { 0 };
  gboolean ret;

  fail_unless (mapper != NULL, "Failed to create mapper");

  /* Test valid parameters for intra-only */
  params.idr_period = 32;
  params.ip_period = 0;
  params.i_period = 0;
  params.num_bframes = 0;
  params.b_pyramid = FALSE;
  params.highest_pyramid_level = 0;
  params.num_iframes = 0;

  ret = gst_h26x_gop_mapper_set_params (mapper, &params);
  fail_unless (ret == TRUE, "Valid intra-only parameters should be accepted");

  /* Test invalid: idr_period = 0 */
  params.idr_period = 0;
  params.ip_period = 0;
  ret = gst_h26x_gop_mapper_set_params (mapper, &params);
  fail_unless (ret == FALSE, "idr_period = 0 should be rejected");

  /* Test invalid: num_bframes > 31 */
  params.b_pyramid = TRUE;
  params.highest_pyramid_level = 1;
  params.ip_period = 33;
  params.num_bframes = 32;
  ret = gst_h26x_gop_mapper_set_params (mapper, &params);
  fail_unless (ret == FALSE, "num_bframes > 31 should be rejected");

  /* Test valid: num_bframes = 32 */
  params.b_pyramid = FALSE;
  params.highest_pyramid_level = 0;
  params.idr_period = 64;
  params.num_bframes = 32;
  ret = gst_h26x_gop_mapper_set_params (mapper, &params);
  fail_unless (ret == TRUE, "num_bframes = 32 should be valid");

  /* Test invalid: ip_period <= idr_period when not 0 */
  params.idr_period = 32;
  params.ip_period = 33;        /* Equal to idr_period - 1 */
  ret = gst_h26x_gop_mapper_set_params (mapper, &params);
  fail_unless (ret == FALSE, "ip_period > idr_period should be rejected");

  /* Test invalid: i_period <= idr_period when not 0 */
  params.i_period = 64;         /* Equal to idr_period */
  ret = gst_h26x_gop_mapper_set_params (mapper, &params);
  fail_unless (ret == FALSE, "i_period > idr_period should be rejected");
}

GST_END_TEST;

static void
count_gop_types (GstH26XGOPMapper * mapper, guint32 idr_period,
    guint * num_i_frames, guint * num_p_frames, guint * num_b_frames)
{
  *num_i_frames = 0;
  *num_p_frames = 0;
  *num_b_frames = 0;

  gst_h26x_gop_mapper_reset_index (mapper);

  for (guint i = 0; i < idr_period; i++) {
    GstH26XGOP *gop = gst_h26x_gop_mapper_get_next (mapper);
    fail_unless (gop != NULL, "Expected GOP frame at index %u", i);

    switch (gop->type) {
      case GST_H26X_GOP_TYPE_I:
        *num_i_frames += 1;
        break;
      case GST_H26X_GOP_TYPE_P:
        *num_p_frames += 1;
        break;
      case GST_H26X_GOP_TYPE_B:
        *num_b_frames += 1;
        break;
      default:
        fail ("Unknown frame type %d", gop->type);
    }
  }
}

 /* Test for a GOP with B frames */
GST_START_TEST (test_gop_with_b_frames)
{
  gboolean ret;
  GstH26XGOPParameters params = {
    .idr_period = 16,           /* GOP size */
    .ip_period = 4,             /* I/P to P distance */
    .i_period = 0,
    .num_bframes = 3,           /* 3 B frames between I/P and P */
    .b_pyramid = FALSE,         /* No B pyramid */
    .highest_pyramid_level = 0,
    .num_iframes = 0,
  };
  guint num_p_frames, num_b_frames, num_i_frames;

  fail_unless (mapper != NULL, "Failed to create mapper");

  /* Set parameters */
  ret = gst_h26x_gop_mapper_set_params (mapper, &params);
  fail_unless (ret == TRUE, "Failed to set parameters for GOP with B frames");

  /* Generate GOP map */
  gst_h26x_gop_mapper_generate (mapper);

  /* Count frame types */
  count_gop_types (mapper, params.idr_period, &num_i_frames, &num_p_frames,
      &num_b_frames);

  /* Check counts: first frame is I, last frame is forced to P,
     and pattern should be I B B P B B P ... */
  fail_unless (num_i_frames == 1, "Should have exactly 1 I frame");
  /* With idr_period=16, ip_period=4, num_bframes=12:
     Pattern: I B B B P B B B P B B B P B B P
     So: 1 I, 4 P frames (including last forced P), 11 B frames */
  fail_unless (num_p_frames == 4, "Should have exactly 4 P frames");
  /* last B frame is replaced by a P frame */
  fail_unless (num_b_frames == 11, "Should have exactly 11 B frames");
}

GST_END_TEST;

 /* Test for a GOP with B frames */
GST_START_TEST (test_gop_with_b_pyramid)
{
  gboolean ret;
  GstH26XGOPParameters params = {
    .idr_period = 16,           /* GOP size */
    .ip_period = 4,             /* I/P to P distance */
    .i_period = 2,
    .num_bframes = 3,           /* 3 B frames between I/P and P */
    .b_pyramid = TRUE,          /* B pyramid */
    .highest_pyramid_level = 1,
    .num_iframes = 0,
  };
  guint num_p_frames, num_b_frames, num_i_frames;

  fail_unless (mapper != NULL, "Failed to create mapper");

  /* Set parameters */
  ret = gst_h26x_gop_mapper_set_params (mapper, &params);
  fail_unless (ret == TRUE, "Failed to set parameters for GOP with B frames");

  /* Generate GOP map */
  gst_h26x_gop_mapper_generate (mapper);

  /* Count frame types */
  count_gop_types (mapper, params.idr_period, &num_i_frames, &num_p_frames,
      &num_b_frames);

  /* Check counts: first frame is I, last frame is forced to P,
     and pattern should be I B B P B B P ... */
  fail_unless (num_i_frames == 1, "Should have exactly 1 I frame");
  /* With idr_period=16, ip_period=4, num_bframes=12:
     Pattern: I B B B P B B B P B B B P B B P
     So: 1 I, 4 P frames (including last forced P), 11 B frames */
  fail_unless (num_p_frames == 4, "Should have exactly 4 P frames");
  /* last B frame is replaced by a P frame */
  fail_unless (num_b_frames == 11, "Should have exactly 11 B frames");
}

GST_END_TEST;


GST_START_TEST (test_big_gop)
{
  GstH26XGOPParameters params = {
    .idr_period = 5 * 1024,     /* GOP size */
    .ip_period = 1,
    .i_period = 32,
    .num_iframes = 160,
  };
  guint num_p_frames, num_b_frames, num_i_frames;

  fail_unless (mapper != NULL, "Failed to create mapper");
  fail_unless (gst_h26x_gop_mapper_set_params (mapper, &params),
      "Failed to set parameters");

  /* Generate GOP map */
  gst_h26x_gop_mapper_generate (mapper);

  /* Count frame types */
  count_gop_types (mapper, params.idr_period, &num_i_frames, &num_p_frames,
      &num_b_frames);

  fail_unless (num_i_frames == 160, "Should have exactly 160 I frame");
  fail_unless (num_p_frames == 4960, "Should have exactly 4960 P frames");
  fail_unless (num_b_frames == 0, "Should have exactly 0 B frames");
}

GST_END_TEST;

/* Test suite */
static Suite *
gsth26xgopmapper_suite (void)
{
  Suite *s = suite_create ("gsth26xgopmapper");
  TCase *tc_chain = tcase_create ("general");

  tcase_add_checked_fixture (tc_chain, setup, teardown);

  tcase_add_test (tc_chain, test_parameter_validation);
  tcase_add_test (tc_chain, test_intra_only_gop_32_frames);
  tcase_add_test (tc_chain, test_gop_with_b_frames);
  tcase_add_test (tc_chain, test_gop_with_b_pyramid);
  tcase_add_test (tc_chain, test_big_gop);
  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (gsth26xgopmapper);
