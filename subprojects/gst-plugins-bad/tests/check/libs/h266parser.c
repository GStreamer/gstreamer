/* Gstreamer
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
#include <gst/codecparsers/gsth266parser.h>
#include <string.h>

unsigned char rasl_eos_rasl_eob[] = {
  0x00, 0x00, 0x00, 0x01, 0x00, 0x1e, 0x94, 0x05, 0x83, 0x45, 0x21, 0xef,
  0x7e, 0xb4, 0xa4, 0x9a, 0x31, 0xc0, 0xe4, 0x55, 0xfc,
  0x00, 0x00, 0x00, 0x01, 0x00, 0xae,
  0x00, 0x00, 0x00, 0x01, 0x00, 0x1e, 0x94, 0x05, 0x83, 0x45, 0x21, 0xef,
  0x7e, 0xb4, 0xa4, 0x9a, 0x31, 0xc0, 0xe4, 0x55, 0xfc,
  0x00, 0x00, 0x00, 0x01, 0x00, 0xb6
};

static const guint8 h266_vps_with_nonzero_max_layer_id[] = {
  0x00, 0x00, 0x00, 0x01, 0x00, 0x71, 0x10, 0x70, 0x00, 0x2d,
  0xc0, 0x08, 0x0e, 0x60, 0x22, 0x23, 0x80, 0x00, 0x00, 0x66,
  0x80, 0xb0, 0xa1, 0x50, 0x0d, 0x08, 0x0f, 0x15, 0x90
};

static const guint8 h266_sps[] = {
  0x00, 0x00, 0x00, 0x01, 0x00, 0x79, 0x00, 0x8d, 0x02, 0x43,
  0x80, 0x00, 0x00, 0xc0, 0x07, 0x81, 0x00, 0x21, 0xc8, 0xd4,
  0x00, 0xc5, 0xe8, 0x8d, 0xd1, 0x08, 0xd1, 0x0a, 0x4c, 0x8d,
  0xc2, 0x6c, 0xac, 0x60, 0x81, 0x04, 0xf0, 0x05, 0x48, 0x10,
  0x84, 0x22, 0x0c, 0x44, 0x45, 0x92, 0x22, 0xd4, 0x45, 0xe8,
  0xf5, 0x6a, 0x4b, 0xc9, 0x26, 0xa4, 0xb2, 0x44, 0x5a, 0x88,
  0xbc, 0x44, 0x9a, 0x88, 0x91, 0x49, 0x11, 0x26, 0x48, 0x89,
  0x75, 0x24, 0x45, 0x04, 0x2c, 0x44, 0x20, 0x64, 0x88, 0x35,
  0x20, 0x2a, 0xc2, 0x10, 0x85, 0x88, 0x04, 0x2c, 0x81, 0x02,
  0x21, 0x02, 0x05, 0x90, 0x81, 0x02, 0x44, 0x08, 0x34, 0x10,
  0x24, 0x82, 0x0e, 0x10, 0x64, 0x08, 0xb4, 0x20, 0x92, 0x10,
  0xe2, 0x1a, 0x12, 0xe4, 0x72, 0xa0, 0x85, 0x88, 0x04, 0x2c,
  0x81, 0x02, 0x21, 0x02, 0x0f, 0xff, 0xff, 0xaf, 0xc6, 0x20,
  0x40
};

static const guint8 h266_pps[] = {
  0x00, 0x00, 0x00, 0x01, 0x00, 0x81, 0x00, 0x00, 0x07, 0x81,
  0x00, 0x21, 0xc8, 0xa9, 0x00, 0x41, 0xec, 0x08,
};

static const guint8 h266_128x128_sps[] = {
  0x00, 0x00, 0x00, 0x01, 0x00, 0x79, 0x00, 0x0d, 0x02, 0x00, 0x80, 0x00, 0x40,
  0x20, 0x40, 0x40, 0x8d, 0x40, 0x7d, 0x11, 0xba, 0x21, 0x1a, 0x21, 0x49, 0x91,
  0xb8, 0x4d, 0x8a, 0x31, 0x50, 0xc1, 0xbe, 0x15, 0x71, 0xfc, 0x1f, 0x8c, 0x40,
  0x80
};

static const guint8 h266_128x128_pps[] = {
  0x00, 0x00, 0x00, 0x01, 0x00, 0x81, 0x00, 0x00, 0x20, 0x40, 0x40, 0x8a, 0x42,
  0x00, 0x34, 0x7b, 0x02
};

static const guint8 h266_128x128_slice_idr_n_lp[] = {
  0x00, 0x00, 0x00, 0x01, 0x00, 0x41, 0xc4, 0x02, 0x53, 0xe0, 0x0e, 0x83, 0xf7,
  0xff, 0x6a, 0x2a, 0xc6, 0x51, 0x5f, 0x98, 0x54, 0x19, 0x75, 0xa7, 0x7f, 0x99,
  0x29, 0x76, 0xc9, 0x98, 0x75, 0xfd, 0xf4, 0x7d, 0x85, 0x05, 0x4f, 0xee, 0x38,
  0x94, 0x57, 0x8d, 0x83, 0x84, 0x49, 0xfd, 0x77, 0xa7, 0x9f, 0x13, 0xfb, 0x78,
  0xaf, 0xce, 0x4a, 0xfe, 0x5b, 0xfc, 0xe2, 0xaf, 0xde, 0x3c, 0xbb, 0xd7, 0xa8,
  0x18, 0x70, 0x66, 0xbc, 0x46, 0xb7, 0xa9, 0xfa, 0xc8, 0xef, 0x1a, 0x47, 0x74,
  0x98, 0xd3, 0x54, 0x58, 0xcd, 0x1c, 0x72, 0xc1, 0xc0, 0x5d, 0xc7, 0x73, 0x3c,
  0xed, 0xb0, 0x8b, 0xd3, 0xd9, 0x1b, 0x82, 0x43, 0x03, 0x9a, 0x8f, 0xfd, 0x87,
  0x73, 0x48, 0x1c, 0x08, 0xb6, 0xf3, 0xcc, 0xdc, 0x2f, 0x46, 0xe1, 0x0f, 0xd2,
  0xd2, 0xb9, 0xa5, 0x67, 0xe3, 0xe0, 0x29, 0x2f, 0xcd, 0x3f, 0xea, 0xdf, 0xe0
};

GST_START_TEST (test_h266_parse_rasl_eos_rasl_eob)
{
  GstH266ParserResult res;
  GstH266NalUnit nalu;
  GstH266Parser *parser = gst_h266_parser_new ();
  const guint8 *buf = rasl_eos_rasl_eob;
  guint n, buf_size = sizeof (rasl_eos_rasl_eob);

  res = gst_h266_parser_identify_nalu (parser, buf, 0, buf_size, &nalu);

  assert_equals_int (res, GST_H266_PARSER_OK);
  assert_equals_int (nalu.type, GST_H266_NAL_SLICE_RASL);
  assert_equals_int (nalu.size, 17);

  n = nalu.offset + nalu.size;
  buf += n;
  buf_size -= n;

  res = gst_h266_parser_identify_nalu (parser, buf, 0, buf_size, &nalu);

  assert_equals_int (res, GST_H266_PARSER_OK);
  assert_equals_int (nalu.type, GST_H266_NAL_EOS);
  assert_equals_int (nalu.size, 2);

  n = nalu.offset + nalu.size;
  buf += n;
  buf_size -= n;

  res = gst_h266_parser_identify_nalu (parser, buf, 0, buf_size, &nalu);

  assert_equals_int (res, GST_H266_PARSER_OK);
  assert_equals_int (nalu.type, GST_H266_NAL_SLICE_RASL);
  assert_equals_int (nalu.size, 17);

  n = nalu.offset + nalu.size;
  buf += n;
  buf_size -= n;

  res = gst_h266_parser_identify_nalu (parser, buf, 0, buf_size, &nalu);

  assert_equals_int (res, GST_H266_PARSER_OK);
  assert_equals_int (nalu.type, GST_H266_NAL_EOB);
  assert_equals_int (nalu.size, 2);

  gst_h266_parser_free (parser);
}

GST_END_TEST;

GST_START_TEST (test_h266_parse_vps)
{
  GstH266Parser *parser;
  GstH266NalUnit nalu;
  GstH266ParserResult res;
  GstH266VPS *vps;

  vps = g_malloc0 (sizeof (GstH266VPS));

  parser = gst_h266_parser_new ();

  res = gst_h266_parser_identify_nalu_unchecked (parser,
      h266_vps_with_nonzero_max_layer_id, 0,
      sizeof (h266_vps_with_nonzero_max_layer_id), &nalu);

  assert_equals_int (res, GST_H266_PARSER_OK);
  assert_equals_int (nalu.type, GST_H266_NAL_VPS);

  res = gst_h266_parser_parse_vps (parser, &nalu, vps);
  assert_equals_int (res, GST_H266_PARSER_OK);

  assert_equals_int (vps->vps_id, 1);
  assert_equals_int (vps->max_layers_minus1, 1);
  assert_equals_int (vps->max_sublayers_minus1, 6);
  assert_equals_int (vps->default_ptl_dpb_hrd_max_tid_flag, 0);
  assert_equals_int (vps->profile_tier_level[0].profile_idc, 17);
  assert_equals_int (vps->all_independent_layers_flag, 0);
  assert_equals_int (vps->each_layer_is_an_ols_flag, 0);
  assert_equals_int (vps->ols_mode_idc, 2);
  assert_equals_int (vps->num_output_layer_sets_minus2, 0);
  assert_equals_int (vps->num_ptls_minus1, 1);
  assert_equals_int (vps->profile_tier_level[0].tier_flag, 0);
  assert_equals_int (vps->profile_tier_level[0].level_idc, 35);
  assert_equals_int (vps->profile_tier_level[1].level_idc, 102);
  assert_equals_int (vps->num_dpb_params_minus1, 0);
  assert_equals_int (vps->sublayer_dpb_params_present_flag, 0);
  assert_equals_int (vps->dpb_max_tid[0], 6);
  assert_equals_int (vps->dpb[0].max_dec_pic_buffering_minus1[6], 9);
  assert_equals_int (vps->dpb[0].max_num_reorder_pics[6], 9);
  assert_equals_int (vps->dpb[0].max_latency_increase_plus1[6], 0);
  assert_equals_int (vps->ols_dpb_pic_width[0], 416);
  assert_equals_int (vps->ols_dpb_pic_height[0], 240);
  assert_equals_int (vps->ols_dpb_chroma_format[0], 1);
  assert_equals_int (vps->ols_dpb_bitdepth_minus8[0], 2);
  assert_equals_int (vps->timing_hrd_params_present_flag, 0);

  assert_equals_int (vps->extension_flag, 0);
  gst_h266_parser_free (parser);
  g_free (vps);
}

GST_END_TEST;

GST_START_TEST (test_h266_parse_sps)
{
  GstH266Parser *parser;
  GstH266NalUnit nalu;
  GstH266ParserResult res;
  GstH266SPS sps;

  parser = gst_h266_parser_new ();

  res = gst_h266_parser_identify_nalu_unchecked (parser,
      h266_sps, 0, sizeof (h266_sps), &nalu);

  assert_equals_int (res, GST_H266_PARSER_OK);
  assert_equals_int (nalu.type, GST_H266_NAL_SPS);

  res = gst_h266_parser_parse_sps (parser, &nalu, &sps);
  assert_equals_int (res, GST_H266_PARSER_OK);

  assert_equals_int (sps.sps_id, 0);
  assert_equals_int (sps.max_sublayers_minus1, 4);
  assert_equals_int (sps.log2_min_luma_coding_block_size_minus2, 0);
  assert_equals_int (sps.log2_diff_min_qt_min_cb_intra_slice_chroma, 1);
  assert_equals_int (sps.max_mtt_hierarchy_depth_intra_slice_luma, 3);
  assert_equals_int (sps.log2_diff_max_bt_min_qt_intra_slice_luma, 2);
  assert_equals_int (sps.log2_diff_max_tt_min_qt_intra_slice_luma, 2);
  assert_equals_int (sps.qtbtt_dual_tree_intra_flag, 1);

  assert_equals_int (sps.qp_table_start_minus26[0], -9);
  assert_equals_int (sps.lmcs_enabled_flag, 1);
  assert_equals_int (sps.weighted_pred_flag, 0);
  assert_equals_int (sps.rpl1_same_as_rpl0_flag, 0);
  assert_equals_int (sps.num_ref_pic_lists[0], 20);
  assert_equals_int (sps.ref_pic_list_struct[0][0].num_ref_entries, 3);
  assert_equals_int (sps.ref_pic_list_struct[0][0].abs_delta_poc_st[0], 15);
  assert_equals_int (sps.ref_pic_list_struct[0][0].abs_delta_poc_st[1], 15);
  assert_equals_int (sps.ref_pic_list_struct[0][0].abs_delta_poc_st[2], 7);

  assert_equals_int (sps.log2_parallel_merge_level_minus2, 0);
  assert_equals_int (sps.chroma_format_idc, 1);
  assert_equals_int (sps.cclm_enabled_flag, 1);
  assert_equals_int (sps.chroma_horizontal_collocated_flag, 1);
  assert_equals_int (sps.ibc_enabled_flag, 0);
  assert_equals_int (sps.ladf_enabled_flag, 0);
  assert_equals_int (sps.explicit_scaling_list_enabled_flag, 0);
  assert_equals_int (sps.dep_quant_enabled_flag, 1);

  assert_equals_int (sps.vui_parameters_present_flag, 0);
  assert_equals_int (sps.extension_flag, 0);
  gst_h266_parser_free (parser);
}

GST_END_TEST;

GST_START_TEST (test_h266_parse_pps)
{
  GstH266Parser *parser;
  GstH266NalUnit nalu;
  GstH266ParserResult res;
  GstH266PPS pps;

  parser = gst_h266_parser_new ();

  /*init sps[0] to avoid return error in gst_h266_parser_parse_pps */
  parser->sps[0].valid = TRUE;
  parser->sps[0].pic_width_max_in_luma_samples = 1920;
  parser->sps[0].pic_height_max_in_luma_samples = 1080;

  res = gst_h266_parser_identify_nalu_unchecked (parser,
      h266_pps, 0, sizeof (h266_pps), &nalu);

  assert_equals_int (res, GST_H266_PARSER_OK);
  assert_equals_int (nalu.type, GST_H266_NAL_PPS);

  res = gst_h266_parser_parse_pps (parser, &nalu, &pps);

  assert_equals_int (res, GST_H266_PARSER_OK);

  assert_equals_int (pps.pps_id, 0);
  assert_equals_int (pps.sps_id, 0);
  assert_equals_int (pps.mixed_nalu_types_in_pic_flag, 0);
  assert_equals_int (pps.pic_width_in_luma_samples, 1920);
  assert_equals_int (pps.pic_height_in_luma_samples, 1080);
  assert_equals_int (pps.conformance_window_flag, 0);
  assert_equals_int (pps.cabac_init_present_flag, 1);
  assert_equals_int (pps.rpl1_idx_present_flag, 0);
  assert_equals_int (pps.init_qp_minus26, 8);
  assert_equals_int (pps.cu_qp_delta_enabled_flag, 0);
  assert_equals_int (pps.chroma_tool_offsets_present_flag, 1);
  assert_equals_int (pps.joint_cbcr_qp_offset_present_flag, 1);
  assert_equals_int (pps.joint_cbcr_qp_offset_value, -1);

  gst_h266_parser_free (parser);
}

