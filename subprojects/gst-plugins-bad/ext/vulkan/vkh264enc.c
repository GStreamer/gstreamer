/* GStreamer
 * Copyright (C) 2025 Igalia, S.L.
 *     Author: Stéphane Cerveau <scerveau@igalia.com>
 *     Author: Victor Jaquez <vjaquez@igalia.com>
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

/**
 * SECTION:element-vkh264enc
 * @title: vkh264enc
 * @short_description: A Vulkan based H264 video encoder
 *
 * vkh264enc encodes raw video surfaces into H.264 bitstreams using
 * Vulkan video extensions.
 *
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc num-buffers=60 ! timeoverlay ! vulkanupload ! vulkanh264enc ! h264parse ! mp4mux ! filesink location=test.mp4
 * ```
 *
 * Since: 1.28
 */

/*
 * TODO:
 *
 * + support multi-slices
 */

/**
 * GstVulkanEncoderRateControlMode:
 *
 * Rate control modes for Vulkan encoders.
 *
 * Since: 1.28
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vkh264enc.h"

#include <gst/codecparsers/gsth264bitwriter.h>
#include <gst/codecparsers/gsth264parser.h>

#include "base/gsth264encoder.h"
#include "gst/vulkan/gstvkencoder-private.h"
#include "gstvkvideocaps.h"
#include "gstvulkanelements.h"

typedef struct _GstVulkanH264Encoder GstVulkanH264Encoder;
typedef struct _GstVulkanH264EncoderClass GstVulkanH264EncoderClass;
typedef struct _GstVulkanH264EncoderFrame GstVulkanH264EncoderFrame;

enum
{
  PROP_BITRATE = 1,
  PROP_AUD,
  PROP_QUALITY,
  PROP_RATECONTROL,
  PROP_QP_I,
  PROP_QP_P,
  PROP_QP_B,
  PROP_MAX_QP,
  PROP_MIN_QP,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES];

struct _GstVulkanH264Encoder
{
  /*< private > */
  GstH264Encoder parent;

  GstVideoCodecState *in_state;

  gint coded_width;
  gint coded_height;

  GstVulkanInstance *instance;
  GstVulkanDevice *device;
  GstVulkanQueue *encode_queue;
  GstVulkanEncoder *encoder;

  /* sequence configuration */
  GstVulkanVideoProfile profile;
  GstH264SPS sps;
  GstH264PPS pps;
  gsize coded_buffer_size;

  struct
  {
    StdVideoH264SequenceParameterSet sps;
    StdVideoH264PictureParameterSet pps;
    StdVideoH264SequenceParameterSetVui vui;
    StdVideoH264HrdParameters hrd;
  } params;

  struct
  {
    guint bitrate;
    gboolean aud;
    guint32 quality;
    VkVideoEncodeRateControlModeFlagBitsKHR ratecontrol;
    guint32 qp_i;
    guint32 qp_p;
    guint32 qp_b;
    guint32 max_qp;
    guint32 min_qp;
  } prop;

  gboolean update_props;

  struct
  {
    guint bitrate;
    guint max_bitrate;
    guint cpb_size;
    guint32 quality;
    VkVideoEncodeRateControlModeFlagBitsKHR ratecontrol;
    guint32 max_qp;
    guint32 min_qp;
    guint32 qp_i;
    guint32 qp_p;
    guint32 qp_b;
  } rc;
};

struct _GstVulkanH264EncoderClass
{
  GstH264EncoderClass parent;

  gint device_index;
};

struct _GstVulkanH264EncoderFrame
{
  GstVulkanEncoderPicture picture;
  GstVulkanEncoder *encoder;

  VkVideoEncodeH264RateControlInfoKHR vkrc_info;
  VkVideoEncodeH264RateControlLayerInfoKHR vkrc_layer_info;

  /* StdVideoEncodeH264WeightTable slice_wt; *//* UNUSED */
  StdVideoEncodeH264SliceHeader slice_hdr;
  VkVideoEncodeH264NaluSliceInfoKHR vkslice_info;

  StdVideoEncodeH264PictureInfo h264pic_info;
  VkVideoEncodeH264PictureInfoKHR vkh264pic_info;

  StdVideoEncodeH264ReferenceInfo ref_info;
  VkVideoEncodeH264DpbSlotInfoKHR vkref_info;

  StdVideoEncodeH264RefListModEntry mods[2][STD_VIDEO_H264_MAX_NUM_LIST_REF +
      1];
  StdVideoEncodeH264RefPicMarkingEntry mmco[STD_VIDEO_H264_MAX_NUM_LIST_REF +
      1];
  StdVideoEncodeH264ReferenceListsInfo ref_list_info;
};

struct CData
{
  gchar *description;
  gint device_index;
  GstCaps *codec;
  GstCaps *raw;
};

#define GST_VULKAN_H264_ENCODER(obj) ((GstVulkanH264Encoder *)obj)
#define GST_VULKAN_H264_ENCODER_GET_CLASS(obj)                          \
    (G_TYPE_INSTANCE_GET_CLASS((obj), G_TYPE_FROM_INSTANCE(obj),        \
                               GstVulkanH264EncoderClass))
#define GST_VULKAN_H264_ENCODER_CLASS(klass)    \
    ((GstVulkanH264EncoderClass *)klass)

static GstElementClass *parent_class = NULL;

GST_DEBUG_CATEGORY_STATIC (gst_vulkan_h264_encoder_debug);
#define GST_CAT_DEFAULT gst_vulkan_h264_encoder_debug

static gpointer
_register_debug_category (gpointer data)
{
  GST_DEBUG_CATEGORY_INIT (gst_vulkan_h264_encoder_debug, "vulkanh264enc", 0,
      "Vulkan H.264 encoder");

  return NULL;
}

#define update_property(type, obj, old_val, new_val, prop_id)         \
static inline void                                                    \
gst_vulkan_h264_encoder_update_property_##type (GstVulkanH264Encoder * encoder, type * old_val, type new_val, guint prop_id) \
{ \
 GST_OBJECT_LOCK (encoder);                     \
 if (*old_val == new_val) {                     \
   GST_OBJECT_UNLOCK (encoder);                 \
   return;                                      \
 }                                                                      \
 *old_val = new_val;                                                    \
 GST_OBJECT_UNLOCK (encoder);                                           \
 if (prop_id > 0)                                                       \
   g_object_notify_by_pspec (G_OBJECT (encoder), properties[prop_id]);  \
}

update_property (guint, obj, old_val, new_val, prop_id);
#undef update_property

#define update_property_uint(obj, old_val, new_val, prop_id)      \
  gst_vulkan_h264_encoder_update_property_guint (obj, old_val, new_val, prop_id)

static GstVulkanH264EncoderFrame *
gst_vulkan_h264_encoder_frame_new (GstVulkanH264Encoder * self,
    GstVideoCodecFrame * frame)
{
  GstVulkanH264EncoderFrame *vkframe;

  if (self->coded_buffer_size == 0) {
    self->coded_buffer_size = gst_h264_calculate_coded_size (&self->sps, 1);
    if (self->coded_buffer_size == 0)
      goto fail;
    GST_DEBUG_OBJECT (self, "Calculated coded buffer size: %" G_GSIZE_FORMAT,
        self->coded_buffer_size);
  }

  vkframe = g_new (GstVulkanH264EncoderFrame, 1);
  vkframe->encoder = gst_object_ref (self->encoder);
  if (!gst_vulkan_encoder_picture_init (&vkframe->picture, self->encoder,
          frame->input_buffer, self->coded_buffer_size)) {
    gst_object_unref (vkframe->encoder);
    g_free (vkframe);
    goto fail;
  }

  return vkframe;

fail:
  {
    GST_DEBUG_OBJECT (self, "Failed to allocate a vulkan encoding frame");
    return NULL;
  }
}

static void
gst_vulkan_h264_encoder_frame_free (gpointer frame)
{
  GstVulkanH264EncoderFrame *vkframe = frame;
  gst_vulkan_encoder_picture_clear (&vkframe->picture, vkframe->encoder);
  gst_object_unref (vkframe->encoder);
  g_free (vkframe);
}

static inline GstVulkanH264EncoderFrame *
_GET_FRAME (GstH264EncoderFrame * frame)
{
  GstVulkanH264EncoderFrame *enc_frame =
      gst_h264_encoder_frame_get_user_data (frame);
  g_assert (enc_frame);
  return enc_frame;
}

static StdVideoH264SliceType
gst_vulkan_h264_slice_type (GstH264SliceType type)
{
  switch (type) {
    case GST_H264_I_SLICE:
      return STD_VIDEO_H264_SLICE_TYPE_I;
    case GST_H264_P_SLICE:
      return STD_VIDEO_H264_SLICE_TYPE_P;
    case GST_H264_B_SLICE:
      return STD_VIDEO_H264_SLICE_TYPE_B;
    default:
      GST_WARNING ("Unsupported picture type '%d'", type);
      return STD_VIDEO_H264_SLICE_TYPE_INVALID;
  }
}

static const struct
{
  GstH264Profile gst;
  StdVideoH264ProfileIdc vk;
  const char *name;
} H264ProfileMap[] = {
  /* *INDENT-OFF* */
  { GST_H264_PROFILE_BASELINE, STD_VIDEO_H264_PROFILE_IDC_BASELINE, "constrained-baseline" },
  { GST_H264_PROFILE_MAIN, STD_VIDEO_H264_PROFILE_IDC_MAIN, "main" },
  { GST_H264_PROFILE_HIGH, STD_VIDEO_H264_PROFILE_IDC_HIGH, "high" },
  /* { GST_H264_PROFILE_HIGH_444, STD_VIDEO_H264_PROFILE_IDC_HIGH_444_PREDICTIVE, "high-4:4:4" }, */
  /* *INDENT-ON* */
};

static StdVideoH264ProfileIdc
gst_vulkan_h264_profile_type (GstH264Profile profile)
{
  for (int i = 0; i < G_N_ELEMENTS (H264ProfileMap); i++) {
    if (profile == H264ProfileMap[i].gst)
      return H264ProfileMap[i].vk;
  }

  GST_WARNING ("Unsupported profile type '%d'", profile);
  return STD_VIDEO_H264_PROFILE_IDC_INVALID;
}

static const char *
gst_vulkan_h264_profile_name (StdVideoH264ProfileIdc profile)
{
  for (int i = 0; i < G_N_ELEMENTS (H264ProfileMap); i++) {
    if (profile == H264ProfileMap[i].vk)
      return H264ProfileMap[i].name;
  }

  GST_WARNING ("Unsupported profile type '%d'", profile);
  return NULL;
}

