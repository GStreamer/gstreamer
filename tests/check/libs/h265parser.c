/* Gstreamer
 * Copyright (C) <2018> Collabora Ltd.
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
#include <gst/codecparsers/gsth265parser.h>

GST_START_TEST (test_h265_base_profiles)
{
  GstH265ProfileTierLevel ptl;

  memset (&ptl, 0, sizeof (ptl));

  ptl.profile_idc = 1;
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN);
  ptl.profile_idc = 2;
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_10);
  ptl.profile_idc = 3;
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_STILL_PICTURE);

  ptl.profile_idc = 42;
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_INVALID);
}

GST_END_TEST;

GST_START_TEST (test_h265_base_profiles_compat)
{
  GstH265ProfileTierLevel ptl;

  memset (&ptl, 0, sizeof (ptl));

  ptl.profile_compatibility_flag[1] = 1;
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN);
  ptl.profile_compatibility_flag[1] = 0;

  ptl.profile_compatibility_flag[2] = 1;
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_10);
  ptl.profile_compatibility_flag[2] = 0;

  ptl.profile_compatibility_flag[3] = 1;
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_STILL_PICTURE);
  ptl.profile_compatibility_flag[3] = 0;

  ptl.profile_idc = 42;
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_INVALID);
}

GST_END_TEST;

static void
set_format_range_fields (GstH265ProfileTierLevel * ptl,
    guint8 max_12bit_constraint_flag,
    guint8 max_10bit_constraint_flag,
    guint8 max_8bit_constraint_flag,
    guint8 max_422chroma_constraint_flag,
    guint8 max_420chroma_constraint_flag,
    guint8 max_monochrome_constraint_flag,
    guint8 intra_constraint_flag,
    guint8 one_picture_only_constraint_flag,
    guint8 lower_bit_rate_constraint_flag)
{
  ptl->max_12bit_constraint_flag = max_12bit_constraint_flag;
  ptl->max_10bit_constraint_flag = max_10bit_constraint_flag;
  ptl->max_8bit_constraint_flag = max_8bit_constraint_flag;
  ptl->max_422chroma_constraint_flag = max_422chroma_constraint_flag;
  ptl->max_420chroma_constraint_flag = max_420chroma_constraint_flag;
  ptl->max_monochrome_constraint_flag = max_monochrome_constraint_flag;
  ptl->intra_constraint_flag = intra_constraint_flag;
  ptl->one_picture_only_constraint_flag = one_picture_only_constraint_flag;
  ptl->lower_bit_rate_constraint_flag = lower_bit_rate_constraint_flag;
}

GST_START_TEST (test_h265_format_range_profiles_exact_match)
{
  /* Test all the combinations from Table A.2 */
  GstH265ProfileTierLevel ptl;

  memset (&ptl, 0, sizeof (ptl));
  ptl.profile_idc = 4;

  set_format_range_fields (&ptl, 1, 1, 1, 1, 1, 1, 0, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MONOCHROME);

  set_format_range_fields (&ptl, 1, 0, 0, 1, 1, 1, 0, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MONOCHROME_12);

  set_format_range_fields (&ptl, 0, 0, 0, 1, 1, 1, 0, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MONOCHROME_16);

  set_format_range_fields (&ptl, 1, 0, 0, 1, 1, 0, 0, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_12);

  set_format_range_fields (&ptl, 1, 1, 0, 1, 0, 0, 0, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_422_10);

  set_format_range_fields (&ptl, 1, 0, 0, 1, 0, 0, 0, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_422_12);

  set_format_range_fields (&ptl, 1, 1, 1, 0, 0, 0, 0, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_444);

  set_format_range_fields (&ptl, 1, 1, 0, 0, 0, 0, 0, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_444_10);

  set_format_range_fields (&ptl, 1, 0, 0, 0, 0, 0, 0, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_444_12);

  set_format_range_fields (&ptl, 1, 1, 1, 1, 1, 0, 1, 0, 0);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_INTRA);
  set_format_range_fields (&ptl, 1, 1, 1, 1, 1, 0, 1, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_INTRA);

  set_format_range_fields (&ptl, 1, 1, 0, 1, 1, 0, 1, 0, 0);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_10_INTRA);
  set_format_range_fields (&ptl, 1, 1, 0, 1, 1, 0, 1, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_10_INTRA);

  set_format_range_fields (&ptl, 1, 0, 0, 1, 1, 0, 1, 0, 0);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_12_INTRA);
  set_format_range_fields (&ptl, 1, 0, 0, 1, 1, 0, 1, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_12_INTRA);

  set_format_range_fields (&ptl, 1, 1, 0, 1, 0, 0, 1, 0, 0);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_422_10_INTRA);
  set_format_range_fields (&ptl, 1, 1, 0, 1, 0, 0, 1, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_422_10_INTRA);

  set_format_range_fields (&ptl, 1, 0, 0, 1, 0, 0, 1, 0, 0);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_422_12_INTRA);
  set_format_range_fields (&ptl, 1, 0, 0, 1, 0, 0, 1, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_422_12_INTRA);

  set_format_range_fields (&ptl, 1, 1, 1, 0, 0, 0, 1, 0, 0);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_444_INTRA);
  set_format_range_fields (&ptl, 1, 1, 1, 0, 0, 0, 1, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_444_INTRA);

  set_format_range_fields (&ptl, 1, 1, 0, 0, 0, 0, 1, 0, 0);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_444_10_INTRA);
  set_format_range_fields (&ptl, 1, 1, 0, 0, 0, 0, 1, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_444_10_INTRA);

  set_format_range_fields (&ptl, 1, 0, 0, 0, 0, 0, 1, 0, 0);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_444_12_INTRA);
  set_format_range_fields (&ptl, 1, 0, 0, 0, 0, 0, 1, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_444_12_INTRA);

  set_format_range_fields (&ptl, 0, 0, 0, 0, 0, 0, 1, 0, 0);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_444_16_INTRA);
  set_format_range_fields (&ptl, 0, 0, 0, 0, 0, 0, 1, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_444_16_INTRA);

  set_format_range_fields (&ptl, 1, 1, 1, 0, 0, 0, 1, 1, 0);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_444_STILL_PICTURE);
  set_format_range_fields (&ptl, 1, 1, 1, 0, 0, 0, 1, 1, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_444_STILL_PICTURE);

  set_format_range_fields (&ptl, 0, 0, 0, 0, 0, 0, 1, 1, 0);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_444_16_STILL_PICTURE);
  set_format_range_fields (&ptl, 0, 0, 0, 0, 0, 0, 1, 1, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_444_16_STILL_PICTURE);
}

GST_END_TEST;

GST_START_TEST (test_h265_format_range_profiles_partial_match)
{
  /* Test matching compatible profiles from non-standard bitstream */
  GstH265ProfileTierLevel ptl;

  memset (&ptl, 0, sizeof (ptl));
  ptl.profile_idc = 4;

  set_format_range_fields (&ptl, 1, 1, 1, 1, 0, 0, 0, 0, 1);
  g_assert_cmpuint (gst_h265_profile_tier_level_get_profile (&ptl), ==,
      GST_H265_PROFILE_MAIN_444);
}

GST_END_TEST;

static Suite *
h265parser_suite (void)
{
  Suite *s = suite_create ("H265 Parser library");

  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_h265_base_profiles);
  tcase_add_test (tc_chain, test_h265_base_profiles_compat);
  tcase_add_test (tc_chain, test_h265_format_range_profiles_exact_match);
  tcase_add_test (tc_chain, test_h265_format_range_profiles_partial_match);

  return s;
}

GST_CHECK_MAIN (h265parser);