GST_END_TEST;

GST_START_TEST (test_h266_parse_slice_hdr)
{
  GstH266Parser *parser;
  GstH266NalUnit nalu;
  GstH266ParserResult res;
  GstH266SPS sps;
  GstH266PPS pps;
  GstH266SliceHdr sh;

  parser = gst_h266_parser_new ();

  res = gst_h266_parser_identify_nalu_unchecked (parser,
      h266_128x128_sps, 0, sizeof (h266_128x128_sps), &nalu);
  assert_equals_int (res, GST_H266_PARSER_OK);
  assert_equals_int (nalu.type, GST_H266_NAL_SPS);
  res = gst_h266_parser_parse_sps (parser, &nalu, &sps);
  assert_equals_int (res, GST_H266_PARSER_OK);

  res = gst_h266_parser_identify_nalu_unchecked (parser,
      h266_128x128_pps, 0, sizeof (h266_128x128_pps), &nalu);
  assert_equals_int (res, GST_H266_PARSER_OK);
  assert_equals_int (nalu.type, GST_H266_NAL_PPS);
  res = gst_h266_parser_parse_pps (parser, &nalu, &pps);
  assert_equals_int (res, GST_H266_PARSER_OK);

  res = gst_h266_parser_identify_nalu_unchecked (parser,
      h266_128x128_slice_idr_n_lp, 0, sizeof (h266_128x128_slice_idr_n_lp),
      &nalu);
  assert_equals_int (res, GST_H266_PARSER_OK);
  assert_equals_int (nalu.type, GST_H266_NAL_SLICE_IDR_N_LP);
  res = gst_h266_parser_parse_slice_hdr (parser, &nalu, &sh);
  assert_equals_int (res, GST_H266_PARSER_OK);

  assert_equals_int (sh.picture_header_in_slice_header_flag, 1);
  assert_equals_int (sh.picture_header.gdr_or_irap_pic_flag, 1);
  assert_equals_int (sh.picture_header.non_ref_pic_flag, 0);
  assert_equals_int (sh.picture_header.gdr_pic_flag, 0);
  assert_equals_int (sh.picture_header.inter_slice_allowed_flag, 0);
  assert_equals_int (sh.picture_header.pps_id, 0);
  assert_equals_int (sh.picture_header.pic_order_cnt_lsb, 0);
  assert_equals_int (sh.picture_header.lmcs_enabled_flag, 1);
  assert_equals_int (sh.picture_header.lmcs_aps_id, 0);
  assert_equals_int (sh.picture_header.chroma_residual_scale_flag, 1);
  assert_equals_int (sh.picture_header.partition_constraints_override_flag, 0);
  assert_equals_int (sh.picture_header.joint_cbcr_sign_flag, 1);
  assert_equals_int (sh.no_output_of_prior_pics_flag, 0);
  assert_equals_int (sh.alf_enabled_flag, 0);
  assert_equals_int (sh.qp_delta, 0);
  assert_equals_int (sh.sao_luma_used_flag, 1);
  assert_equals_int (sh.sao_chroma_used_flag, 1);
  assert_equals_int (sh.dep_quant_used_flag, 1);

  gst_h266_parser_free (parser);
}