/* *INDENT-OFF* */
static const struct
{
  GstH264Level gst;
  StdVideoH264LevelIdc vk;
  const char *name;
} H264LevelMap[] = {
  { GST_H264_LEVEL_L1, STD_VIDEO_H264_LEVEL_IDC_1_0, "1" },
  /* {GST_H264_LEVEL_L1B, "1b", }, */
  { GST_H264_LEVEL_L1_1, STD_VIDEO_H264_LEVEL_IDC_1_1, "1.1"},
  { GST_H264_LEVEL_L1_2, STD_VIDEO_H264_LEVEL_IDC_1_2, "1.2" },
  { GST_H264_LEVEL_L1_3, STD_VIDEO_H264_LEVEL_IDC_1_3, "1.3" },
  { GST_H264_LEVEL_L2, STD_VIDEO_H264_LEVEL_IDC_2_0, "2" },
  { GST_H264_LEVEL_L2_1, STD_VIDEO_H264_LEVEL_IDC_2_1, "2.1" },
  { GST_H264_LEVEL_L2_2, STD_VIDEO_H264_LEVEL_IDC_2_2, "2.2" },
  { GST_H264_LEVEL_L3, STD_VIDEO_H264_LEVEL_IDC_3_0, "3" },
  { GST_H264_LEVEL_L3_1, STD_VIDEO_H264_LEVEL_IDC_3_1, "3.1" },
  { GST_H264_LEVEL_L3_2, STD_VIDEO_H264_LEVEL_IDC_3_2, "3.2" },
  { GST_H264_LEVEL_L4, STD_VIDEO_H264_LEVEL_IDC_4_0, "4" },
  { GST_H264_LEVEL_L4_1, STD_VIDEO_H264_LEVEL_IDC_4_1, "4.1" },
  { GST_H264_LEVEL_L4_2, STD_VIDEO_H264_LEVEL_IDC_4_2, "4.2" },
  { GST_H264_LEVEL_L5, STD_VIDEO_H264_LEVEL_IDC_5_0, "5" },
  { GST_H264_LEVEL_L5_1, STD_VIDEO_H264_LEVEL_IDC_5_1, "5.1" },
  { GST_H264_LEVEL_L5_2, STD_VIDEO_H264_LEVEL_IDC_5_2, "5.2" },
  { GST_H264_LEVEL_L6, STD_VIDEO_H264_LEVEL_IDC_6_0, "6" },
  { GST_H264_LEVEL_L6_1, STD_VIDEO_H264_LEVEL_IDC_6_1, "6.1" },
  { GST_H264_LEVEL_L6_2, STD_VIDEO_H264_LEVEL_IDC_6_2, "6.2" },
};
/* *INDENT-ON* */

static StdVideoH264LevelIdc
gst_vulkan_h264_level_idc (int level_idc)
{
  for (guint i = 0; i < G_N_ELEMENTS (H264LevelMap); i++) {
    if (level_idc == (int) H264LevelMap[i].gst)
      return H264LevelMap[i].vk;
  }

  GST_WARNING ("Unsupported level idc '%d'", level_idc);
  return STD_VIDEO_H264_LEVEL_IDC_INVALID;
}

static GstH264Level
gst_h264_level_idc_from_vk (StdVideoH264LevelIdc vk_level_idc)
{
  for (guint i = 0; i < G_N_ELEMENTS (H264LevelMap); i++) {
    if (vk_level_idc == (int) H264LevelMap[i].vk)
      return H264LevelMap[i].gst;
  }

  GST_WARNING ("Unsupported level idc '%d'", vk_level_idc);
  return -1;
}

static const char *
gst_vulkan_h264_level_name (StdVideoH264LevelIdc level_idc)
{
  for (guint i = 0; i < G_N_ELEMENTS (H264LevelMap); i++) {
    if (level_idc == (int) H264LevelMap[i].vk)
      return H264LevelMap[i].name;
  }

  GST_WARNING ("Unsupported level idc '%d'", level_idc);
  return NULL;
}

static VkVideoComponentBitDepthFlagBitsKHR
gst_vulkan_h264_bit_depth (guint8 depth)
{
  switch (depth) {
    case 8:
      return VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    case 10:
      return VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR;
    case 12:
      return VK_VIDEO_COMPONENT_BIT_DEPTH_12_BIT_KHR;
    default:
      GST_WARNING ("Unsupported bit depth '%u'", depth);
      return VK_VIDEO_COMPONENT_BIT_DEPTH_INVALID_KHR;
  }
}

#define SPS_GST_2_VK(F) \
  F(constraint_set0_flag, flags.constraint_set0_flag)                   \
  F(constraint_set1_flag, flags.constraint_set1_flag)                   \
  F(constraint_set2_flag, flags.constraint_set2_flag)                   \
  F(constraint_set3_flag, flags.constraint_set3_flag)                   \
  F(constraint_set4_flag, flags.constraint_set4_flag)                   \
  F(constraint_set5_flag, flags.constraint_set5_flag)                   \
  F(direct_8x8_inference_flag, flags.direct_8x8_inference_flag)         \
  F(mb_adaptive_frame_field_flag, flags.mb_adaptive_frame_field_flag)   \
  F(frame_mbs_only_flag, flags.frame_mbs_only_flag)                     \
  F(delta_pic_order_always_zero_flag, flags.delta_pic_order_always_zero_flag) \
  F(separate_colour_plane_flag, flags.separate_colour_plane_flag)       \
  F(gaps_in_frame_num_value_allowed_flag, flags.gaps_in_frame_num_value_allowed_flag) \
  F(qpprime_y_zero_transform_bypass_flag, flags.qpprime_y_zero_transform_bypass_flag) \
  F(frame_cropping_flag, flags.frame_cropping_flag)                     \
  F(scaling_matrix_present_flag, flags.seq_scaling_matrix_present_flag) \
  F(vui_parameters_present_flag, flags.vui_parameters_present_flag)     \
  F(id, seq_parameter_set_id)                                           \
  F(bit_depth_luma_minus8, bit_depth_luma_minus8)                       \
  F(bit_depth_chroma_minus8, bit_depth_chroma_minus8)                   \
  F(log2_max_frame_num_minus4, log2_max_frame_num_minus4)               \
  F(pic_order_cnt_type, pic_order_cnt_type)                             \
  F(offset_for_non_ref_pic, offset_for_non_ref_pic)                     \
  F(offset_for_top_to_bottom_field, offset_for_top_to_bottom_field)     \
  F(log2_max_pic_order_cnt_lsb_minus4, log2_max_pic_order_cnt_lsb_minus4) \
  F(num_ref_frames_in_pic_order_cnt_cycle, num_ref_frames_in_pic_order_cnt_cycle) \
  F(num_ref_frames, max_num_ref_frames)                                 \
  F(pic_width_in_mbs_minus1, pic_width_in_mbs_minus1)                   \
  F(pic_height_in_map_units_minus1, pic_height_in_map_units_minus1)     \
  F(frame_crop_left_offset, frame_crop_left_offset)                     \
  F(frame_crop_right_offset, frame_crop_right_offset)                   \
  F(frame_crop_top_offset, frame_crop_top_offset)                       \
  F(frame_crop_bottom_offset, frame_crop_bottom_offset)

#define SPS_VUI_GST_2_VK(F)                                                    \
  F(aspect_ratio_info_present_flag, flags.aspect_ratio_info_present_flag)      \
  F(overscan_info_present_flag, flags.overscan_info_present_flag)              \
  F(overscan_appropriate_flag, flags.overscan_appropriate_flag)                \
  F(chroma_loc_info_present_flag, flags.chroma_loc_info_present_flag)          \
  F(timing_info_present_flag, flags.timing_info_present_flag)                  \
  F(nal_hrd_parameters_present_flag, flags.nal_hrd_parameters_present_flag)    \
  F(vcl_hrd_parameters_present_flag, flags.vcl_hrd_parameters_present_flag)    \
  F(fixed_frame_rate_flag, flags.fixed_frame_rate_flag)                        \
  F(bitstream_restriction_flag, flags.bitstream_restriction_flag)              \
  F(aspect_ratio_idc, aspect_ratio_idc)                                        \
  F(sar_width, sar_width)                                                      \
  F(sar_height, sar_height)                                                    \
  F(num_units_in_tick, num_units_in_tick)                                      \
  F(time_scale, time_scale)                                                    \
  F(num_reorder_frames, max_num_reorder_frames)                                \
  F(max_dec_frame_buffering, max_dec_frame_buffering)                          \
  F(video_signal_type_present_flag, flags.video_signal_type_present_flag)      \
  F(video_full_range_flag, flags.video_full_range_flag)                        \
  F(colour_description_present_flag, flags.color_description_present_flag)     \
  F(video_format, video_format)                                                \
  F(colour_primaries, colour_primaries)                                        \
  F(transfer_characteristics, transfer_characteristics)                        \
  F(matrix_coefficients, matrix_coefficients)                                  \
  F(chroma_sample_loc_type_top_field, chroma_sample_loc_type_top_field)        \
  F(chroma_sample_loc_type_bottom_field, chroma_sample_loc_type_bottom_field)

static inline void
_configure_rate_control (GstVulkanH264Encoder * self,
    GstVulkanVideoCapabilities * vk_caps)
{
  self->rc.bitrate =
      MIN (self->rc.bitrate, vk_caps->encoder.caps.maxBitrate / 1024);
  update_property_uint (self, &self->prop.bitrate, self->rc.bitrate,
      PROP_BITRATE);

  switch (self->rc.ratecontrol) {
    case VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR:
      self->rc.max_bitrate = self->rc.bitrate;
      break;
    case VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR:
      /* by default max bitrate is 66% from vah264enc (target_percentage) */
      self->rc.max_bitrate = (guint)
          gst_util_uint64_scale_int (self->rc.bitrate, 100, 66);
      self->rc.max_bitrate =
          MIN (self->rc.max_bitrate, vk_caps->encoder.caps.maxBitrate / 1024);
      break;
    default:
      break;
  }

  self->rc.cpb_size = (guint)
      gst_util_uint64_scale_int (self->rc.max_bitrate, 1000LL,
      self->rc.bitrate);

  /* uncomment if max_bitrate turns into a property */
  /* update_property_uint (self, &self->prop.max_bitrate, self->rc.max_bitrate, */
  /*     PROP_MAX_BITRATE); */

  /* uncomment if cpb_size turns into a property */
  /* update_property_uint (self, &self->prop.cpb_size, self->rc.cpb_size, */
  /*     PROP_MAX_BITRATE); */

  {
    GstTagList *tags = gst_tag_list_new_empty ();
    gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_NOMINAL_BITRATE,
        self->rc.bitrate, GST_TAG_MAXIMUM_BITRATE, self->rc.max_bitrate,
        GST_TAG_CODEC, "H.264", GST_TAG_ENCODER, "vulkanh264enc", NULL);

    gst_video_encoder_merge_tags (GST_VIDEO_ENCODER (self), tags,
        GST_TAG_MERGE_REPLACE);
    gst_tag_list_unref (tags);
  }
}

static gboolean
gst_vulkan_h264_encoder_init_std_sps (GstVulkanH264Encoder * self,
    GstH264SPS * sps)
{
  GstVulkanVideoCapabilities vk_caps;
  VkVideoEncodeH264CapabilitiesKHR *vk_h264_caps;

  if (!gst_vulkan_encoder_caps (self->encoder, &vk_caps))
    return FALSE;
  vk_h264_caps = &vk_caps.encoder.codec.h264;

  g_assert (sps->vui_parameters_present_flag == 1);
  g_assert (sps->scaling_matrix_present_flag == 0);

  self->params.sps = (StdVideoH264SequenceParameterSet) {
#define FILL_SPS(gst, vk) .vk = sps->gst,
    SPS_GST_2_VK (FILL_SPS)
#undef FILL_SPS
  };

  self->params.sps.profile_idc =
      gst_vulkan_h264_profile_type (sps->profile_idc);
  self->params.sps.chroma_format_idc =
      (StdVideoH264ChromaFormatIdc) sps->chroma_format_idc;

  self->params.sps.level_idc = gst_vulkan_h264_level_idc (sps->level_idc);
  if (sps->level_idc == 0xff)
    return FALSE;

  if (self->rc.bitrate == 0) {
    const GstH264LevelDescriptor *desc;

    desc = gst_h264_get_level_descriptor (sps->profile_idc, 0,
        &self->in_state->info, sps->vui_parameters.max_dec_frame_buffering);
    if (!desc)
      return FALSE;

    self->rc.bitrate =
        desc->max_br * gst_h264_get_cpb_nal_factor (sps->profile_idc) / 1024;
  }

  _configure_rate_control (self, &vk_caps);

  if (sps->direct_8x8_inference_flag == 0
      && (vk_h264_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H264_STD_DIRECT_8X8_INFERENCE_FLAG_UNSET_BIT_KHR) ==
      0) {
    sps->direct_8x8_inference_flag =
        self->params.sps.flags.direct_8x8_inference_flag = 1;
  }

  if (sps->vui_parameters_present_flag == 1) {
    g_assert (sps->vui_parameters.nal_hrd_parameters_present_flag == 0);
    g_assert (sps->vui_parameters.vcl_hrd_parameters_present_flag == 0);

    self->params.vui = (StdVideoH264SequenceParameterSetVui) {
#define FILL_VUI(gst, vk) .vk = sps->vui_parameters.gst,
      SPS_VUI_GST_2_VK (FILL_VUI)
#undef FILL_VUI
    };

    self->params.vui.aspect_ratio_idc =
        (StdVideoH264AspectRatioIdc) sps->vui_parameters.aspect_ratio_idc;
    self->params.sps.pSequenceParameterSetVui = &self->params.vui;
  }

  return TRUE;
}

