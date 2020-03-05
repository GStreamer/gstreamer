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

static const guint8 h265_pps_with_range_extension[] = {
  0x00, 0x00, 0x00, 0x01, 0x44, 0x01, 0x30, 0x42, 0x13, 0x1c, 0x0c, 0x60,
  0xe1, 0xd9, 0x38, 0x83, 0xb6, 0x38, 0x2c, 0x19, 0x29, 0x82, 0x42, 0xee,
  0x61, 0xec, 0x28, 0x11, 0x1a, 0x51, 0xc1, 0x60, 0xc9, 0x4c, 0x12, 0x17,
  0x73, 0x0f, 0x61, 0x40, 0x88, 0xd1, 0x05, 0x38, 0x20, 0x28, 0x94, 0xc5,
  0x1c, 0x26, 0x70, 0xb0, 0x44, 0x20, 0x30, 0x69, 0x4a, 0x16, 0x12, 0x2c,
  0x20, 0x83, 0xe3, 0x06, 0x87, 0x87, 0xc7, 0x30, 0xa9, 0x22, 0xd0, 0xb1,
  0x01, 0x40, 0x98, 0xa1, 0x02, 0x47, 0x33, 0x85, 0x43, 0xc1, 0x31, 0x01,
  0x18, 0x68, 0x2e, 0x3a, 0x20, 0x22, 0x20, 0x48, 0xc0, 0xd8, 0xe0, 0xa8,
  0xa1, 0xc5, 0x04, 0x05, 0x12, 0x98, 0xa3, 0x84, 0xce, 0x16, 0x08, 0x84,
  0x06, 0x0d, 0x29, 0x42, 0xc2, 0x45, 0x84, 0x10, 0x7c, 0x60, 0xd0, 0xf0,
  0xf8, 0xe6, 0x15, 0x24, 0x5a, 0x16, 0x20, 0x28, 0x13, 0x14, 0x20, 0x48,
  0xe6, 0x70, 0xa8, 0x78, 0x26, 0x20, 0x23, 0x0d, 0x05, 0xc7, 0x44, 0x04,
  0x44, 0x09, 0x18, 0x1b, 0x1c, 0x15, 0x14, 0x3a, 0x08, 0x0a, 0x25, 0x31,
  0x47, 0x09, 0x9c, 0x2c, 0x11, 0x08, 0x0c, 0x1a, 0x52, 0x85, 0x84, 0x8b,
  0x08, 0x20, 0xf8, 0xc1, 0xa1, 0xe1, 0xf1, 0xcc, 0x2a, 0x48, 0xb4, 0x2c,
  0x40, 0x50, 0x26, 0x28, 0x40, 0x91, 0xcc, 0xe1, 0x50, 0xf0, 0x4c, 0x40,
  0x46, 0x1a, 0x0b, 0x8e, 0x88, 0x08, 0x88, 0x12, 0x30, 0x36, 0x38, 0x2a,
  0x28, 0x71, 0x41, 0x01, 0x44, 0xa6, 0x28, 0xe1, 0x33, 0x85, 0x82, 0x21,
  0x01, 0x83, 0x4a, 0x50, 0xb0, 0x91, 0x61, 0x04, 0x1f, 0x18, 0x34, 0x3c,
  0x3e, 0x39, 0x85, 0x49, 0x16, 0x85, 0x88, 0x0a, 0x04, 0xc5, 0x08, 0x12,
  0x39, 0x9c, 0x2a, 0x1e, 0x09, 0x88, 0x08, 0xc3, 0x41, 0x71, 0xd1, 0x01,
  0x11, 0x02, 0x46, 0x06, 0xc7, 0x05, 0x45, 0x0e, 0x82, 0x00, 0x88, 0xc0,
  0xa9, 0xc3, 0x08, 0xc1, 0xf0, 0xf1, 0x43, 0xe2, 0x04, 0x04, 0x83, 0x28,
  0x51, 0x03, 0x64, 0x20, 0x70, 0x22, 0x13, 0x08, 0x18, 0x68, 0xd1, 0xc3,
  0x04, 0x8d, 0x87, 0x85, 0x86, 0x43, 0x81, 0x50, 0xd0, 0xf0, 0x98, 0x70,
  0xa6, 0x1e, 0x34, 0x31, 0x0d, 0x87, 0x82, 0xe8, 0xf0, 0xc0, 0xd2, 0x94,
  0xa1, 0x20, 0xcb, 0x31, 0x88, 0xa0, 0x80, 0x22, 0x30, 0x2a, 0x70, 0xc2,
  0x30, 0x7c, 0x3c, 0x50, 0xf8, 0x81, 0x01, 0x20, 0xca, 0x14, 0x40, 0xd9,
  0x08, 0x1c, 0x08, 0x84, 0xc2, 0x06, 0x1a, 0x34, 0x70, 0xc1, 0x23, 0x61,
  0xe1, 0x61, 0x90, 0xe0, 0x54, 0x34, 0x3c, 0x26, 0x1c, 0x29, 0x87, 0x8d,
  0x0c, 0x43, 0x61, 0xe0, 0xba, 0x3c, 0x30, 0x34, 0xa5, 0x28, 0x48, 0x32,
  0xcc, 0x68, 0x20, 0x08, 0x8c, 0x0a, 0x9c, 0x30, 0x8c, 0x1f, 0x0f, 0x14,
  0x3e, 0x20, 0x40, 0x48, 0x32, 0x85, 0x10, 0x36, 0x42, 0x07, 0x02, 0x21,
  0x30, 0x81, 0x86, 0x8d, 0x1c, 0x30, 0x48, 0xd8, 0x78, 0x58, 0x64, 0x38,
  0x15, 0x0d, 0x0f, 0x09, 0x87, 0x0a, 0x61, 0xe3, 0x43, 0x10, 0xd8, 0x78,
  0x2e, 0x8f, 0x0c, 0x0d, 0x29, 0x4a, 0x12, 0x0c, 0xb3, 0x1a, 0x08, 0x02,
  0x23, 0x02, 0xa7, 0x0c, 0x23, 0x07, 0xc3, 0xc5, 0x0f, 0x88, 0x10, 0x12,
  0x0c, 0xa1, 0x44, 0x0d, 0x90, 0x81, 0xc0, 0x88, 0x4c, 0x20, 0x61, 0xa3,
  0x47, 0x0c, 0x12, 0x36, 0x1e, 0x16, 0x19, 0x0e, 0x05, 0x43, 0x43, 0xc2,
  0x61, 0xc2, 0x98, 0x78, 0xd0, 0xc4, 0x36, 0x1e, 0x0b, 0xa3, 0xc3, 0x03,
  0x4a, 0x52, 0x84, 0x83, 0x2c, 0xc6, 0x4a, 0x56, 0x01, 0x46, 0x89, 0x0c,
  0xce, 0x25, 0x04, 0x83, 0x21, 0x96, 0x3b, 0x80,
};

