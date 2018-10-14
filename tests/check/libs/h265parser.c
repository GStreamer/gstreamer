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

unsigned char slice_eos_slice_eob[] = {
  0x00, 0x00, 0x00, 0x01, 0x26, 0x01, 0xaf, 0x06, 0xb8, 0x63, 0xef, 0x3a,
  0x7f, 0x3e, 0x53, 0xff, 0xff, 0xf2, 0x4a, 0xef, 0xff, 0xfe, 0x6a, 0x5d,
  0x60, 0xbc, 0xf8, 0x29, 0xeb, 0x9c, 0x4a, 0xb5, 0xcc, 0x76, 0x30, 0xa0,
  0x7c, 0xd3, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x19, 0x30,
  0x00, 0x00, 0x00, 0x01, 0x48, 0x01,
  0x00, 0x00, 0x00, 0x01, 0x26, 0x01, 0xaf, 0x06, 0xb8, 0x63, 0xef, 0x3a,
  0x7f, 0x3e, 0x53, 0xff, 0xff, 0xf2, 0x4a, 0xef, 0xff, 0xfe, 0x6a, 0x5d,
  0x60, 0xbc, 0xf8, 0x29, 0xeb, 0x9c, 0x4a, 0xb5, 0xcc, 0x76, 0x30, 0xa0,
  0x7c, 0xd3, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x19, 0x30,
  0x00, 0x00, 0x00, 0x01, 0x4a, 0x01,
};

static const guint8 h265_vps_with_nonzero_max_layer_id[] = {
  0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0c, 0x01,
  0xff, 0xff, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00,
  0xb0, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00,
  0x5d, 0xac, 0x59
};

GST_START_TEST (test_h265_parse_slice_eos_slice_eob)
{
  GstH265ParserResult res;
  GstH265NalUnit nalu;
  GstH265Parser *const parser = gst_h265_parser_new ();
  const guint8 *buf = slice_eos_slice_eob;
  guint n, buf_size = sizeof (slice_eos_slice_eob);

  res = gst_h265_parser_identify_nalu (parser, buf, 0, buf_size, &nalu);

  assert_equals_int (res, GST_H265_PARSER_OK);
  assert_equals_int (nalu.type, GST_H265_NAL_SLICE_IDR_W_RADL);
  assert_equals_int (nalu.size, 43);

  n = nalu.offset + nalu.size;
  buf += n;
  buf_size -= n;

  res = gst_h265_parser_identify_nalu (parser, buf, 0, buf_size, &nalu);

  assert_equals_int (res, GST_H265_PARSER_OK);
  assert_equals_int (nalu.type, GST_H265_NAL_EOS);
  assert_equals_int (nalu.size, 2);

  n = nalu.offset + nalu.size;
  buf += n;
  buf_size -= n;

  res = gst_h265_parser_identify_nalu (parser, buf, 0, buf_size, &nalu);

  assert_equals_int (res, GST_H265_PARSER_OK);
  assert_equals_int (nalu.type, GST_H265_NAL_SLICE_IDR_W_RADL);
  assert_equals_int (nalu.size, 43);

  n = nalu.offset + nalu.size;
  buf += n;
  buf_size -= n;

  res = gst_h265_parser_identify_nalu (parser, buf, 0, buf_size, &nalu);

  assert_equals_int (res, GST_H265_PARSER_OK);
  assert_equals_int (nalu.type, GST_H265_NAL_EOB);
  assert_equals_int (nalu.size, 2);

  gst_h265_parser_free (parser);
}

GST_END_TEST;

GST_START_TEST (test_h265_parse_slice_6bytes)
{
  GstH265ParserResult res;
  GstH265NalUnit nalu;
  GstH265Parser *const parser = gst_h265_parser_new ();
  const guint8 *buf = slice_eos_slice_eob;

  res = gst_h265_parser_identify_nalu (parser, buf, 0, 6, &nalu);

  assert_equals_int (res, GST_H265_PARSER_NO_NAL_END);
  assert_equals_int (nalu.type, GST_H265_NAL_SLICE_IDR_W_RADL);
  assert_equals_int (nalu.size, 2);

  gst_h265_parser_free (parser);
}

GST_END_TEST;

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

GST_START_TEST (test_h265_parse_vps)
{
  /* Parsing non-zero vps_max_layer_id in VPS
   * https://bugzilla.gnome.org/show_bug.cgi?id=797279 */
  GstH265Parser *parser;
  GstH265NalUnit nalu;
  GstH265ParserResult res;
  GstH265VPS vps;
  GstH265Profile profile;

  parser = gst_h265_parser_new ();

  res = gst_h265_parser_identify_nalu_unchecked (parser,
      h265_vps_with_nonzero_max_layer_id, 0,
      sizeof (h265_vps_with_nonzero_max_layer_id), &nalu);

  assert_equals_int (res, GST_H265_PARSER_OK);
  assert_equals_int (nalu.type, GST_H265_NAL_VPS);

  res = gst_h265_parser_parse_vps (parser, &nalu, &vps);
  assert_equals_int (res, GST_H265_PARSER_OK);

  assert_equals_int (vps.id, 0);
  assert_equals_int (vps.max_layers_minus1, 0);
  assert_equals_int (vps.max_sub_layers_minus1, 0);
  assert_equals_int (vps.temporal_id_nesting_flag, 1);

  profile = gst_h265_profile_tier_level_get_profile (&vps.profile_tier_level);

  assert_equals_int (profile, GST_H265_PROFILE_MAIN);
  assert_equals_int (vps.sub_layer_ordering_info_present_flag, 1);

  assert_equals_int (vps.max_dec_pic_buffering_minus1[0], 1);
  assert_equals_int (vps.max_num_reorder_pics[0], 0);
  assert_equals_int (vps.max_latency_increase_plus1[0], 0);

  assert_equals_int (vps.max_layer_id, 5);
  assert_equals_int (vps.num_layer_sets_minus1, 0);

  assert_equals_int (vps.timing_info_present_flag, 0);
  assert_equals_int (vps.vps_extension, 0);

  gst_h265_parser_free (parser);
}

GST_END_TEST;

static Suite *
h265parser_suite (void)
{
  Suite *s = suite_create ("H265 Parser library");

  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_h265_parse_slice_eos_slice_eob);
  tcase_add_test (tc_chain, test_h265_parse_slice_6bytes);
  tcase_add_test (tc_chain, test_h265_base_profiles);
  tcase_add_test (tc_chain, test_h265_base_profiles_compat);
  tcase_add_test (tc_chain, test_h265_format_range_profiles_exact_match);
  tcase_add_test (tc_chain, test_h265_format_range_profiles_partial_match);
  tcase_add_test (tc_chain, test_h265_parse_vps);

  return s;
}

GST_CHECK_MAIN (h265parser);