#define PPS_MEMBERS(F)                                                         \
  F(id, pic_parameter_set_id)                                                  \
  F(sequence->id, seq_parameter_set_id)                                        \
  F(entropy_coding_mode_flag, flags.entropy_coding_mode_flag)                  \
  F(pic_order_present_flag,                                                    \
    flags.bottom_field_pic_order_in_frame_present_flag)                        \
  F(num_ref_idx_l0_active_minus1, num_ref_idx_l0_default_active_minus1)        \
  F(num_ref_idx_l1_active_minus1, num_ref_idx_l1_default_active_minus1)        \
  F(weighted_pred_flag, flags.weighted_pred_flag)                              \
  F(weighted_bipred_idc, weighted_bipred_idc)                                  \
  F(pic_init_qp_minus26, pic_init_qp_minus26)                                  \
  F(pic_init_qs_minus26, pic_init_qs_minus26)                                  \
  F(chroma_qp_index_offset, chroma_qp_index_offset)                            \
  F(deblocking_filter_control_present_flag,                                    \
    flags.deblocking_filter_control_present_flag)                              \
  F(constrained_intra_pred_flag, flags.constrained_intra_pred_flag)            \
  F(redundant_pic_cnt_present_flag, flags.redundant_pic_cnt_present_flag)      \
  F(transform_8x8_mode_flag, flags.transform_8x8_mode_flag)                    \
  F(second_chroma_qp_index_offset, second_chroma_qp_index_offset)              \
  F(pic_scaling_matrix_present_flag, flags.pic_scaling_matrix_present_flag)
  /* Missing in Vulkan
   * num_slice_groups_minus1
   * slice_group_map_type
   * slice_group_change_direction_flag
   * slice_group_change_rate_minus1
   * pic_size_in_map_units_minus1
   */

static gboolean
gst_vulkan_h264_encoder_init_std_pps (GstVulkanH264Encoder * self,
    GstH264PPS * pps)
{
  GstVulkanVideoCapabilities vk_caps;
  VkVideoEncodeH264CapabilitiesKHR *caps;

  if (!gst_vulkan_encoder_caps (self->encoder, &vk_caps))
    return FALSE;
  caps = &vk_caps.encoder.codec.h264;

  self->params.pps = (StdVideoH264PictureParameterSet) {
#define FILL_PPS(gst, vk) .vk = pps->gst,
    PPS_MEMBERS (FILL_PPS)
#undef FILL_PPS
  };

  /* CABAC */
  if (pps->entropy_coding_mode_flag
      && !(caps->stdSyntaxFlags
          & VK_VIDEO_ENCODE_H264_STD_ENTROPY_CODING_MODE_FLAG_SET_BIT_KHR)) {
    pps->entropy_coding_mode_flag =
        self->params.pps.flags.entropy_coding_mode_flag = 0;
  }

  /* dct 8x8 */
  if (pps->transform_8x8_mode_flag
      && !(caps->stdSyntaxFlags
          & VK_VIDEO_ENCODE_H264_STD_TRANSFORM_8X8_MODE_FLAG_SET_BIT_KHR)) {
    pps->transform_8x8_mode_flag =
        self->params.pps.flags.transform_8x8_mode_flag = 0;
  }

  return TRUE;
}

static VkVideoChromaSubsamplingFlagBitsKHR
_h264_get_chroma_subsampling (GstVideoInfo * info)
{
  gint w_sub, h_sub;

  w_sub = 1 << GST_VIDEO_FORMAT_INFO_W_SUB (info->finfo, 1);
  h_sub = 1 << GST_VIDEO_FORMAT_INFO_H_SUB (info->finfo, 1);

  if (w_sub == 2 && h_sub == 2)
    return VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
  else if (w_sub == 2 && h_sub == 1)
    return VK_VIDEO_CHROMA_SUBSAMPLING_422_BIT_KHR;
  else if (w_sub == 1 && h_sub == 1)
    return VK_VIDEO_CHROMA_SUBSAMPLING_444_BIT_KHR;

  g_assert_not_reached ();
}

static GstFlowReturn
gst_vulkan_h264_encoder_new_sequence (GstH264Encoder * encoder,
    GstVideoCodecState * in_state, GstH264Profile profile, GstH264Level * level)
{
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (encoder);
  GError *err = NULL;
  GstVideoInfo *in_info = &in_state->info;
  VkVideoChromaSubsamplingFlagBitsKHR chroma_subsampling;
  VkVideoComponentBitDepthFlagsKHR bit_depth_luma, bit_depth_chroma;
  StdVideoH264ProfileIdc vk_profile;
  GstVulkanVideoCapabilities vk_caps;
  VkVideoEncodeH264CapabilitiesKHR *vk_h264_caps;
  GstVulkanEncoderQualityProperties quality_props;

  if (!self->encoder) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("The vulkan encoder has not been initialized properly"), (NULL));
    return GST_FLOW_ERROR;
  }

  /* profile configuration */
  {
    chroma_subsampling = _h264_get_chroma_subsampling (in_info);
    bit_depth_luma =
        gst_vulkan_h264_bit_depth (GST_VIDEO_INFO_COMP_DEPTH (in_info, 0));
    g_assert (bit_depth_luma != VK_VIDEO_COMPONENT_BIT_DEPTH_INVALID_KHR);
    bit_depth_chroma =
        gst_vulkan_h264_bit_depth (GST_VIDEO_INFO_COMP_DEPTH (in_info, 1));
    g_assert (bit_depth_chroma != VK_VIDEO_COMPONENT_BIT_DEPTH_INVALID_KHR);

    vk_profile = gst_vulkan_h264_profile_type (profile);

    /* *INDENT-OFF* */
    self->profile = (GstVulkanVideoProfile) {
      .profile = (VkVideoProfileInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
        .pNext = &self->profile.usage.encode,
        .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR,
        .chromaSubsampling = chroma_subsampling,
        .chromaBitDepth = bit_depth_chroma,
        .lumaBitDepth = bit_depth_luma,
      },
      .usage.encode = (VkVideoEncodeUsageInfoKHR) {
        .pNext = &self->profile.codec.h264enc,
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_USAGE_INFO_KHR,
        .videoUsageHints = VK_VIDEO_ENCODE_USAGE_DEFAULT_KHR,
        .videoContentHints = VK_VIDEO_ENCODE_CONTENT_DEFAULT_KHR,
        .tuningMode = VK_VIDEO_ENCODE_TUNING_MODE_DEFAULT_KHR,
      },
      .codec.h264enc = (VkVideoEncodeH264ProfileInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_KHR,
        .stdProfileIdc = vk_profile,
      },
    };
    quality_props =  (GstVulkanEncoderQualityProperties) {
      .quality_level = self->rc.quality,
      .codec.h264 = {
        .sType =
            VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_QUALITY_LEVEL_PROPERTIES_KHR,
      },
    };
    /* *INDENT-ON* */
  }

  if (gst_vulkan_encoder_is_started (self->encoder)) {
    if (self->profile.profile.chromaSubsampling == chroma_subsampling
        && self->profile.profile.chromaBitDepth == bit_depth_chroma
        && self->profile.profile.lumaBitDepth == bit_depth_luma
        && self->profile.codec.h264enc.stdProfileIdc == vk_profile) {
      return GST_FLOW_OK;
    } else {
      GST_DEBUG_OBJECT (self, "Restarting vulkan encoder");
      gst_vulkan_encoder_stop (self->encoder);
    }
  }

  if (!gst_vulkan_encoder_start (self->encoder, &self->profile, &quality_props,
          &err)) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Unable to start vulkan encoder with error %s", err->message), (NULL));
    g_clear_error (&err);
    return GST_FLOW_ERROR;
  }

  /* quality configuration */
  {
    self->rc.quality = gst_vulkan_encoder_quality_level (self->encoder);
    update_property_uint (self, &self->prop.quality, self->rc.quality,
        PROP_QUALITY);
    self->rc.ratecontrol = gst_vulkan_encoder_rc_mode (self->encoder);
    update_property_uint (self, &self->prop.ratecontrol, self->rc.ratecontrol,
        PROP_RATECONTROL);
  }

  gst_vulkan_encoder_caps (self->encoder, &vk_caps);
  vk_h264_caps = &vk_caps.encoder.codec.h264;

  GST_LOG_OBJECT (self, "H264 encoder capabilities:\n"
      "    Standard capability flags:\n"
      "        separate_color_plane: %i\n"
      "        qprime_y_zero_transform_bypass: %i\n"
      "        scaling_lists: %i\n"
      "        chroma_qp_index_offset: %i\n"
      "        second_chroma_qp_index_offset: %i\n"
      "        pic_init_qp: %i\n"
      "        weighted:%s%s%s\n"
      "        8x8_transforms: %i\n"
      "        disable_direct_spatial_mv_pred: %i\n"
      "        coder:%s%s\n"
      "        direct_8x8_inference: %i\n"
      "        constrained_intra_pred: %i\n"
      "        deblock:%s%s%s\n"
      "    Capability flags:\n"
      "        hdr_compliance: %i\n"
      "        pred_weight_table_generated: %i\n"
      "        row_unaligned_slice: %i\n"
      "        different_slice_type: %i\n"
      "        b_frame_in_l0_list: %i\n"
      "        b_frame_in_l1_list: %i\n"
      "        per_pict_type_min_max_qp: %i\n"
      "        per_slice_constant_qp: %i\n"
      "        generate_prefix_nalu: %i\n"
      "    Capabilities:\n"
      "        maxLevelIdc: %i\n"
      "        maxSliceCount: %i\n"
      "        max(P/B)PictureL0ReferenceCount: %i P / %i B\n"
      "        maxL1ReferenceCount: %i\n"
      "        maxTemporalLayerCount: %i\n"
      "        expectDyadicTemporalLayerPattern: %i\n"
      "        min/max Qp: [%i, %i]\n"
      "        prefersGopRemainingFrames: %i\n"
      "        requiresGopRemainingFrames: %i\n",
      !!(vk_h264_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H264_STD_SEPARATE_COLOR_PLANE_FLAG_SET_BIT_KHR),
      !!(vk_h264_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H264_STD_QPPRIME_Y_ZERO_TRANSFORM_BYPASS_FLAG_SET_BIT_KHR),
      !!(vk_h264_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H264_STD_SCALING_MATRIX_PRESENT_FLAG_SET_BIT_KHR),
      !!(vk_h264_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H264_STD_CHROMA_QP_INDEX_OFFSET_BIT_KHR),
      !!(vk_h264_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H264_STD_SECOND_CHROMA_QP_INDEX_OFFSET_BIT_KHR),
      !!(vk_h264_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H264_STD_PIC_INIT_QP_MINUS26_BIT_KHR),
      vk_h264_caps->stdSyntaxFlags &
      VK_VIDEO_ENCODE_H264_STD_WEIGHTED_PRED_FLAG_SET_BIT_KHR ?
      " pred" : "",
      vk_h264_caps->stdSyntaxFlags &
      VK_VIDEO_ENCODE_H264_STD_WEIGHTED_BIPRED_IDC_EXPLICIT_BIT_KHR ?
      " bipred_explicit" : "",
      vk_h264_caps->stdSyntaxFlags &
      VK_VIDEO_ENCODE_H264_STD_WEIGHTED_BIPRED_IDC_IMPLICIT_BIT_KHR ?
      " bipred_implicit" : "",
      !!(vk_h264_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H264_STD_TRANSFORM_8X8_MODE_FLAG_SET_BIT_KHR),
      !!(vk_h264_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H264_STD_DIRECT_SPATIAL_MV_PRED_FLAG_UNSET_BIT_KHR),
      vk_h264_caps->stdSyntaxFlags &
      VK_VIDEO_ENCODE_H264_STD_ENTROPY_CODING_MODE_FLAG_UNSET_BIT_KHR ?
      " cabac" : "",
      vk_h264_caps->stdSyntaxFlags &
      VK_VIDEO_ENCODE_H264_STD_ENTROPY_CODING_MODE_FLAG_SET_BIT_KHR ?
      " cavlc" : "",
      !!(vk_h264_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H264_STD_DIRECT_8X8_INFERENCE_FLAG_UNSET_BIT_KHR),
      !!(vk_h264_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H264_STD_CONSTRAINED_INTRA_PRED_FLAG_SET_BIT_KHR),
      vk_h264_caps->stdSyntaxFlags &
      VK_VIDEO_ENCODE_H264_STD_DEBLOCKING_FILTER_DISABLED_BIT_KHR ?
      " filter_disabling" : "",
      vk_h264_caps->stdSyntaxFlags &
      VK_VIDEO_ENCODE_H264_STD_DEBLOCKING_FILTER_ENABLED_BIT_KHR ?
      " filter_enabling" : "",
      vk_h264_caps->stdSyntaxFlags &
      VK_VIDEO_ENCODE_H264_STD_DEBLOCKING_FILTER_PARTIAL_BIT_KHR ?
      " filter_partial" : "",
      !!(vk_h264_caps->flags &
          VK_VIDEO_ENCODE_H264_CAPABILITY_HRD_COMPLIANCE_BIT_KHR),
      !!(vk_h264_caps->flags &
          VK_VIDEO_ENCODE_H264_CAPABILITY_PREDICTION_WEIGHT_TABLE_GENERATED_BIT_KHR),
      !!(vk_h264_caps->flags &
          VK_VIDEO_ENCODE_H264_CAPABILITY_ROW_UNALIGNED_SLICE_BIT_KHR),
      !!(vk_h264_caps->flags &
          VK_VIDEO_ENCODE_H264_CAPABILITY_DIFFERENT_SLICE_TYPE_BIT_KHR),
      !!(vk_h264_caps->flags &
          VK_VIDEO_ENCODE_H264_CAPABILITY_B_FRAME_IN_L0_LIST_BIT_KHR),
      !!(vk_h264_caps->flags &
          VK_VIDEO_ENCODE_H264_CAPABILITY_B_FRAME_IN_L1_LIST_BIT_KHR),
      !!(vk_h264_caps->flags &
          VK_VIDEO_ENCODE_H264_CAPABILITY_PER_PICTURE_TYPE_MIN_MAX_QP_BIT_KHR),
      !!(vk_h264_caps->flags &
          VK_VIDEO_ENCODE_H264_CAPABILITY_PER_SLICE_CONSTANT_QP_BIT_KHR),
      !!(vk_h264_caps->flags &
          VK_VIDEO_ENCODE_H264_CAPABILITY_GENERATE_PREFIX_NALU_BIT_KHR),
      vk_h264_caps->maxLevelIdc,
      vk_h264_caps->maxSliceCount,
      vk_h264_caps->maxPPictureL0ReferenceCount,
      vk_h264_caps->maxBPictureL0ReferenceCount,
      vk_h264_caps->maxL1ReferenceCount,
      vk_h264_caps->maxTemporalLayerCount,
      vk_h264_caps->expectDyadicTemporalLayerPattern,
      vk_h264_caps->maxQp, vk_h264_caps->minQp,
      vk_h264_caps->prefersGopRemainingFrames,
      vk_h264_caps->requiresGopRemainingFrames);

  if (GST_VIDEO_INFO_WIDTH (in_info) > vk_caps.caps.maxCodedExtent.width
      || GST_VIDEO_INFO_HEIGHT (in_info) > vk_caps.caps.maxCodedExtent.height
      || GST_VIDEO_INFO_WIDTH (in_info) < vk_caps.caps.minCodedExtent.width
      || GST_VIDEO_INFO_HEIGHT (in_info) < vk_caps.caps.minCodedExtent.height) {
    GST_ERROR_OBJECT (self, "Frame size is out of driver limits");
    gst_vulkan_encoder_stop (self->encoder);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  gst_h264_encoder_set_max_num_references (encoder,
      vk_h264_caps->maxPPictureL0ReferenceCount,
      vk_h264_caps->maxL1ReferenceCount);

  if (gst_h264_encoder_is_live (encoder)) {
    /* low latency */
    gst_h264_encoder_set_preferred_output_delay (encoder, 0);
  } else {
    /* experimental best value for VA */
    gst_h264_encoder_set_preferred_output_delay (encoder, 4);
  }

  if (self->in_state)
    gst_video_codec_state_unref (self->in_state);
  self->in_state = gst_video_codec_state_ref (in_state);

  self->coded_width = GST_ROUND_UP_N (GST_VIDEO_INFO_WIDTH (in_info),
      vk_caps.encoder.caps.encodeInputPictureGranularity.width);
  self->coded_height = GST_ROUND_UP_N (GST_VIDEO_INFO_HEIGHT (in_info),
      vk_caps.encoder.caps.encodeInputPictureGranularity.height);

  return GST_FLOW_OK;
}