static guint8 h265_sps[] = {
  0x00, 0x00, 0x00, 0x01, 0x42, 0x01, 0x01, 0x21, 0x60, 0x00, 0x00, 0x03,
  0x00, 0xb0, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x99, 0xa0, 0x01,
  0xe0, 0x20, 0x02, 0x1c, 0x59, 0x4b, 0x92, 0x42, 0x96, 0x11, 0x80, 0xb5,
  0x01, 0x01, 0x01, 0x14, 0x00, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x03,
  0x00, 0xf3, 0xf2, 0x00, 0x6e, 0x00, 0x17, 0xbd, 0xf8, 0x00, 0x02, 0x94,
  0xb4, 0x00, 0x06, 0x9b, 0x60, 0x00, 0xd3, 0x6c, 0x00, 0x01, 0x4a, 0x5a,
  0x40, 0x00, 0x14, 0xa5, 0xa0, 0x00, 0x34, 0xdb, 0x00, 0x06, 0x9b, 0x60,
  0x00, 0x0a, 0x52, 0xd0, 0x40,
};

static guint8 h265_sei_pic_timing[] = {
  0x00, 0x00, 0x00, 0x01, 0x4e, 0x01, 0x01, 0x10, 0x04, 0x00, 0x00, 0x03, 0x00,
  0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x08, 0xaf, 0xff, 0xff,
  0xff, 0xfe, 0x80
};