GST_END_TEST;

GST_START_TEST (test_h266_parse_decoder_config_record)
{
  GstH266Parser *parser;
  GstH266ParserResult res;
  GstH266DecoderConfigRecord *config;
  GstH266DecoderConfigRecordNalUnitArray *nalu_array;
  GstH266NalUnit *nalu;

  // vvcC data from standard ITU stream SPATSCAL_A_4.bit after muxing it as MP4 with FFmpeg 7.1:
  // ffmpeg -i SPATSCAL_A_4.bit -c:v copy SPATSCAL_A_4.mp4
  static const guint8 vvcc_data[] = {
    0xFF, 0x00, 0x75, 0x5F, 0x01, 0x22, 0x66, 0xC0, 0x00, 0x00, 0x00, 0xB0,
    0x00, 0x90, 0x00, 0x00, 0x03, 0x8E, 0x00, 0x01, 0x00, 0x1C, 0x00, 0x71,
    0x10, 0xB4, 0x03, 0xC7, 0x23, 0x00, 0x00, 0x22, 0x66, 0xC0, 0x00, 0x00,
    0x41, 0x42, 0xA3, 0xC7, 0xC0, 0x58, 0x80, 0xC1, 0x58, 0x05, 0x24, 0x02,
    0x32, 0xB2, 0x8F, 0x00, 0x01, 0x00, 0x64, 0x00, 0x79, 0x01, 0x0D, 0x22,
    0x66, 0xC0, 0x00, 0x40, 0x2C, 0x40, 0x48, 0x8D, 0x40, 0x17, 0xC8, 0xB9,
    0x12, 0x91, 0x35, 0x91, 0x98, 0x4D, 0x95, 0x8C, 0x10, 0x20, 0x9E, 0x08,
    0x68, 0xB8, 0x88, 0x88, 0x89, 0x7C, 0x44, 0x44, 0xBA, 0x88, 0x88, 0x97,
    0x71, 0x11, 0x12, 0xE4, 0x88, 0x88, 0x97, 0x2C, 0x44, 0x44, 0xB9, 0xA2,
    0x22, 0x25, 0xCF, 0x11, 0x11, 0x5B, 0xF2, 0x7E, 0x5F, 0xF2, 0xFE, 0xA5,
    0xFD, 0xCB, 0xF9, 0x25, 0xFC, 0xB2, 0xFE, 0x69, 0x7F, 0x3C, 0xBF, 0x88,
    0x97, 0xD4, 0x44, 0xBE, 0xE2, 0x25, 0xF2, 0x44, 0x4B, 0xE5, 0x88, 0x97,
    0xCD, 0x11, 0x2F, 0x9E, 0x22, 0xE3, 0xFB, 0xEB, 0xB1, 0x88, 0x10, 0x90,
    0x00, 0x01, 0x00, 0x10, 0x00, 0x81, 0x00, 0x00, 0x2C, 0x40, 0x48, 0x8A,
    0x42, 0x00, 0x97, 0xB2, 0x16, 0x59, 0x62, 0x00
  };

  parser = gst_h266_parser_new ();

  res = gst_h266_parser_parse_decoder_config_record (parser, vvcc_data,
      sizeof (vvcc_data), &config);

  assert_equals_int (res, GST_H266_PARSER_OK);

  assert_equals_int (config->length_size_minus_one, 3);
  assert_equals_int (config->ptl_present_flag, 1);
  assert_equals_int (config->ols_idx, 0);
  assert_equals_int (config->num_sublayers, 7);
  assert_equals_int (config->constant_frame_rate, 1);
  assert_equals_int (config->chroma_format_idc, 1);
  assert_equals_int (config->bit_depth_minus8, 2);

  assert_equals_int (config->native_ptl.num_bytes_constraint_info, 1);
  assert_equals_int (config->native_ptl.general_profile_idc,
      GST_H266_PROFILE_MULTILAYER_MAIN_10);
  assert_equals_int (config->native_ptl.general_tier_flag, 0);
  assert_equals_int (config->native_ptl.general_level_idc, GST_H266_LEVEL_L6_2);
  assert_equals_int (config->native_ptl.ptl_frame_only_constraint_flag, 1);
  assert_equals_int (config->native_ptl.ptl_multilayer_enabled_flag, 1);
  assert_equals_int (config->native_ptl.general_constraint_info[0], 0);

  assert_equals_int (config->max_picture_width, 176);
  assert_equals_int (config->max_picture_height, 144);
  assert_equals_int (config->avg_frame_rate, 0);

  assert_equals_int (config->nalu_array->len, 3);

  nalu_array =
      &g_array_index (config->nalu_array,
      GstH266DecoderConfigRecordNalUnitArray, 0);
  assert_equals_int (nalu_array->nal_unit_type, GST_H266_NAL_VPS);
  assert_equals_int (nalu_array->nalu->len, 1);
  nalu = &g_array_index (nalu_array->nalu, GstH266NalUnit, 0);
  assert_equals_int (nalu->type, GST_H266_NAL_VPS);
  assert_equals_int (nalu->size, 28);

  nalu_array =
      &g_array_index (config->nalu_array,
      GstH266DecoderConfigRecordNalUnitArray, 1);
  assert_equals_int (nalu_array->nal_unit_type, GST_H266_NAL_SPS);
  assert_equals_int (nalu_array->nalu->len, 1);
  nalu = &g_array_index (nalu_array->nalu, GstH266NalUnit, 0);
  assert_equals_int (nalu->type, GST_H266_NAL_SPS);
  assert_equals_int (nalu->size, 100);

  nalu_array =
      &g_array_index (config->nalu_array,
      GstH266DecoderConfigRecordNalUnitArray, 2);
  assert_equals_int (nalu_array->nal_unit_type, GST_H266_NAL_PPS);
  assert_equals_int (nalu_array->nalu->len, 1);
  nalu = &g_array_index (nalu_array->nalu, GstH266NalUnit, 0);
  assert_equals_int (nalu->type, GST_H266_NAL_PPS);
  assert_equals_int (nalu->size, 16);

  gst_h266_decoder_config_record_free (config);
  gst_h266_parser_free (parser);
}