static gboolean
_h264_parameters_parse (GstVulkanH264Encoder * self, gpointer data,
    gsize data_size, GstH264SPS * sps, GstH264PPS * pps)
{
  GstH264ParserResult res, pres;
  GstH264NalUnit nalu = { 0, };
  GstH264NalParser parser = { 0, };
  guint offset = 0;

  do {
    res =
        gst_h264_parser_identify_nalu (&parser, data, offset, data_size, &nalu);
    if (res != GST_H264_PARSER_OK && res != GST_H264_PARSER_NO_NAL_END) {
      GST_WARNING_OBJECT (self, "Failed to parse overridden parameters");
      return FALSE;
    }

    if (nalu.type == GST_H264_NAL_SPS) {
      pres = gst_h264_parser_parse_sps (&parser, &nalu, sps);
      if (pres != GST_H264_PARSER_OK)
        GST_WARNING_OBJECT (self, "Failed to parse overridden SPS");
    } else if (nalu.type == GST_H264_NAL_PPS) {
      pres = gst_h264_parser_parse_pps (&parser, &nalu, pps);
      if (pres != GST_H264_PARSER_OK)
        GST_WARNING_OBJECT (self, "Failed to parse overridden PPS");
    } else {
      GST_WARNING_OBJECT (self, "Unexpected NAL identified: %d", nalu.type);
    }

    offset = nalu.offset + nalu.size;
  } while (res == GST_H264_PARSER_OK);

  /* from gst_h264_nal_parser_free */
  gst_h264_sps_clear (&parser.sps[0]);
  gst_h264_pps_clear (&parser.pps[0]);

  return res == GST_H264_PARSER_OK;
}

