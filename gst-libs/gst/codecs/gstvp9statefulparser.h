/* GStreamer
 * Copyright (C) 2021 Seungha Yang <seungha@centricular.com>
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

#ifndef __GST_VP9_STATEFUL_PARSER_H__
#define __GST_VP9_STATEFUL_PARSER_H__

#include <gst/codecs/codecs-prelude.h>
#include <gst/codecparsers/gstvp9parser.h>

G_BEGIN_DECLS

typedef struct _GstVp9StatefulParser        GstVp9StatefulParser;
typedef struct _GstVp9LoopFilterParams      GstVp9LoopFilterParams;
typedef struct _GstVp9QuantizationParams    GstVp9QuantizationParams;
typedef struct _GstVp9SegmentationParams    GstVp9SegmentationParams;
typedef struct _GstVp9FrameHeader           GstVp9FrameHeader;

/**
 * GST_VP9_SEG_LVL_ALT_Q:
 *
 * Index for quantizer segment feature
 *
 * Since: 1.20
 */
#define GST_VP9_SEG_LVL_ALT_Q 0

/**
 * GST_VP9_SEG_LVL_ALT_L:
 *
 * Index for loop filter segment feature
 *
 * Since: 1.20
 */
#define GST_VP9_SEG_LVL_ALT_L 1

/**
 * GST_VP9_SEG_LVL_REF_FRAME:
 *
 * Index for reference frame segment feature
 *
 * Since: 1.20
 */
#define GST_VP9_SEG_LVL_REF_FRAME 2

/**
 * GST_VP9_SEG_SEG_LVL_SKIP:
 *
 * Index for skip segment feature
 *
 * Since: 1.20
 */
#define GST_VP9_SEG_SEG_LVL_SKIP 3

/**
 * GST_VP9_SEG_LVL_MAX:
 *
 * Number of segment features
 *
 * Since: 1.20
 */
#define GST_VP9_SEG_LVL_MAX 4

/**
 * GstVp9LoopFilterParams:
 * @loop_filter_level: indicates the loop filter strength
 * @loop_filter_sharpness: indicates the sharpness level
 * @loop_filter_delta_enabled: equal to 1 means that the filter level depends
 *   on the mode and reference frame used to predict a block
 * @loop_filter_delta_update: equal to 1 means that the bitstream contains
 *   additional syntax elements that specify which mode and reference frame
 *   deltas are to be updated
 * @update_ref_delta: equal to 1 means that the bitstream contains the syntax
 *   element loop_filter_ref_delta
 * @loop_filter_ref_deltas: contains the adjustment needed for the filter level
 *   based on the chosen reference frame
 * @update_mode_delta: equal to 1 means that the bitstream contains the syntax
 *   element loop_filter_mode_deltas
 * @loop_filter_mode_deltas: contains the adjustment needed for the filter level
 *   based on the chosen mode
 *
 * Loop filter params. See "6.2.8 Loop filter params syntax" and
 * "7.2.8 Loop filter semantics".
 *
 * If syntax elements for @update_ref_delta
 * and/or @loop_filter_mode_deltas are not present in bitstream,
 * parser will fill @loop_filter_ref_deltas and @loop_filter_mode_deltas values
 * by using previously parsed values.
 *
 * Since: 1.20
 */
struct _GstVp9LoopFilterParams
{
  guint8 loop_filter_level;
  guint8 loop_filter_sharpness;
  guint8 loop_filter_delta_enabled;
  guint8 loop_filter_delta_update;

  guint8 update_ref_delta[GST_VP9_MAX_REF_LF_DELTAS];
  gint8 loop_filter_ref_deltas[GST_VP9_MAX_REF_LF_DELTAS];

  guint8 update_mode_delta[GST_VP9_MAX_MODE_LF_DELTAS];
  gint8 loop_filter_mode_deltas[GST_VP9_MAX_MODE_LF_DELTAS];
};

/**
 * GstVp9QuantizationParams:
 * @base_q_idx: indicates the base frame qindex. This is used for Y AC
 *   coefficients and as the base value for the other quantizers
 * @delta_q_y_dc: indicates the Y DC quantizer relative to base_q_idx
 * @delta_q_uv_dc: indicates the UV DC quantizer relative to base_q_idx
 * @delta_q_uv_ac: indicates the UV AC quantizer relative to base_q_idx
 *
 * Since: 1.20
 */