GST_START_TEST (test_h265_parse_slice_eos_slice_eob)
{
  GstH265ParserResult res;
  GstH265NalUnit nalu;
  GstH265Parser *parser = gst_h265_parser_new ();
  const guint8 *buf = slice_eos_slice_eob;
  guint n, buf_size = sizeof (slice_eos_slice_eob);

  res = gst_h265_parser_identify_nalu (parser, buf, 0, buf_size, &nalu);

  assert_equals_int (res, GST_H265_PARSER_OK);
  assert_equals_int (nalu.type, GST_H265_NAL_SLICE_IDR_W_RADL);
  assert_equals_int (nalu.size, buf_size / 2 - 10);     /* 2 slices, 1 start code(4) and EOx(6) */

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

GST_START_TEST (test_h265_parse_pic_timing)
{
  GstH265NalUnit nalu;
  GstH265Parser *parser = gst_h265_parser_new ();
  const guint8 *buf = h265_sps;
  guint i, buf_size = sizeof (h265_sps);
  GArray *messages;
  GstH265SEIMessage sei;
  GstH265SPS sps;

  assert_equals_int (gst_h265_parser_identify_nalu (parser, buf, 0, buf_size,
          &nalu), GST_H265_PARSER_NO_NAL_END);
  assert_equals_int (nalu.type, GST_H265_NAL_SPS);
  assert_equals_int (nalu.size, buf_size - 4);  /* 4 for start_code */

  assert_equals_int (gst_h265_parser_parse_sps (parser, &nalu, &sps, TRUE),
      GST_H265_PARSER_OK);

  buf = h265_sei_pic_timing;
  buf_size = sizeof (h265_sei_pic_timing);

  assert_equals_int (gst_h265_parser_identify_nalu (parser, buf, 0, buf_size,
          &nalu), GST_H265_PARSER_NO_NAL_END);
  assert_equals_int (nalu.type, GST_H265_NAL_PREFIX_SEI);
  assert_equals_int (nalu.size, buf_size - 4);  /* 4 for start_code size */

  assert_equals_int (gst_h265_parser_parse_sei (parser, &nalu, &messages),
      GST_H265_PARSER_OK);

  for (i = 0; i < messages->len; i++) {
    sei = g_array_index (messages, GstH265SEIMessage, i);
    assert_equals_int (sei.payloadType, GST_H265_SEI_PIC_TIMING);
    assert_equals_int (sei.payload.pic_timing.pic_struct, 0);
    assert_equals_int (sei.payload.pic_timing.source_scan_type, 1);
    assert_equals_int (sei.payload.pic_timing.duplicate_flag, 0);
    assert_equals_int (sei.payload.pic_timing.au_cpb_removal_delay_minus1, 0);
    assert_equals_int (sei.payload.pic_timing.pic_dpb_output_delay, 0);
    assert_equals_int (sei.payload.pic_timing.pic_dpb_output_du_delay, 0);
    assert_equals_int (sei.payload.pic_timing.num_decoding_units_minus1, 33);
    assert_equals_int (sei.payload.pic_timing.du_common_cpb_removal_delay_flag,
        1);
    assert_equals_int (sei.payload.
        pic_timing.du_common_cpb_removal_delay_increment_minus1, 0);
  }
  g_array_free (messages, TRUE);
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

GST_START_TEST (test_h265_parse_pps)
{
  GstH265Parser *parser;
  GstH265NalUnit nalu;
  GstH265ParserResult res;
  GstH265PPS pps;

  parser = gst_h265_parser_new ();

  /*init sps[15] to avoid return error in gst_h265_parser_parse_pps */
  parser->sps[15].valid = TRUE;
  parser->sps[15].log2_diff_max_min_luma_coding_block_size = 1;

  res = gst_h265_parser_identify_nalu_unchecked (parser,
      h265_pps_with_range_extension, 0,
      sizeof (h265_pps_with_range_extension), &nalu);

  assert_equals_int (res, GST_H265_PARSER_OK);
  assert_equals_int (nalu.type, GST_H265_NAL_PPS);

  res = gst_h265_parser_parse_pps (parser, &nalu, &pps);

  assert_equals_int (res, GST_H265_PARSER_OK);
  assert_equals_int (pps.pps_range_extension_flag, 1);
  assert_equals_int (pps.pps_multilayer_extension_flag, 0);
  assert_equals_int (pps.pps_3d_extension_flag, 0);
  assert_equals_int (pps.pps_extension_5bits, 0);
  assert_equals_int (pps.
      pps_extension_params.log2_max_transform_skip_block_size_minus2, 0);
  assert_equals_int (pps.
      pps_extension_params.cross_component_prediction_enabled_flag, 0);
  assert_equals_int (pps.
      pps_extension_params.chroma_qp_offset_list_enabled_flag, 1);
  assert_equals_int (pps.pps_extension_params.diff_cu_chroma_qp_offset_depth,
      1);
  assert_equals_int (pps.pps_extension_params.chroma_qp_offset_list_len_minus1,
      5);
  assert_equals_int (pps.pps_extension_params.log2_sao_offset_scale_luma, 0);
  assert_equals_int (pps.pps_extension_params.log2_sao_offset_scale_chroma, 0);

  gst_h265_parser_free (parser);
}

GST_END_TEST;

typedef struct
{
  GstH265NalUnitType type;
  gboolean is_idr;
  gboolean is_irap;
  gboolean is_bla;
  gboolean is_cra;
  gboolean is_radl;
  gboolean is_rasl;
} H265NalTypeTestVector;

GST_START_TEST (test_h265_nal_type_classification)
{
  gint i;
  /* *INDENT-OFF* */
  H265NalTypeTestVector test_vector[] = {
    /*         NAL-TYPE             IDR    IRAP   BLA    CRA    RADL   RASL */
    {GST_H265_NAL_SLICE_TRAIL_N,    FALSE, FALSE, FALSE, FALSE, FALSE, FALSE},
    {GST_H265_NAL_SLICE_TRAIL_R,    FALSE, FALSE, FALSE, FALSE, FALSE, FALSE},
    {GST_H265_NAL_SLICE_TSA_N,      FALSE, FALSE, FALSE, FALSE, FALSE, FALSE},
    {GST_H265_NAL_SLICE_TSA_R,      FALSE, FALSE, FALSE, FALSE, FALSE, FALSE},
    {GST_H265_NAL_SLICE_STSA_N,     FALSE, FALSE, FALSE, FALSE, FALSE, FALSE},
    {GST_H265_NAL_SLICE_STSA_R,     FALSE, FALSE, FALSE, FALSE, FALSE, FALSE},
    {GST_H265_NAL_SLICE_RADL_N,     FALSE, FALSE, FALSE, FALSE, TRUE,  FALSE},
    {GST_H265_NAL_SLICE_RADL_R,     FALSE, FALSE, FALSE, FALSE, TRUE,  FALSE},
    {GST_H265_NAL_SLICE_RASL_N,     FALSE, FALSE, FALSE, FALSE, FALSE, TRUE },
    {GST_H265_NAL_SLICE_RASL_R,     FALSE, FALSE, FALSE, FALSE, FALSE, TRUE },
    /* 10 ~ 15: reserved non-irap sublayer nal */
    {GST_H265_NAL_SLICE_BLA_W_LP,   FALSE, TRUE,  TRUE,  FALSE, FALSE, FALSE},
    {GST_H265_NAL_SLICE_BLA_W_RADL, FALSE, TRUE,  TRUE,  FALSE, FALSE, FALSE},
    {GST_H265_NAL_SLICE_BLA_N_LP,   FALSE, TRUE,  TRUE,  FALSE, FALSE, FALSE},
    {GST_H265_NAL_SLICE_IDR_W_RADL, TRUE,  TRUE,  FALSE, FALSE, FALSE, FALSE},
    {GST_H265_NAL_SLICE_IDR_N_LP,   TRUE,  TRUE,  FALSE, FALSE, FALSE, FALSE},
    {GST_H265_NAL_SLICE_CRA_NUT,    FALSE, TRUE,  FALSE, TRUE,  FALSE, FALSE},
    /* 22 ~ 23: reserved irap nal */
    {(GstH265NalUnitType) 22,       FALSE, TRUE,  FALSE, FALSE, FALSE, FALSE},
    {(GstH265NalUnitType) 23,       FALSE, TRUE,  FALSE, FALSE, FALSE, FALSE},
  };
  /* *INDENT-ON* */

  for (i = 0; i < G_N_ELEMENTS (test_vector); i++) {
    assert_equals_int (GST_H265_IS_NAL_TYPE_IDR (test_vector[i].type),
        test_vector[i].is_idr);
    assert_equals_int (GST_H265_IS_NAL_TYPE_IRAP (test_vector[i].type),
        test_vector[i].is_irap);
    assert_equals_int (GST_H265_IS_NAL_TYPE_BLA (test_vector[i].type),
        test_vector[i].is_bla);
    assert_equals_int (GST_H265_IS_NAL_TYPE_CRA (test_vector[i].type),
        test_vector[i].is_cra);
    assert_equals_int (GST_H265_IS_NAL_TYPE_RADL (test_vector[i].type),
        test_vector[i].is_radl);
    assert_equals_int (GST_H265_IS_NAL_TYPE_RASL (test_vector[i].type),
        test_vector[i].is_rasl);
  }

  for (i = RESERVED_NON_IRAP_NAL_TYPE_MIN;
      i <= UNSPECIFIED_NON_VCL_NAL_TYPE_MAX; i++) {
    assert_equals_int (GST_H265_IS_NAL_TYPE_IDR (i), FALSE);
    assert_equals_int (GST_H265_IS_NAL_TYPE_IRAP (i), FALSE);
    assert_equals_int (GST_H265_IS_NAL_TYPE_BLA (i), FALSE);
    assert_equals_int (GST_H265_IS_NAL_TYPE_CRA (i), FALSE);
    assert_equals_int (GST_H265_IS_NAL_TYPE_RADL (i), FALSE);
    assert_equals_int (GST_H265_IS_NAL_TYPE_RASL (i), FALSE);
  }
}

GST_END_TEST;

static Suite *
h265parser_suite (void)
{
  Suite *s = suite_create ("H265 Parser library");

  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_h265_parse_slice_eos_slice_eob);
  tcase_add_test (tc_chain, test_h265_parse_pic_timing);
  tcase_add_test (tc_chain, test_h265_parse_slice_6bytes);
  tcase_add_test (tc_chain, test_h265_base_profiles);
  tcase_add_test (tc_chain, test_h265_base_profiles_compat);
  tcase_add_test (tc_chain, test_h265_format_range_profiles_exact_match);
  tcase_add_test (tc_chain, test_h265_format_range_profiles_partial_match);
  tcase_add_test (tc_chain, test_h265_parse_vps);
  tcase_add_test (tc_chain, test_h265_parse_pps);
  tcase_add_test (tc_chain, test_h265_nal_type_classification);

  return s;
}

GST_CHECK_MAIN (h265parser);