static GstFlowReturn
gst_vulkan_h264_encoder_update_parameters (GstVulkanH264Encoder * self,
    GstH264SPS * sps, GstH264PPS * pps)
{
  GError *err = NULL;
  GstVulkanEncoderParameters params;
  VkVideoEncodeH264SessionParametersAddInfoKHR params_add;

  if (!gst_vulkan_h264_encoder_init_std_sps (self, sps))
    return GST_FLOW_ERROR;
  if (!gst_vulkan_h264_encoder_init_std_pps (self, pps))
    return GST_FLOW_ERROR;

  /* *INDENT-OFF* */
  params_add = (VkVideoEncodeH264SessionParametersAddInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR,
    .pStdSPSs = &self->params.sps,
    .stdSPSCount = 1,
    .pStdPPSs = &self->params.pps,
    .stdPPSCount = 1,
  };
  params.h264 = (VkVideoEncodeH264SessionParametersCreateInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR,
    .maxStdSPSCount = params_add.stdSPSCount,
    .maxStdPPSCount = params_add.stdPPSCount,
    .pParametersAddInfo = &params_add,
  };
  /* *INDENT-ON* */

  if (!gst_vulkan_encoder_update_video_session_parameters (self->encoder,
          &params, &err)) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Unable to update session parameters with error %s", err->message),
        (NULL));
    g_clear_error (&err);
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vulkan_h264_encoder_new_parameters (GstH264Encoder * encoder,
    GstH264SPS * sps, GstH264PPS * pps)
{
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (encoder);
  GError *err = NULL;
  GstVulkanEncoderParametersOverrides overrides;
  GstVulkanEncoderParametersFeedback feedback;
  GstVulkanVideoCapabilities vk_caps;
  GstFlowReturn ret;
  gpointer data = NULL;
  gsize data_size = 0;
  StdVideoH264LevelIdc vk_max_level;

  if (!self->encoder) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("The vulkan encoder has not been initialized properly"), (NULL));
    return GST_FLOW_ERROR;
  }

  /* gallium drivers always reply 10 level idc  */
  gst_vulkan_encoder_caps (self->encoder, &vk_caps);
  vk_max_level = vk_caps.encoder.codec.h264.maxLevelIdc;
  if (vk_max_level > STD_VIDEO_H264_LEVEL_IDC_1_0) {
    sps->level_idc =
        MIN (gst_h264_level_idc_from_vk (vk_max_level), sps->level_idc);
  }

  ret = gst_vulkan_h264_encoder_update_parameters (self, sps, pps);
  if (ret != GST_FLOW_OK)
    return ret;

  overrides = (GstVulkanEncoderParametersOverrides) {
    .h264 = {
      .sType =
          VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_GET_INFO_KHR,
      .stdSPSId = self->params.sps.seq_parameter_set_id,
      .stdPPSId = self->params.pps.pic_parameter_set_id,
      .writeStdPPS = VK_TRUE,
      .writeStdSPS = VK_TRUE,
    }
  };

  feedback = (GstVulkanEncoderParametersFeedback) {
    .h264 = {
      .sType =
          VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_FEEDBACK_INFO_KHR,
    }
  };

  if (!gst_vulkan_encoder_video_session_parameters_overrides (self->encoder,
          &overrides, &feedback, &data_size, &data, &err))
    return GST_FLOW_ERROR;

  /* ignore overrides until we get a use case they are actually needed */
  feedback.h264.hasStdPPSOverrides = feedback.h264.hasStdSPSOverrides = 0;

  if (feedback.h264.hasStdSPSOverrides || feedback.h264.hasStdPPSOverrides) {
    GstH264SPS new_sps;
    GstH264PPS new_pps;
    GST_LOG_OBJECT (self, "Vulkan driver overrode parameters:%s%s",
        feedback.h264.hasStdSPSOverrides ? " SPS" : "",
        feedback.h264.hasStdPPSOverrides ? " PPS" : "");

    if (_h264_parameters_parse (self, data, data_size, &new_sps, &new_pps)) {
      if (feedback.h264.hasStdSPSOverrides)
        *sps = new_sps;

      if (feedback.h264.hasStdPPSOverrides) {
        new_pps.sequence = sps;
        *pps = new_pps;
      }

      ret = gst_vulkan_h264_encoder_update_parameters (self, sps, pps);
      if (ret != GST_FLOW_OK)
        return ret;
    }
  }

  g_free (data);

  /* copy it to calculate coded buffer size (MVC extension not supported!) */
  self->sps = *sps;
  self->pps = *pps;
  self->pps.sequence = &self->sps;

  {
    GstCaps *caps;
    GstVideoInfo *info = &self->in_state->info;
    const char *profile, *level;
    GstVideoCodecState *out_state;

    profile = gst_vulkan_h264_profile_name (self->params.sps.profile_idc);
    level = gst_vulkan_h264_level_name (self->params.sps.level_idc);

    if (!(profile && level))
      return GST_FLOW_ERROR;

    caps = gst_caps_new_simple ("video/x-h264", "profile", G_TYPE_STRING,
        profile, "level", G_TYPE_STRING, level, "width", G_TYPE_INT,
        GST_VIDEO_INFO_WIDTH (info), "height", G_TYPE_INT,
        GST_VIDEO_INFO_HEIGHT (info), "alignment", G_TYPE_STRING, "au",
        "stream-format", G_TYPE_STRING, "byte-stream", NULL);

    out_state =
        gst_video_encoder_set_output_state (GST_VIDEO_ENCODER_CAST (self),
        caps, self->in_state);
    gst_video_codec_state_unref (out_state);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vulkan_h264_encoder_new_output (GstH264Encoder * base,
    GstVideoCodecFrame * codec_frame, GstH264EncoderFrame * h264_frame)
{
  GstVulkanH264EncoderFrame *vk_frame;
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (base);

  vk_frame = gst_vulkan_h264_encoder_frame_new (self, codec_frame);
  if (!vk_frame)
    return GST_FLOW_NOT_NEGOTIATED;

  gst_h264_encoder_frame_set_user_data (h264_frame, vk_frame,
      gst_vulkan_h264_encoder_frame_free);

  return GST_FLOW_OK;
}

static gboolean
_write_headers (GstVulkanH264Encoder * self,
    GstVulkanH264EncoderFrame * vk_frame)
{
  GstMapInfo info;
  guint aligned_offset, offset, orig_size, size, fillers;
  GstH264BitWriterResult res;
  guint8 aud_pic_type, *data;
  GstVulkanVideoCapabilities vk_caps;
  gboolean aud, ret = FALSE;
  StdVideoH264PictureType pic_type = vk_frame->h264pic_info.primary_pic_type;
  GstBuffer *buffer = vk_frame->picture.out_buffer;

  if (!gst_buffer_map (buffer, &info, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Failed to map output buffer");
    return FALSE;
  }

  offset = 0;
  data = info.data;
  orig_size = size = info.size;

  GST_OBJECT_LOCK (self);
  aud = self->prop.aud;
  GST_OBJECT_UNLOCK (self);

  if (aud) {
    guint8 nal_buf[4096] = { 0, };
    guint nal_size = sizeof (nal_buf);

    switch (pic_type) {
      case STD_VIDEO_H264_PICTURE_TYPE_IDR:
      case STD_VIDEO_H264_PICTURE_TYPE_I:
        aud_pic_type = 0;
        break;
      case STD_VIDEO_H264_PICTURE_TYPE_P:
        aud_pic_type = 1;
        break;
      case STD_VIDEO_H264_PICTURE_TYPE_B:
        aud_pic_type = 2;
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    res = gst_h264_bit_writer_aud (aud_pic_type, TRUE, nal_buf, &nal_size);
    if (res != GST_H264_BIT_WRITER_OK) {
      GST_ERROR_OBJECT (self, "Failed to generate the AUD header");
      goto bail;
    }

    res = gst_h264_bit_writer_convert_to_nal (4, FALSE, TRUE, FALSE, nal_buf,
        nal_size * 8, data, &size);
    if (res != GST_H264_BIT_WRITER_OK) {
      GST_ERROR_OBJECT (self, "Failed to generate the AUD bytes");
      goto bail;
    }

    offset += size + 1;
  }

  if (pic_type == STD_VIDEO_H264_PICTURE_TYPE_IDR) {
    guint8 nal_buf[4096] = { 0, };
    guint nal_size = sizeof (nal_buf);

    res = gst_h264_bit_writer_sps (&self->sps, TRUE, nal_buf, &nal_size);
    if (res != GST_H264_BIT_WRITER_OK) {
      GST_ERROR_OBJECT (self, "Failed to generate the sequence header");
      goto bail;
    }

    data = info.data + offset;
    size = orig_size - offset;

    res = gst_h264_bit_writer_convert_to_nal (4, FALSE, TRUE, FALSE, nal_buf,
        nal_size * 8, data, &size);
    if (res != GST_H264_BIT_WRITER_OK) {
      GST_ERROR_OBJECT (self, "Failed to generate the SPS bytes");
      goto bail;
    }

    offset += size + 1;
  }

  if (pic_type == STD_VIDEO_H264_PICTURE_TYPE_I
      || pic_type == STD_VIDEO_H264_PICTURE_TYPE_IDR) {
    guint8 nal_buf[4096] = { 0, };
    guint nal_size = sizeof (nal_buf);

    res = gst_h264_bit_writer_pps (&self->pps, TRUE, nal_buf, &nal_size);
    if (res != GST_H264_BIT_WRITER_OK) {
      GST_ERROR_OBJECT (self, "Failed to generate the picture header");
      goto bail;
    }

    data = info.data + offset;
    size = orig_size - offset;

    res = gst_h264_bit_writer_convert_to_nal (4, FALSE, TRUE, FALSE, nal_buf,
        nal_size * 8, data, &size);
    if (res != GST_H264_BIT_WRITER_OK) {
      GST_ERROR_OBJECT (self, "Failed to generate the PPS bytes");
      goto bail;
    }

    offset += size + 1;
  }

  gst_vulkan_encoder_caps (self->encoder, &vk_caps);
  aligned_offset = GST_ROUND_UP_N (offset,
      vk_caps.caps.minBitstreamBufferOffsetAlignment);

  fillers = aligned_offset - offset;
  if (fillers > 0) {
    guint8 nal_buf[4096] = { 0, };
    guint nal_size = sizeof (nal_buf);

    while (fillers < 7 /* filler header size */ )
      fillers += vk_caps.caps.minBitstreamBufferOffsetAlignment;

    fillers -= 7 /* filler header size */ ;

    res = gst_h264_bit_writer_filler (TRUE, fillers, nal_buf, &nal_size);
    if (res != GST_H264_BIT_WRITER_OK) {
      GST_ERROR_OBJECT (self, "Failed to generate fillers");
      goto bail;
    }

    data = info.data + offset;
    size = orig_size - offset;

    res = gst_h264_bit_writer_convert_to_nal (4, FALSE, TRUE, FALSE, nal_buf,
        nal_size * 8, data, &size);
    if (res != GST_H264_BIT_WRITER_OK) {
      GST_ERROR_OBJECT (self, "Failed to generate the fillers bytes");
      goto bail;
    }

    offset += size + 1;
  }

  vk_frame->picture.offset = offset;

  ret = TRUE;

bail:
  gst_buffer_unmap (buffer, &info);
  return ret;
}

static void
_setup_rc_pic (GstVulkanEncoderPicture * pic,
    VkVideoEncodeRateControlInfoKHR * rc_info,
    VkVideoEncodeRateControlLayerInfoKHR * rc_layer, gpointer data)
{
  GstVulkanH264Encoder *self = data;
  GstVulkanH264EncoderFrame *vk_frame = (GstVulkanH264EncoderFrame *) pic;
  GstH264Encoder *h264enc = GST_H264_ENCODER (self);
  guint32 idr_period, num_bframes;
  gboolean b_pyramid;
  VkVideoEncodeH264RateControlFlagsKHR rc_flag;

  idr_period = gst_h264_encoder_get_idr_period (h264enc);
  num_bframes = gst_h264_encoder_get_num_b_frames (h264enc);
  b_pyramid = gst_h264_encoder_gop_is_b_pyramid (h264enc);

  rc_flag = b_pyramid ?
      VK_VIDEO_ENCODE_H264_RATE_CONTROL_REFERENCE_PATTERN_DYADIC_BIT_KHR
      : VK_VIDEO_ENCODE_H264_RATE_CONTROL_REFERENCE_PATTERN_FLAT_BIT_KHR;

  /* *INDENT-OFF* */
  vk_frame->vkrc_info = (VkVideoEncodeH264RateControlInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_RATE_CONTROL_INFO_KHR,
    .flags = rc_flag | VK_VIDEO_ENCODE_H264_RATE_CONTROL_REGULAR_GOP_BIT_KHR,
    .pNext = NULL,
    .gopFrameCount = idr_period,
    .idrPeriod = idr_period,
    .consecutiveBFrameCount = num_bframes,
    .temporalLayerCount = 0,
  };
  /* *INDENT-ON* */

  rc_info->pNext = &vk_frame->vkrc_info;

  if (rc_info->rateControlMode >
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR) {
    rc_layer->averageBitrate = self->rc.bitrate * 1024;
    rc_layer->maxBitrate = self->rc.max_bitrate * 1024;

    /* virtualBufferSizeInMs ~ hrd_buffer_size * 1000LL / bitrate
     *
     * FIXME: add max-bitrate and coded-buffer-size properties to customize the
     * bucket model
     *
     * for more information: https://www.youtube.com/watch?v=Mn8v1ojV80M */
    rc_info->virtualBufferSizeInMs = self->rc.cpb_size;
    rc_info->initialVirtualBufferSizeInMs = self->rc.cpb_size * (3 / 4);

    /* *INDENT-OFF* */
    vk_frame->vkrc_layer_info = (VkVideoEncodeH264RateControlLayerInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_RATE_CONTROL_LAYER_INFO_KHR,

      .useMinQp  = self->rc.min_qp > 0,
      .minQp.qpI = self->rc.min_qp,
      .minQp.qpP = self->rc.min_qp,
      .minQp.qpB = self->rc.min_qp,

      .useMaxQp  = self->rc.max_qp > 0,
      .maxQp.qpI = self->rc.max_qp,
      .maxQp.qpP = self->rc.max_qp,
      .maxQp.qpB = self->rc.max_qp,

      .useMaxFrameSize = 0,
    };
    /* *INDENT-ON* */

    rc_layer->pNext = &vk_frame->vkrc_layer_info;
    vk_frame->vkrc_info.temporalLayerCount = 1;
  }
}

static void
_setup_codec_pic (GstVulkanEncoderPicture * pic, VkVideoEncodeInfoKHR * info,
    gpointer data)
{
  GstVulkanH264EncoderFrame *vk_frame = (GstVulkanH264EncoderFrame *) pic;

  info->pNext = &vk_frame->vkh264pic_info;
  pic->dpb_slot.pNext = &vk_frame->vkref_info;

  /* *INDENT-OFF* */
  vk_frame->vkh264pic_info = (VkVideoEncodeH264PictureInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PICTURE_INFO_KHR,
    .pNext = NULL,
    .naluSliceEntryCount = 1,
    .pNaluSliceEntries = &vk_frame->vkslice_info, /* filled in _setup_slice() */
    .pStdPictureInfo = &vk_frame->h264pic_info,   /* filled in encode_frame() */
    .generatePrefixNalu = VK_FALSE,
  };
  vk_frame->vkref_info = (VkVideoEncodeH264DpbSlotInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_DPB_SLOT_INFO_KHR,
    .pNext = NULL,
    .pStdReferenceInfo = &vk_frame->ref_info, /* filled in encode_frame() */
  };
  /* *INDENT-ON* */
}

static guint8
_get_slot_index (GArray * list, int i)
{
  GstH264EncoderFrame *h264_frame;

  h264_frame = g_array_index (list, GstH264EncoderFrame *, i);
  return _GET_FRAME (h264_frame)->picture.dpb_slot.slotIndex;
}

static void
_setup_ref_lists (GstH264EncoderFrame * h264_frame, GstH264SliceHdr * slice_hdr,
    GArray * list0, GArray * list1)
{
  int i;
  GstVulkanH264EncoderFrame *vk_frame = _GET_FRAME (h264_frame);

  /* *INDENT-OFF* */
  vk_frame->ref_list_info = (StdVideoEncodeH264ReferenceListsInfo) {
    .flags = {
      .ref_pic_list_modification_flag_l0 = 0,
      .ref_pic_list_modification_flag_l1 = 0,
      .reserved = 0,
    },
    .num_ref_idx_l0_active_minus1 =
        MIN (slice_hdr->num_ref_idx_l0_active_minus1,
        STD_VIDEO_H264_MAX_NUM_LIST_REF),
    .num_ref_idx_l1_active_minus1 =
        MIN (slice_hdr->num_ref_idx_l1_active_minus1,
        STD_VIDEO_H264_MAX_NUM_LIST_REF),
    .RefPicList0 = { 0, }, /* filled below */
    .RefPicList1 = { 0, }, /* filled below */
    .refList0ModOpCount = MIN (slice_hdr->n_ref_pic_list_modification_l0, 33),
    .refList1ModOpCount = MIN (slice_hdr->n_ref_pic_list_modification_l1, 33),
    .refPicMarkingOpCount =
        MIN (slice_hdr->dec_ref_pic_marking.n_ref_pic_marking, 10),
    .reserved1 = { 0, },
    .pRefList0ModOperations = NULL,
    .pRefList1ModOperations = NULL,
    .pRefPicMarkingOperations = NULL, /*filled below */
  };
  /* *INDENT-ON* */

  for (i = 0; i < STD_VIDEO_H264_MAX_NUM_LIST_REF; i++) {
    if (i < list0->len) {
      vk_frame->ref_list_info.RefPicList0[i] = _get_slot_index (list0, i);
    } else {
      vk_frame->ref_list_info.RefPicList0[i] =
          STD_VIDEO_H264_NO_REFERENCE_PICTURE;
    }

    if (i < list1->len) {
      vk_frame->ref_list_info.RefPicList1[i] = _get_slot_index (list1, i);
    } else {
      vk_frame->ref_list_info.RefPicList1[i] =
          STD_VIDEO_H264_NO_REFERENCE_PICTURE;
    }
  }

  for (i = 0; i < vk_frame->ref_list_info.refList0ModOpCount; i++) {
    GstH264RefPicListModification *mod =
        &slice_hdr->ref_pic_list_modification_l0[i];

    /* *INDENT-OFF* */
    vk_frame->mods[0][i] = (StdVideoEncodeH264RefListModEntry) {
      .modification_of_pic_nums_idc = mod->modification_of_pic_nums_idc,
      .abs_diff_pic_num_minus1 = mod->value.abs_diff_pic_num_minus1,
    };
    /* *INDENT-ON* */
  }
  if (vk_frame->ref_list_info.refList0ModOpCount > 0)
    vk_frame->ref_list_info.pRefList0ModOperations = vk_frame->mods[0];

  for (i = 0; i < vk_frame->ref_list_info.refList1ModOpCount; i++) {
    GstH264RefPicListModification *mod =
        &slice_hdr->ref_pic_list_modification_l1[i];

    /* *INDENT-OFF* */
    vk_frame->mods[1][i] = (StdVideoEncodeH264RefListModEntry) {
      .modification_of_pic_nums_idc = mod->modification_of_pic_nums_idc,
      .abs_diff_pic_num_minus1 = mod->value.abs_diff_pic_num_minus1,
    };
    /* *INDENT-ON* */
  }
  if (vk_frame->ref_list_info.refList1ModOpCount > 0)
    vk_frame->ref_list_info.pRefList1ModOperations = vk_frame->mods[1];

  for (i = 0; i < vk_frame->ref_list_info.refPicMarkingOpCount; i++) {
    GstH264RefPicMarking *mmco =
        &slice_hdr->dec_ref_pic_marking.ref_pic_marking[i];

    /* *INDENT-OFF* */
    vk_frame->mmco[i] = (StdVideoEncodeH264RefPicMarkingEntry) {
      .long_term_frame_idx = mmco->long_term_frame_idx,
      .max_long_term_frame_idx_plus1 = mmco->max_long_term_frame_idx_plus1,
      .long_term_pic_num = mmco->long_term_pic_num,
      .difference_of_pic_nums_minus1 = mmco->difference_of_pic_nums_minus1,
    };
    /* *INDENT-ON* */
  }
  if (vk_frame->ref_list_info.refPicMarkingOpCount > 0)
    vk_frame->ref_list_info.pRefPicMarkingOperations = vk_frame->mmco;
}

static void
_setup_slice (GstVulkanH264Encoder * self, GstH264EncoderFrame * h264_frame,
    GstH264SliceHdr * slice_hdr)
{
  GstVulkanH264EncoderFrame *vk_frame = _GET_FRAME (h264_frame);
  GstH264SliceType slice_type = h264_frame->type.slice_type;

  /* *INDENT-OFF* */
  vk_frame->slice_hdr = (StdVideoEncodeH264SliceHeader) {
    .flags = (StdVideoEncodeH264SliceHeaderFlags) {
      .direct_spatial_mv_pred_flag = slice_hdr->direct_spatial_mv_pred_flag,
      .num_ref_idx_active_override_flag =
          slice_hdr->num_ref_idx_active_override_flag,
    },
    .first_mb_in_slice = slice_hdr->first_mb_in_slice, /* 0 */
    .slice_type = gst_vulkan_h264_slice_type(h264_frame->type.slice_type),
    .cabac_init_idc = slice_hdr->cabac_init_idc,
    .disable_deblocking_filter_idc = slice_hdr->disable_deblocking_filter_idc,
    .slice_qp_delta = slice_hdr->slice_qp_delta,
    .slice_alpha_c0_offset_div2 = slice_hdr->slice_alpha_c0_offset_div2,
    .slice_beta_offset_div2 = slice_hdr->slice_beta_offset_div2,
    .pWeightTable = NULL,
  };

  vk_frame->vkslice_info = (VkVideoEncodeH264NaluSliceInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_NALU_SLICE_INFO_KHR,
    .pNext = NULL,
    .constantQp = slice_type == GST_H264_P_SLICE ? self->rc.qp_p :
        slice_type == GST_H264_B_SLICE ? self->rc.qp_b :
        self->rc.qp_i,
    .pStdSliceHeader = &vk_frame->slice_hdr,
  };
  /* *INDENT-ON* */

  vk_frame->slice_hdr.slice_qp_delta = vk_frame->vkslice_info.constantQp -
      (self->params.pps.pic_init_qp_minus26 + 26);
}

static void
_reset_rc_props (GstVulkanH264Encoder * self)
{
  GstVulkanVideoCapabilities vk_caps;
  gint32 rc_mode;

  if (!self->encoder)
    return;

  if (!gst_vulkan_encoder_caps (self->encoder, &vk_caps))
    return;

  GST_OBJECT_LOCK (self);
  self->rc.ratecontrol = self->prop.ratecontrol;
  self->rc.min_qp = (self->prop.min_qp > 0) ?
      MAX (self->prop.min_qp, vk_caps.encoder.codec.h264.minQp) : 0;
  self->rc.max_qp = (self->prop.max_qp > 0) ?
      MIN (self->prop.max_qp, vk_caps.encoder.codec.h264.maxQp) : 0;
  GST_OBJECT_UNLOCK (self);

  if (self->rc.ratecontrol ==
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR) {
    GST_OBJECT_LOCK (self);
    self->rc.qp_i =
        CLAMP (self->prop.qp_i, vk_caps.encoder.codec.h264.minQp,
        vk_caps.encoder.codec.h264.maxQp);
    self->rc.qp_p =
        CLAMP (self->prop.qp_p, vk_caps.encoder.codec.h264.minQp,
        vk_caps.encoder.codec.h264.maxQp);
    self->rc.qp_b =
        CLAMP (self->prop.qp_b, vk_caps.encoder.codec.h264.minQp,
        vk_caps.encoder.codec.h264.maxQp);
    GST_OBJECT_UNLOCK (self);
  } else {
    self->rc.qp_i = 0;
    self->rc.qp_p = 0;
    self->rc.qp_b = 0;
  }

  gst_vulkan_encoder_set_rc_mode (self->encoder, self->rc.ratecontrol);
  rc_mode = gst_vulkan_encoder_rc_mode (self->encoder);
  if (rc_mode != -1) {
    self->rc.ratecontrol = rc_mode;
    update_property_uint (self, &self->prop.ratecontrol, self->rc.ratecontrol,
        PROP_RATECONTROL);
  }

  update_property_uint (self, &self->prop.qp_i, self->rc.qp_i, PROP_QP_I);
  update_property_uint (self, &self->prop.qp_p, self->rc.qp_p, PROP_QP_P);
  update_property_uint (self, &self->prop.qp_b, self->rc.qp_b, PROP_QP_B);
  update_property_uint (self, &self->prop.min_qp, self->rc.min_qp, PROP_MIN_QP);
  update_property_uint (self, &self->prop.max_qp, self->rc.max_qp, PROP_MAX_QP);
}

static StdVideoH264PictureType
_gst_slice_type_2_vk_pic_type (GstH264GOPFrame * frame)
{
  if ((frame->slice_type == GST_H264_I_SLICE) && frame->is_ref)
    return STD_VIDEO_H264_PICTURE_TYPE_IDR;
  switch (frame->slice_type) {
    case GST_H264_B_SLICE:
      return STD_VIDEO_H264_PICTURE_TYPE_B;
    case GST_H264_P_SLICE:
      return STD_VIDEO_H264_PICTURE_TYPE_P;
    case GST_H264_I_SLICE:
      return STD_VIDEO_H264_PICTURE_TYPE_I;
    default:
      GST_WARNING ("Unsupported slice type '%d' for picture",
          frame->slice_type);
      return STD_VIDEO_H264_PICTURE_TYPE_INVALID;
  }
}

static void
update_properties_unlocked (GstVulkanH264Encoder * self)
{
  if (!self->update_props)
    return;

  GST_OBJECT_UNLOCK (self);
  _reset_rc_props (self);
  GST_OBJECT_LOCK (self);

  self->update_props = FALSE;
}

static GstFlowReturn
gst_vulkan_h264_encoder_encode_frame (GstH264Encoder * base,
    GstVideoCodecFrame * frame, GstH264EncoderFrame * h264_frame,
    GstH264SliceHdr * slice_hdr, GArray * list0, GArray * list1)
{
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (base);
  GstVulkanEncoderPicture *ref_pics[16] = { NULL, };
  gint i, j;
  GstVulkanH264EncoderFrame *vk_frame = _GET_FRAME (h264_frame);

  if (!gst_vulkan_encoder_is_started (self->encoder))
    return GST_FLOW_NOT_NEGOTIATED;

  GST_OBJECT_LOCK (self);
  update_properties_unlocked (self);
  GST_OBJECT_UNLOCK (self);

  /* *INDENT-OFF* */
  vk_frame->h264pic_info = (StdVideoEncodeH264PictureInfo) {
    .flags = {
      .IdrPicFlag = ((h264_frame->type.slice_type == GST_H264_I_SLICE)
          && h264_frame->type.is_ref),
      .is_reference = h264_frame->type.is_ref,
      .no_output_of_prior_pics_flag =
          slice_hdr->dec_ref_pic_marking.no_output_of_prior_pics_flag,
      .long_term_reference_flag =
          slice_hdr->dec_ref_pic_marking.long_term_reference_flag,
      .adaptive_ref_pic_marking_mode_flag =
          slice_hdr->dec_ref_pic_marking.adaptive_ref_pic_marking_mode_flag,
    },
    .seq_parameter_set_id = self->params.sps.seq_parameter_set_id,
    .pic_parameter_set_id = self->params.pps.pic_parameter_set_id,
    .idr_pic_id = slice_hdr->idr_pic_id,
    .primary_pic_type = _gst_slice_type_2_vk_pic_type (&h264_frame->type),
    .frame_num = h264_frame->gop_frame_num,
    .PicOrderCnt = h264_frame->poc,
    .temporal_id = 0,  /* no support for MVC extension */
    .reserved1 = { 0, },
    .pRefLists = &vk_frame->ref_list_info, /* filled in setup_refs() */
  };

  vk_frame->ref_info = (StdVideoEncodeH264ReferenceInfo) {
    .flags = {
      .used_for_long_term_reference = 0,
      .reserved = 0,
    },
    .primary_pic_type = vk_frame->h264pic_info.primary_pic_type,
    .FrameNum = vk_frame->h264pic_info.frame_num,
    .PicOrderCnt = vk_frame->h264pic_info.PicOrderCnt,
    .long_term_frame_idx = 0,
    .long_term_pic_num = 0,
    .temporal_id = vk_frame->h264pic_info.temporal_id,
  };
  /* *INDENT-ON* */

  _setup_ref_lists (h264_frame, slice_hdr, list0, list1);
  _setup_slice (self, h264_frame, slice_hdr);

  vk_frame->picture.codec_rc_info = &vk_frame->vkrc_info;

  g_assert (list0->len + list1->len <= 16);
  for (i = 0; i < list0->len; i++) {
    GstH264EncoderFrame *pic = g_array_index (list0, GstH264EncoderFrame *, i);
    ref_pics[i] = &_GET_FRAME (pic)->picture;
  }
  for (j = 0; j < list1->len; j++) {
    GstH264EncoderFrame *pic = g_array_index (list1, GstH264EncoderFrame *, j);
    ref_pics[i++] = &_GET_FRAME (pic)->picture;
  }

  if (!_write_headers (self, vk_frame))
    return GST_FLOW_ERROR;

  if (!gst_vulkan_encoder_encode (self->encoder, &self->in_state->info,
          &vk_frame->picture, i, ref_pics)) {
    GST_ERROR_OBJECT (self, "Encode frame error");
    return GST_FLOW_ERROR;
  }
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vulkan_h264_encoder_prepare_output (GstH264Encoder * base,
    GstVideoCodecFrame * frame)
{
  GstH264EncoderFrame *h264_frame;
  GstVulkanH264EncoderFrame *vk_frame;

  h264_frame = gst_video_codec_frame_get_user_data (frame);
  vk_frame = _GET_FRAME (h264_frame);

  gst_buffer_replace (&frame->output_buffer, vk_frame->picture.out_buffer);

  return GST_FLOW_OK;
}

static void
gst_vulkan_h264_encoder_reset (GstH264Encoder * base)
{
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (base);

  GST_OBJECT_LOCK (self);
  self->rc.bitrate = self->prop.bitrate;
  self->rc.quality = self->prop.quality;
  GST_OBJECT_UNLOCK (self);

  _reset_rc_props (self);

  self->coded_buffer_size = 0;
}

static gboolean
gst_vulkan_h264_encoder_open (GstVideoEncoder * base)
{
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (base);
  GstVulkanH264EncoderClass *klass = GST_VULKAN_H264_ENCODER_GET_CLASS (self);
  GstVulkanEncoderCallbacks callbacks = { _setup_codec_pic, _setup_rc_pic };

  if (!gst_vulkan_ensure_element_data (GST_ELEMENT (self), NULL,
          &self->instance)) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Failed to retrieve vulkan instance"), (NULL));
    return FALSE;
  }

  if (!gst_vulkan_ensure_element_device (GST_ELEMENT (self), self->instance,
          &self->device, klass->device_index)) {
    return FALSE;
  }

  self->encode_queue = gst_vulkan_device_select_queue (self->device,
      VK_QUEUE_VIDEO_ENCODE_BIT_KHR);
  if (!self->encode_queue) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Failed to create/retrieve vulkan H.264 encoder queue"), (NULL));
    gst_clear_object (&self->instance);
    return FALSE;
  }

  self->encoder =
      gst_vulkan_encoder_create_from_queue (self->encode_queue,
      VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR);

  if (!self->encoder) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Failed to retrieve vulkan encoder"), (NULL));
    return FALSE;
  }

  gst_vulkan_encoder_set_callbacks (self->encoder, &callbacks, self, NULL);

  return TRUE;
}

static gboolean
gst_vulkan_h264_encoder_close (GstVideoEncoder * encoder)
{
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (encoder);

  gst_clear_object (&self->encoder);
  gst_clear_object (&self->encode_queue);
  gst_clear_object (&self->device);
  gst_clear_object (&self->instance);

  return TRUE;
}

static gboolean
gst_vulkan_h264_encoder_stop (GstVideoEncoder * encoder)
{
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (encoder);

  if (self->in_state)
    gst_video_codec_state_unref (self->in_state);
  self->in_state = NULL;

  gst_vulkan_encoder_stop (self->encoder);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->stop (encoder);
}

static gboolean
_query_context (GstVulkanH264Encoder * self, GstQuery * query)
{
  if (!self->encoder)
    return FALSE;
  if (gst_vulkan_handle_context_query (GST_ELEMENT (self), query, NULL,
          self->instance, self->device))
    return TRUE;

  if (gst_vulkan_queue_handle_context_query (GST_ELEMENT (self), query,
          self->encode_queue))
    return TRUE;

  return FALSE;
}

static gboolean
gst_vulkan_h264_encoder_src_query (GstVideoEncoder * encoder, GstQuery * query)
{
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      ret = _query_context (GST_VULKAN_H264_ENCODER (encoder), query);
      break;
    default:
      ret = GST_VIDEO_ENCODER_CLASS (parent_class)->src_query (encoder, query);
      break;
  }

  return ret;
}