struct _GstVp9QuantizationParams
{
  guint8 base_q_idx;
  gint8 delta_q_y_dc;
  gint8 delta_q_uv_dc;
  gint8 delta_q_uv_ac;
};

/**
 * GstVp9SegmentationParams:
 * @segmentation_enabled: equal to 1 indicates that this frame makes use of the
 *   segmentation tool
 * @segmentation_update_map: equal to 1 indicates that the segmentation map
 *   should be updated during the decoding of this frame
 * @segmentation_tree_probs: specify the probability values to be used when
 *   decoding segment_id
 * @segmentation_pred_prob: specify the probability values to be used when
 *    decoding seg_id_predicted
 * @segmentation_temporal_update: equal to 1 indicates that the updates to
 *   the segmentation map are coded relative to the existing segmentation map
 * @segmentation_update_data: equal to 1 indicates that new parameters are
 *   about to be specified for each segment
 * @segmentation_abs_or_delta_update: equal to 0 indicates that the segmentation
 *   parameters represent adjustments relative to the standard values.
 *   equal to 1 indicates that the segmentation parameters represent the actual
 *   values to be used
 * @feature_enabled: indicates whether feature is enabled or not
 * @feature_data: segmentation feature data
 *
 * See "6.2.11 Segmentation params syntax" and
 * "7.2.10 Segmentation params syntax". When @segmentation_update_data is equal
 * to zero, parser will fill @feature_enabled and by @feature_data
 * using previously parsed values.
 *
 * Since: 1.20
 */
struct _GstVp9SegmentationParams
{
  guint8 segmentation_enabled;
  guint8 segmentation_update_map;
  guint8 segmentation_tree_probs[GST_VP9_SEG_TREE_PROBS];
  guint8 segmentation_pred_prob[GST_VP9_PREDICTION_PROBS];
  guint8 segmentation_temporal_update;

  guint8 segmentation_update_data;
  guint8 segmentation_abs_or_delta_update;

  guint8 feature_enabled[GST_VP9_MAX_SEGMENTS][GST_VP9_SEG_LVL_MAX];
  gint16 feature_data[GST_VP9_MAX_SEGMENTS][GST_VP9_SEG_LVL_MAX];
};

/**
 * GstVp9FrameHeader:
 * @profile: encoded profile
 * @bit_depth: encoded bit depth
 * @subsampling_x: specify the chroma subsampling format for x coordinate
 * @subsampling_y: specify the chroma subsampling format for y coordinate
 * @color_space: specifies the color space of the stream
 * @color_range: specifies the black level and range of the luma and chroma
 *   signals
 * @show_existing_frame: equal to 1, indicates the frame indexed by
 *   frame_to_show_map_idx is to be displayed
 * @frame_to_show_map_idx: specifies the frame to be displayed.
 *   It is only available if show_existing_frame is 1
 * @frame_type: equal to 0 indicates that the current frame is a key frame
 * @show_frame: indicate whether it is a displayable frame or not
 * @error_resilient_mode: equal to 1 indicates that error resilient mode is
 *   enabled
 * @width: coded frame width
 * @height: coded frame height
 * @render_and_frame_size_different: equal to 0 means that the render width and
 *   height are inferred from the frame width and height
 * @render_width: render width of the frame
 * @render_height: render width of the frame
 * @intra_only: equal to 1 indicates that the frame is an intra-only frame
 * @reset_frame_context: specifies whether the frame context should be reset to
 *   default values
 * @refresh_frame_flags: contains a bitmask that specifies which reference frame
 *   slots will be updated with the current frame after it is decoded
 * @ref_frame_idx: specifies which reference frames are used by inter frames
 * @ref_frame_sign_bias: specifies the intended direction of the motion vector
 *   in time for each reference frame. A sign bias equal to 0 indicates that
 *   the reference frame is a backwards reference
 * @allow_high_precision_mv: equal to 0 specifies that motion vectors are
 *   specified to quarter pel precision
 * @interpolation_filter: specifies the filter selection used for performing
 *   inter prediction
 * @refresh_frame_context: equal to 1 indicates that the probabilities computed
 *   for this frame
 * @frame_parallel_decoding_mode: equal to 1 indicates that parallel decoding
 *   mode is enabled
 * @frame_context_idx: indicates the frame context to use
 * @loop_filter_params: a #GstVp9LoopFilterParams
 * @quantization_params: a #GstVp9QuantizationParams
 * @segmentation_params: a #GstVp9SegmentationParams
 * @tile_cols_log2: specifies the base 2 logarithm of the width of each tile
 * @tile_rows_log2: specifies the base 2 logarithm of the height of each tile
 * @lossless_flag: lossless mode decode
 * @frame_header_length_in_bytes: length of uncompressed header
 *
 * Since: 1.20
 */