GST_END_TEST;

GST_START_TEST (test_h266_parse_decoder_config_record_gci)
{
  GstH266Parser *parser;
  GstH266ParserResult res;
  GstH266DecoderConfigRecord *config;
  GstH266DecoderConfigRecordNalUnitArray *nalu_array;
  GstH266NalUnit *nalu;
  gint i;

  // vvcC data from standard ITU stream LMCS_C_1.bit after muxing it as MP4 with FFmpeg 7.1:
  // ffmpeg -i LMCS_C_1.bit -c:v copy LMCS_C_1.mp4
  static const guint8 vvcc_data[] = {
    0xFF, 0x00, 0x65, 0x5F, 0x09, 0x02, 0x43, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x07, 0x80, 0x04, 0x38, 0x00, 0x00,
    0x02, 0x8F, 0x00, 0x01, 0x01, 0x0E, 0x00, 0x79, 0x00, 0xAD, 0x02, 0x43,
    0xA0, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x01,
    0x00, 0x00, 0x03, 0x00, 0x00, 0xC0, 0x07, 0x81, 0x00, 0x21, 0xC8, 0xD4,
    0x00, 0xE6, 0xE8, 0x8D, 0xD1, 0x08, 0xD1, 0x0A, 0x4C, 0x8D, 0x83, 0x65,
    0x38, 0xF0, 0x80, 0x84, 0x8A, 0x20, 0x58, 0x40, 0x36, 0x53, 0x8F, 0x08,
    0x85, 0xC8, 0x9A, 0x14, 0x34, 0x3A, 0x41, 0x28, 0x28, 0x21, 0x10, 0x5A,
    0xE0, 0x02, 0x62, 0x02, 0x08, 0x42, 0x10, 0xB0, 0x84, 0x21, 0x62, 0x21,
    0x0B, 0x24, 0x21, 0x6A, 0x10, 0xBD, 0x1E, 0xAD, 0x49, 0x79, 0x24, 0xD4,
    0x96, 0x48, 0x8B, 0x51, 0x17, 0x88, 0x93, 0x51, 0x12, 0x29, 0x22, 0x24,
    0xC9, 0x11, 0x2E, 0xA4, 0x88, 0xB1, 0x10, 0x85, 0x92, 0x10, 0xB5, 0x08,
    0x5E, 0x10, 0x93, 0x50, 0x84, 0x8A, 0x48, 0x42, 0x4C, 0x90, 0x84, 0xBA,
    0x92, 0x10, 0x90, 0x91, 0x10, 0x84, 0x8A, 0x22, 0x10, 0x93, 0x11, 0x08,
    0x4B, 0xA8, 0x88, 0x42, 0x45, 0x19, 0x08, 0x49, 0x8C, 0x84, 0x25, 0xD4,
    0x64, 0x21, 0x40, 0x82, 0xC2, 0x10, 0x40, 0x62, 0x21, 0x03, 0x24, 0x41,
    0xA9, 0x00, 0x99, 0x82, 0x08, 0x42, 0xC2, 0x00, 0x41, 0x62, 0x01, 0x01,
    0x08, 0x10, 0x08, 0x0A, 0x84, 0x08, 0x04, 0x06, 0x90, 0x81, 0x00, 0x80,
    0xB1, 0x02, 0x01, 0x01, 0x10, 0x40, 0x20, 0x2C, 0x82, 0x01, 0x01, 0x21,
    0x00, 0x81, 0x90, 0x10, 0x11, 0x08, 0x08, 0x0B, 0x21, 0x01, 0x01, 0x22,
    0x02, 0x06, 0x81, 0x01, 0x24, 0x08, 0x1C, 0x10, 0x31, 0x00, 0x85, 0x90,
    0x20, 0x44, 0x20, 0x40, 0xB2, 0x10, 0x20, 0x48, 0x81, 0x06, 0x82, 0x04,
    0x90, 0x41, 0xC2, 0x0C, 0x81, 0x16, 0x84, 0x12, 0x42, 0x1C, 0x43, 0x42,
    0x5C, 0x8E, 0x54, 0x08, 0x2C, 0x20, 0x04, 0x16, 0x20, 0x10, 0x10, 0x81,
    0x00, 0x80, 0xA8, 0x40, 0x80, 0x40, 0xFF, 0xFF, 0xFA, 0xFE, 0x88, 0x10,
    0x90, 0x00, 0x01, 0x00, 0x0F, 0x00, 0x81, 0x00, 0x00, 0x07, 0x81, 0x00,
    0x21, 0xC8, 0xA9, 0x00, 0xC7, 0xB0, 0x20, 0x00
  };

  parser = gst_h266_parser_new ();
  res = gst_h266_parser_parse_decoder_config_record (parser, vvcc_data,
      sizeof (vvcc_data), &config);

  assert_equals_int (res, GST_H266_PARSER_OK);

  assert_equals_int (config->length_size_minus_one, 3);
  assert_equals_int (config->ptl_present_flag, 1);
  assert_equals_int (config->ols_idx, 0);
  assert_equals_int (config->num_sublayers, 6);
  assert_equals_int (config->constant_frame_rate, 1);
  assert_equals_int (config->chroma_format_idc, 1);
  assert_equals_int (config->bit_depth_minus8, 2);

  assert_equals_int (config->native_ptl.num_bytes_constraint_info, 9);
  assert_equals_int (config->native_ptl.general_profile_idc,
      GST_H266_PROFILE_MAIN_10);
  assert_equals_int (config->native_ptl.general_tier_flag, 0);
  assert_equals_int (config->native_ptl.general_level_idc, GST_H266_LEVEL_L4_1);
  assert_equals_int (config->native_ptl.ptl_frame_only_constraint_flag, 1);
  assert_equals_int (config->native_ptl.ptl_multilayer_enabled_flag, 0);
  for (i = 0; i < 8; i++)
    assert_equals_int (config->native_ptl.general_constraint_info[i], 0);
  assert_equals_int (config->native_ptl.general_constraint_info[8], 4);

  assert_equals_int (config->max_picture_width, 1920);
  assert_equals_int (config->max_picture_height, 1080);
  assert_equals_int (config->avg_frame_rate, 0);

  assert_equals_int (config->nalu_array->len, 2);

  nalu_array =
      &g_array_index (config->nalu_array,
      GstH266DecoderConfigRecordNalUnitArray, 0);
  assert_equals_int (nalu_array->nal_unit_type, GST_H266_NAL_SPS);
  assert_equals_int (nalu_array->nalu->len, 1);
  nalu = &g_array_index (nalu_array->nalu, GstH266NalUnit, 0);
  assert_equals_int (nalu->type, GST_H266_NAL_SPS);
  assert_equals_int (nalu->size, 270);

  nalu_array =
      &g_array_index (config->nalu_array,
      GstH266DecoderConfigRecordNalUnitArray, 1);
  assert_equals_int (nalu_array->nal_unit_type, GST_H266_NAL_PPS);
  assert_equals_int (nalu_array->nalu->len, 1);
  nalu = &g_array_index (nalu_array->nalu, GstH266NalUnit, 0);
  assert_equals_int (nalu->type, GST_H266_NAL_PPS);
  assert_equals_int (nalu->size, 15);


  gst_h266_decoder_config_record_free (config);
  gst_h266_parser_free (parser);
}

GST_END_TEST;


static Suite *
h266parser_suite (void)
{
  Suite *s = suite_create ("H266 Parser library");

  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_h266_parse_rasl_eos_rasl_eob);
  tcase_add_test (tc_chain, test_h266_parse_vps);
  tcase_add_test (tc_chain, test_h266_parse_sps);
  tcase_add_test (tc_chain, test_h266_parse_pps);
  tcase_add_test (tc_chain, test_h266_parse_slice_hdr);
  tcase_add_test (tc_chain, test_h266_parse_decoder_config_record);
  tcase_add_test (tc_chain, test_h266_parse_decoder_config_record_gci);

  return s;
}

GST_CHECK_MAIN (h266parser);