static gboolean
gst_vulkan_h264_encoder_sink_query (GstVideoEncoder * encoder, GstQuery * query)
{
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      ret = _query_context (GST_VULKAN_H264_ENCODER (encoder), query);
      break;
    default:
      ret = GST_VIDEO_ENCODER_CLASS (parent_class)->sink_query (encoder, query);
      break;
  }

  return ret;
}

static gboolean
gst_vulkan_h264_encoder_propose_allocation (GstVideoEncoder * venc,
    GstQuery * query)
{
  gboolean need_pool;
  GstCaps *caps, *profile_caps;
  GstVideoInfo info;
  guint size;
  GstBufferPool *pool = NULL;
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (venc);

  if (!self->encoder) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("The vulkan encoder has not been initialized properly"), (NULL));
    return FALSE;
  }

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  /* the normal size of a frame */
  size = info.size;

  if (!need_pool) {
    gint height, width;

    width = GST_VIDEO_INFO_WIDTH (&info);
    height = GST_VIDEO_INFO_HEIGHT (&info);
    need_pool = self->coded_width != width || self->coded_height != height;
  }

  if (need_pool) {
    GstCaps *new_caps;
    GstStructure *config;
    GstVulkanVideoCapabilities vk_caps;

    new_caps = gst_caps_copy (caps);
    gst_caps_set_simple (new_caps, "width", G_TYPE_INT, self->coded_width,
        "height", G_TYPE_INT, self->coded_height, NULL);

    pool = gst_vulkan_image_buffer_pool_new (self->device);
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, new_caps, size, 0, 0);
    gst_caps_unref (new_caps);

    profile_caps = gst_vulkan_encoder_profile_caps (self->encoder);
    gst_vulkan_image_buffer_pool_config_set_encode_caps (config, profile_caps);
    gst_caps_unref (profile_caps);

    gst_vulkan_image_buffer_pool_config_set_allocation_params (config,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR,
        VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);

    if (!gst_vulkan_encoder_caps (self->encoder, &vk_caps)) {
      gst_structure_free (config);
      g_object_unref (pool);
      return FALSE;
    }
    if ((vk_caps.caps.
            flags & VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR)
        == 0) {
      gst_structure_set (config, "num-layers", G_TYPE_UINT,
          vk_caps.caps.maxDpbSlots, NULL);
    }

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_WARNING_OBJECT (self, "Failed to set pool config");
      g_object_unref (pool);
      return FALSE;
    }
  }

  gst_query_add_allocation_pool (query, pool, size,
      self->sps.vui_parameters.max_dec_frame_buffering, 0);
  if (pool)
    gst_object_unref (pool);

  if (!gst_vulkan_encoder_create_dpb_pool (self->encoder, caps)) {
    GST_ERROR_OBJECT (self, "Unable to create the dpb pool");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_vulkan_h264_encoder_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  gboolean ret;

  ret = GST_VIDEO_ENCODER_CLASS (parent_class)->set_format (encoder, state);
  if (ret)
    ret = gst_h264_encoder_reconfigure (GST_H264_ENCODER (encoder), TRUE);
  return ret;
}