struct _GstVp9FrameHeader
{
  guint8 profile;
  guint8 bit_depth;
  guint8 subsampling_x;
  guint8 subsampling_y;
  guint8 color_space;
  guint8 color_range;
  guint8 show_existing_frame;
  guint8 frame_to_show_map_idx;
  guint8 frame_type;
  guint8 show_frame;
  guint8 error_resilient_mode;
  guint32 width;
  guint32 height;
  guint8 render_and_frame_size_different;
  guint32 render_width;
  guint32 render_height;
  guint8 intra_only;
  guint8 reset_frame_context;
  guint8 refresh_frame_flags;
  guint8 ref_frame_idx[GST_VP9_REFS_PER_FRAME];
  guint8 ref_frame_sign_bias[4];
  guint8 allow_high_precision_mv;
  guint8 interpolation_filter;

  guint8 refresh_frame_context;
  guint8 frame_parallel_decoding_mode;
  guint8 frame_context_idx;

  GstVp9LoopFilterParams loop_filter_params;
  GstVp9QuantizationParams quantization_params;
  GstVp9SegmentationParams segmentation_params;

  guint8 tile_cols_log2;
  guint8 tile_rows_log2;

  guint16 header_size_in_bytes;

  /* calculated values */
  guint8 lossless_flag;
  guint32 frame_header_length_in_bytes;
};

/**
 * GstVp9StatefulParser:
 *
 * Opaque VP9 parser struct.
 * The size of this object and member variables are not API
 *
 * Since: 1.20
 */
struct _GstVp9StatefulParser
{
  /*< private >*/
  guint8 bit_depth;
  guint8 subsampling_x;
  guint8 subsampling_y;
  guint8 color_space;
  guint8 color_range;

  guint mi_cols;
  guint mi_rows;
  guint sb64_cols;
  guint sb64_rows;

  GstVp9LoopFilterParams loop_filter_params;
  GstVp9SegmentationParams segmentation_params;

  struct {
    guint32 width;
    guint32 height;
  } reference[GST_VP9_REF_FRAMES];
};

GST_CODECS_API
GstVp9StatefulParser *  gst_vp9_stateful_parser_new (void);

GST_CODECS_API
void                    gst_vp9_stateful_parser_free               (GstVp9StatefulParser * parser);

GST_CODECS_API
GstVp9ParserResult      gst_vp9_stateful_parser_parse_frame_header (GstVp9StatefulParser * parser,
                                                                    GstVp9FrameHeader * header,
                                                                    const guint8 * data,
                                                                    gsize size);

/* Util methods */
GST_CODECS_API
gboolean                gst_vp9_seg_feature_active (const GstVp9SegmentationParams * params,
                                                    guint8 segment_id,
                                                    guint8 feature);

GST_CODECS_API
guint8                  gst_vp9_get_qindex   (const GstVp9SegmentationParams * segmentation_params,
                                              const GstVp9QuantizationParams * quantization_params,
                                              guint8 segment_id);


GST_CODECS_API
gint16                  gst_vp9_get_dc_quant (guint8 qindex,
                                              gint8 delta_q_dc,
                                              guint8 bit_depth);

GST_CODECS_API
gint16                  gst_vp9_get_ac_quant (guint8 qindex,
                                              gint8 delta_q_ac,
                                              guint8 bit_depth);

G_END_DECLS

#endif /* __GST_VP9_STATEFUL_PARSER_H__ */