static void
gst_vulkan_h264_encoder_init (GTypeInstance * instance, gpointer g_class)
{
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (instance);

  gst_vulkan_buffer_memory_init_once ();

  self->prop.aud = TRUE;
  self->prop.qp_i = 26;
  self->prop.qp_p = 26;
  self->prop.qp_b = 26;
  self->prop.max_qp = 0;
  self->prop.min_qp = 0;
  self->prop.ratecontrol = VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR;
  self->prop.quality = 2;
}

static void
gst_vulkan_h264_encoder_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (object);

  GST_OBJECT_LOCK (self);
  switch (property_id) {
    case PROP_BITRATE:
      g_value_set_uint (value, self->prop.bitrate);
      break;
    case PROP_AUD:
      g_value_set_boolean (value, self->prop.aud);
      break;
    case PROP_QUALITY:
      g_value_set_uint (value, self->prop.quality);
      break;
    case PROP_RATECONTROL:
      g_value_set_enum (value, self->prop.ratecontrol);
      break;
    case PROP_QP_I:
      g_value_set_uint (value, self->prop.qp_i);
      break;
    case PROP_QP_B:
      g_value_set_uint (value, self->prop.qp_b);
      break;
    case PROP_QP_P:
      g_value_set_uint (value, self->prop.qp_p);
      break;
    case PROP_MAX_QP:
      g_value_set_uint (value, self->prop.max_qp);
      break;
    case PROP_MIN_QP:
      g_value_set_uint (value, self->prop.min_qp);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);
}

static void
gst_vulkan_h264_encoder_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVulkanH264Encoder *self = GST_VULKAN_H264_ENCODER (object);
  GstH264Encoder *h264enc = GST_H264_ENCODER (object);
  gboolean reconfigure = FALSE;

  GST_OBJECT_LOCK (self);
  switch (property_id) {
    case PROP_BITRATE:
      self->prop.bitrate = g_value_get_uint (value);
      reconfigure = TRUE;
      break;
    case PROP_AUD:
      self->prop.aud = g_value_get_boolean (value);
      break;
    case PROP_QUALITY:
      self->prop.quality = g_value_get_uint (value);
      reconfigure = TRUE;
      break;
    case PROP_RATECONTROL:
      self->prop.ratecontrol = g_value_get_enum (value);
      reconfigure = TRUE;
      break;
    case PROP_QP_I:
      self->prop.qp_i = g_value_get_uint (value);
      self->update_props = TRUE;
      break;
    case PROP_QP_P:
      self->prop.qp_p = g_value_get_uint (value);
      self->update_props = TRUE;
      break;
    case PROP_QP_B:
      self->prop.qp_b = g_value_get_uint (value);
      self->update_props = TRUE;
      break;
    case PROP_MAX_QP:
      self->prop.max_qp = g_value_get_uint (value);
      self->update_props = TRUE;
      break;
    case PROP_MIN_QP:
      self->prop.min_qp = g_value_get_uint (value);
      self->update_props = TRUE;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);

  if (reconfigure)
    gst_h264_encoder_reconfigure (h264enc, FALSE);
}

static void
gst_vulkan_h264_encoder_class_init (gpointer g_klass, gpointer class_data)
{
  GstVulkanH264EncoderClass *klass = g_klass;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *encoder_class = GST_VIDEO_ENCODER_CLASS (klass);
  GstH264EncoderClass *h264encoder_class = GST_H264_ENCODER_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  struct CData *cdata = class_data;
  GParamFlags param_flags =
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT
      | GST_PARAM_MUTABLE_PLAYING;
  gchar *long_name;
  const gchar *name;
  GstPadTemplate *sink_pad_template, *src_pad_template;
  GstCaps *sink_doc_caps, *src_doc_caps;

  name = "Vulkan H.264 encoder";
  if (cdata->description)
    long_name = g_strdup_printf ("%s on %s", name, cdata->description);
  else
    long_name = g_strdup (name);

  klass->device_index = cdata->device_index;

  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Encoder/Video/Hardware", "A H.264 video encoder based on Vulkan",
      "Stéphane Cerveau <scerveau@igalia.com>, "
      "Victor Jaquez <vjaquez@igalia.com>");

  parent_class = g_type_class_peek_parent (klass);

  src_doc_caps = gst_caps_from_string ("video/x-h264, "
      "profile = { (string) high, (string) main, (string) constrained-baseline }, "
      "stream-format = (string) byte-stream, alignment = (string) au");
  sink_doc_caps = gst_caps_from_string (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
      (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE, "NV12"));

  sink_pad_template =
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, cdata->raw);
  gst_element_class_add_pad_template (element_class, sink_pad_template);

  src_pad_template =
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, cdata->codec);
  gst_element_class_add_pad_template (element_class, src_pad_template);

  gst_pad_template_set_documentation_caps (sink_pad_template, sink_doc_caps);
  gst_caps_unref (sink_doc_caps);

  gst_pad_template_set_documentation_caps (src_pad_template, src_doc_caps);
  gst_caps_unref (src_doc_caps);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_get_property);

  encoder_class->open = GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_open);
  encoder_class->close = GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_close);
  encoder_class->stop = GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_stop);
  encoder_class->src_query =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_src_query);
  encoder_class->sink_query =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_sink_query);
  encoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_propose_allocation);
  encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_set_format);

  h264encoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_new_sequence);
  h264encoder_class->new_parameters =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_new_parameters);
  h264encoder_class->new_output =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_new_output);
  h264encoder_class->encode_frame =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_encode_frame);
  h264encoder_class->prepare_output =
      GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_prepare_output);
  h264encoder_class->reset = GST_DEBUG_FUNCPTR (gst_vulkan_h264_encoder_reset);

  /**
   * GstVulkanH264Encoder:bitrate:
   *
   * Bitrate is the amount of data (in kilobits) to process per second. It's
   * both a function of the encoded bitstream data size of the encoded pictures
   * as well as the frame rate used by the video sequence
   *
   * A higher bitrate will result in a better visual quality but it will result
   * in a bigger file. A lower bitrate will result in a smaller file, but it
   * will also result in  a worse visual quality.
   *
   * Since: 1.28
   */
  properties[PROP_BITRATE] = g_param_spec_uint ("bitrate", "Bitrate (kbps)",
      "The desired bitrate expressed in kbps (0: auto-calculate)",
      0, G_MAXUINT, 0, param_flags);

  /**
   * GstVulkanH264Encoder:aud:
   *
   * Insert the AU (Access Unit) delimeter for each frame.
   *
   * Since: 1.28
   */
  properties[PROP_AUD] = g_param_spec_boolean ("aud", "Insert AUD",
      "Insert AU (Access Unit) delimeter for each frame", TRUE, param_flags);

  /**
   * GstVulkanH264Encoder:qp-i:
   *
   * Indicates the quantization parameter for all the slices in each I frame.
   * It's only applied when the rate control mode
   * (#GstVulkanH264Encoder:rc-mode) is CQP (constant quantization parameter).
   *
   * Lower QP values mean higher video quality, but larger file sizes or higher
   * bitrates.
   *
   * Since: 1.28
   */
  properties[PROP_QP_I] = g_param_spec_uint ("qp-i", "Constant I frame QP",
      "Constant quantization value for each I-frame slice", 0, 51, 26,
      param_flags);

  /**
   * GstVulkanH264Encoder:qp-p:
   *
   * Indicates the quantization parameter for all the slices in each P frame.
   * It's only applied when the rate control mode
   * (#GstVulkanH264Encoder:rc-mode) is CQP (constant quantization parameter).
   *
   * Lower QP values mean higher video quality, but larger file sizes or higher
   * bitrates.
   *
   * Since: 1.28
   */
  properties[PROP_QP_P] = g_param_spec_uint ("qp-p", "Constant P frame QP",
      "Constant quantization value for each P-frame slice", 0, 51, 26,
      param_flags);

  /**
   * GstVulkanH264Encoder:qp-b:
   *
   * Indicates the quantization parameter for all the slices in each B frame.
   * It's only applied when the rate control mode
   * (#GstVulkanH264Encoder:rc-mode) is CQP (constant quantization parameter).
   *
   * Lower QP values mean higher video quality, but larger file sizes or higher
   * bitrates.
   *
   * Since: 1.28
   */
  properties[PROP_QP_B] = g_param_spec_uint ("qp-b", "Constant B frame QP",
      "Constant quantization value for each B-frame slice", 0, 51, 26,
      param_flags);

  /**
   * GstVulkanH264Encoder:max-qp:
   *
   * Indicates the quantization parameter upper bound for each frame. It's only
   * applied when the rate control mode (#GstVulkanH264Encoder:rc-mode) is
   * either CBR (constant bitrate) or VBR (variable bitrate).
   *
   * Lower QP values mean higher video quality, but larger file sizes or higher
   * bitrates.
   *
   * If zero, the upper bound will not be clamped.
   *
   * Since: 1.28
   */
  properties[PROP_MAX_QP] = g_param_spec_uint ("max-qp", "Maximum QP",
      "Maximum quantization value for each frame (0: disabled)", 0, 51, 0,
      param_flags);

  /**
   * GstVulkanH264Encoder:min-qp:
   *
   * Indicates the quantization parameter lower bound for each frame. It's only
   * applied when the rate control mode (#GstVulkanH264Encoder::rc-mode) is
   * either CBR (constant bitrate) or VBR (variable bitrate).
   *
   * Lower QP values mean higher video quality, but larger file sizes or higher
   * bitrates.
   *
   * If zero, the lower bound will not be clamped.
   *
   * Since: 1.28
   */
  properties[PROP_MIN_QP] = g_param_spec_uint ("min-qp", "Minimum QP",
      "Minimum quantization value for each frame (0: disabled)", 0, 51, 0,
      param_flags);

  /**
   * GstVulkanH264Encoder:rate-control:
   *
   * Rate control algorithms adjust encoding parameters dynamically to regulate
   * the output bitrate. This can involve managing Quantization Parameters (QP),
   * quality, or other encoding parameters.
   *
   * Since: 1.28
   */
  properties[PROP_RATECONTROL] = g_param_spec_enum ("rate-control",
      "rate control mode", "The encoding rate control mode to use",
      GST_TYPE_VULKAN_ENCODER_RATE_CONTROL_MODE,
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR, param_flags);

  /**
   * GstVulkanH264Encoder:quality:
   *
   * Video encode quality level.
   *
   * Higher quality levels may produce higher quality videos at the cost of
   * additional processing time.
   *
   * Since: 1.28
   */
  properties[PROP_QUALITY] = g_param_spec_uint ("quality", "quality level",
      "Video encoding quality level", 0, 10, 2, param_flags);

  g_object_class_install_properties (gobject_class, N_PROPERTIES, properties);

  /* since GstVulkanEncoder is private API */
  gst_type_mark_as_plugin_api (GST_TYPE_VULKAN_ENCODER_RATE_CONTROL_MODE, 0);

  g_free (long_name);
  g_free (cdata->description);
  gst_clear_caps (&cdata->codec);
  gst_clear_caps (&cdata->raw);
  g_free (cdata);
}

gboolean
gst_vulkan_h264_encoder_register (GstPlugin * plugin, GstVulkanDevice * device,
    guint rank)
{
  static GOnce debug_once = G_ONCE_INIT;
  GType type;
  GTypeInfo type_info = {
    .class_size = sizeof (GstVulkanH264EncoderClass),
    .class_init = gst_vulkan_h264_encoder_class_init,
    .instance_size = sizeof (GstVulkanH264Encoder),
    .instance_init = gst_vulkan_h264_encoder_init,
  };
  struct CData *cdata;
  gboolean ret;
  gchar *type_name, *feature_name;
  GstCaps *codec = NULL, *raw = NULL;

  g_return_val_if_fail (GST_IS_PLUGIN (plugin), FALSE);
  g_return_val_if_fail (GST_IS_VULKAN_DEVICE (device), FALSE);

  if (!gst_vulkan_physical_device_codec_caps (device->physical_device,
          VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR, &codec, &raw)) {
    gst_plugin_add_status_warning (plugin,
        "Unable to query H.264 encoder properties");
    return FALSE;
  }

  cdata = g_new (struct CData, 1);
  cdata->description = NULL;
  cdata->device_index = device->physical_device->device_index;
  cdata->codec = codec;
  cdata->raw = raw;

  /* class data will be leaked if the element never gets instantiated */
  GST_MINI_OBJECT_FLAG_SET (cdata->codec, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (cdata->raw, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  gst_vulkan_create_feature_name (device, "GstVulkanH264Encoder",
      "GstVulkanH264Device%dEncoder", &type_name, "vulkanh264enc",
      "vulkanh264device%denc", &feature_name, &cdata->description, &rank);

  type_info.class_data = cdata;

  g_once (&debug_once, _register_debug_category, NULL);
  type = g_type_register_static (GST_TYPE_H264_ENCODER,
      type_name, &type_info, 0);

  ret = gst_element_register (plugin, feature_name, rank, type);

  g_free (type_name);
  g_free (feature_name);

  return ret;
}
