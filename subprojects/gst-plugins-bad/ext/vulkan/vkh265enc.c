/* GStreamer
 * Copyright (C) 2026 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
 *     Author: Stéphane Cerveau <scerveau@igalia.com>
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
 * SECTION:element-vkh265enc
 * @title: vkh265enc
 * @short_description: A Vulkan based H.265 video encoder
 *
 * vkh265enc encodes raw video surfaces into H.265 bitstreams using
 * Vulkan video extensions.
 *
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc num-buffers=60 ! timeoverlay ! vulkanupload ! vulkanh265enc ! h265parse ! mp4mux ! filesink location=test.mp4
 * ```
 *
 * Since: 1.30
 */

/*
 * TODO:
 *
 * + support multi-slices
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vkh265enc.h"

#include <gst/codecparsers/gsth265bitwriter.h>
#include <gst/codecparsers/gsth265parser.h>

#include "base/gsth265encoder.h"
#include "gst/vulkan/gstvkencoder-private.h"
#include "gst/vulkan/gstvkvideo-private.h"
#include "gstvkvideocaps.h"
#include "gstvulkanelements.h"

typedef struct _GstVulkanH265Encoder GstVulkanH265Encoder;
typedef struct _GstVulkanH265EncoderClass GstVulkanH265EncoderClass;
typedef struct _GstVulkanH265EncoderFrame GstVulkanH265EncoderFrame;

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

typedef struct _VK_VPS VK_VPS;
typedef struct _VK_SPS VK_SPS;
typedef struct _VK_PPS VK_PPS;

struct _VK_SPS
{
  StdVideoH265SequenceParameterSet sps;
  StdVideoH265ScalingLists scaling;
  StdVideoH265HrdParameters vui_header;
  StdVideoH265SequenceParameterSetVui vui;
  StdVideoH265ProfileTierLevel ptl;
  StdVideoH265DecPicBufMgr dpbm;
  StdVideoH265PredictorPaletteEntries pal;
  StdVideoH265SubLayerHrdParameters nal_hrd[GST_H265_MAX_SUB_LAYERS];
  StdVideoH265SubLayerHrdParameters vcl_hrd[GST_H265_MAX_SUB_LAYERS];
  /* 7.4.3.2.1: num_short_term_ref_pic_sets is in [0, 64]. */
    StdVideoH265ShortTermRefPicSet
      str[STD_VIDEO_H265_MAX_SHORT_TERM_REF_PIC_SETS];
  StdVideoH265LongTermRefPicsSps ltr;
};

struct _VK_PPS
{
  StdVideoH265PictureParameterSet pps;
  StdVideoH265ScalingLists scaling;
  StdVideoH265PredictorPaletteEntries pal;
};

struct _VK_VPS
{
  StdVideoH265VideoParameterSet vps;
  StdVideoH265ProfileTierLevel ptl;
  StdVideoH265DecPicBufMgr dpbm;
  /* FIXME: a VPS can have multiple header params, each with its own nal and vlc
   * headers sets. Sadly, that's not currently supported by the GStreamer H265
   * parser, which only supports one header params per VPS. */
  StdVideoH265HrdParameters hrd;
  StdVideoH265SubLayerHrdParameters nal_hdr[GST_H265_MAX_SUB_LAYERS];
  StdVideoH265SubLayerHrdParameters vcl_hdr[GST_H265_MAX_SUB_LAYERS];
};

struct _GstVulkanH265Encoder
{
  /*< private > */
  GstH265Encoder parent;

  GstVideoCodecState *in_state;

  gint coded_width;
  gint coded_height;

  GstVulkanInstance *instance;
  GstVulkanDevice *device;
  GstVulkanQueue *encode_queue;
  GstVulkanEncoder *encoder;

  /* sequence configuration */
  GstVulkanVideoProfile profile;
  StdVideoH265LevelIdc level_idc;
  VkVideoEncodeH265StdFlagBitsKHR std_syntax_flags;
  guint offset_alignment;
  GstH265VPS vps;
  GstH265SPS sps;
  GstH265PPS pps;

  gsize coded_buffer_size;

  struct
  {
    VK_VPS vps;
    VK_SPS sps;
    VK_PPS pps;
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

struct _GstVulkanH265EncoderClass
{
  GstH265EncoderClass parent;

  gint device_index;
};

struct _GstVulkanH265EncoderFrame
{
  GstVulkanEncoderPicture picture;
  GstVulkanEncoder *encoder;

  VkVideoEncodeH265RateControlInfoKHR vkrc_info;
  VkVideoEncodeH265RateControlLayerInfoKHR vkrc_layer_info;

  StdVideoEncodeH265WeightTable slice_wt;
  StdVideoEncodeH265SliceSegmentHeader slice_hdr;
  VkVideoEncodeH265NaluSliceSegmentInfoKHR vkslice_info;

  StdVideoEncodeH265PictureInfo pic_info;
  VkVideoEncodeH265PictureInfoKHR vkpic_info;

  StdVideoEncodeH265ReferenceInfo ref_info;
  VkVideoEncodeH265DpbSlotInfoKHR dpb_slot_info;

  StdVideoEncodeH265ReferenceListsInfo ref_list_info;
  StdVideoH265ShortTermRefPicSet short_term_ref_pic_set;
  StdVideoEncodeH265LongTermRefPics long_term_ref_pic_set;
};

struct CData
{
  gchar *description;
  gint device_index;
  GstCaps *codec;
  GstCaps *raw;
};

#define GST_VULKAN_H265_ENCODER(obj) ((GstVulkanH265Encoder *)obj)
#define GST_VULKAN_H265_ENCODER_GET_CLASS(obj)                          \
    (G_TYPE_INSTANCE_GET_CLASS((obj), G_TYPE_FROM_INSTANCE(obj),        \
                               GstVulkanH265EncoderClass))
#define GST_VULKAN_H265_ENCODER_CLASS(klass)    \
    ((GstVulkanH265EncoderClass *)klass)

static GstElementClass *parent_class = NULL;

GST_DEBUG_CATEGORY_STATIC (gst_vulkan_h265_encoder_debug);
#define GST_CAT_DEFAULT gst_vulkan_h265_encoder_debug

static gpointer
_register_debug_category (gpointer data)
{
  GST_DEBUG_CATEGORY_INIT (gst_vulkan_h265_encoder_debug, "vulkanh265enc", 0,
      "Vulkan H.265 encoder");

  return NULL;
}

#define update_property(type, obj, old_val, new_val, prop_id)         \
static inline void                                                    \
gst_vulkan_h265_encoder_update_property_##type (GstVulkanH265Encoder * encoder, type * old_val, type new_val, guint prop_id) \
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
  gst_vulkan_h265_encoder_update_property_guint (obj, old_val, new_val, prop_id)

static GstVulkanH265EncoderFrame *
gst_vulkan_h265_encoder_frame_new (GstVulkanH265Encoder * self,
    GstVideoCodecFrame * frame)
{
  GstVulkanH265EncoderFrame *vkframe;

  if (self->coded_buffer_size == 0) {
    self->coded_buffer_size = gst_h265_calculate_coded_size (&self->sps, 1);
    if (self->coded_buffer_size == 0)
      goto fail;
    GST_DEBUG_OBJECT (self, "Calculated coded buffer size: %" G_GSIZE_FORMAT,
        self->coded_buffer_size);
  }

  vkframe = g_new (GstVulkanH265EncoderFrame, 1);
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
gst_vulkan_h265_encoder_frame_free (gpointer frame)
{
  GstVulkanH265EncoderFrame *vkframe = frame;
  gst_vulkan_encoder_picture_clear (&vkframe->picture, vkframe->encoder);
  gst_object_unref (vkframe->encoder);
  g_free (vkframe);
}

static inline GstVulkanH265EncoderFrame *
_GET_FRAME (GstH265EncoderFrame * frame)
{
  GstVulkanH265EncoderFrame *enc_frame =
      gst_h265_encoder_frame_get_user_data (frame);
  g_assert (enc_frame);
  return enc_frame;
}

static StdVideoH265SliceType
gst_vulkan_h265_slice_type (GstH265SliceType slice_type)
{
  const StdVideoH265SliceType slice_map[] = {
    [GST_H265_B_SLICE] = STD_VIDEO_H265_SLICE_TYPE_B,
    [GST_H265_P_SLICE] = STD_VIDEO_H265_SLICE_TYPE_P,
    [GST_H265_I_SLICE] = STD_VIDEO_H265_SLICE_TYPE_I,
  };

  if ((guint) slice_type < G_N_ELEMENTS (slice_map))
    return slice_map[slice_type];

  return STD_VIDEO_H265_SLICE_TYPE_INVALID;
}

/* *INDENT-OFF* */
static const struct
{
  GstH265ProfileIDC gst;
  StdVideoH265ProfileIdc vk;
} H265ProfileIDCMap[] = {
  { GST_H265_PROFILE_IDC_MAIN, STD_VIDEO_H265_PROFILE_IDC_MAIN },
  { GST_H265_PROFILE_IDC_MAIN_10, STD_VIDEO_H265_PROFILE_IDC_MAIN_10 },
  { GST_H265_PROFILE_IDC_MAIN_STILL_PICTURE, STD_VIDEO_H265_PROFILE_IDC_MAIN_STILL_PICTURE },
  { GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION, STD_VIDEO_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSIONS },
  { GST_H265_PROFILE_IDC_SCREEN_CONTENT_CODING, STD_VIDEO_H265_PROFILE_IDC_SCC_EXTENSIONS },
};
/* *INDENT-ON* */

/* The Vulkan backend intentionally supports only the profiles above. Any
 * other profile is rejected with STD_VIDEO_H265_PROFILE_IDC_INVALID by
 * gst_vulkan_h265_profile_idc(). */
static StdVideoH265ProfileIdc
gst_vulkan_h265_profile_idc (GstH265ProfileIDC profile_idc)
{
  for (guint i = 0; i < G_N_ELEMENTS (H265ProfileIDCMap); i++) {
    if (profile_idc == H265ProfileIDCMap[i].gst)
      return H265ProfileIDCMap[i].vk;
  }

  GST_WARNING ("Unsupported profile idc '%d'", profile_idc);
  return STD_VIDEO_H265_PROFILE_IDC_INVALID;
}

/* *INDENT-OFF* */
static const struct
{
  GstH265Level gst;
  StdVideoH265LevelIdc vk;
  const char *name;
} H265LevelMap[] = {
  { GST_H265_LEVEL_L1, STD_VIDEO_H265_LEVEL_IDC_1_0, "1" },
  { GST_H265_LEVEL_L2, STD_VIDEO_H265_LEVEL_IDC_2_0, "2" },
  { GST_H265_LEVEL_L2_1, STD_VIDEO_H265_LEVEL_IDC_2_1, "2.1" },
  { GST_H265_LEVEL_L3, STD_VIDEO_H265_LEVEL_IDC_3_0, "3" },
  { GST_H265_LEVEL_L3_1, STD_VIDEO_H265_LEVEL_IDC_3_1, "3.1" },
  { GST_H265_LEVEL_L4, STD_VIDEO_H265_LEVEL_IDC_4_0, "4" },
  { GST_H265_LEVEL_L4_1, STD_VIDEO_H265_LEVEL_IDC_4_1, "4.1" },
  { GST_H265_LEVEL_L5, STD_VIDEO_H265_LEVEL_IDC_5_0, "5" },
  { GST_H265_LEVEL_L5_1, STD_VIDEO_H265_LEVEL_IDC_5_1, "5.1" },
  { GST_H265_LEVEL_L5_2, STD_VIDEO_H265_LEVEL_IDC_5_2, "5.2" },
  { GST_H265_LEVEL_L6, STD_VIDEO_H265_LEVEL_IDC_6_0, "6" },
  { GST_H265_LEVEL_L6_1, STD_VIDEO_H265_LEVEL_IDC_6_1, "6.1" },
  { GST_H265_LEVEL_L6_2, STD_VIDEO_H265_LEVEL_IDC_6_2, "6.2" },
  /* Levels 6.3 and 7.0/7.1/7.2 (GST_H265_LEVEL_L6_3/L7/L7_1/L7_2) are not
   * listed because: the Vulkan-Headers used for this build only define
   * STD_VIDEO_H265_LEVEL_IDC up to 6_2, so the enum constants do not exist
   * yet. Add the entries once a Vulkan-Headers exposing them is in use;
   * until then these levels fall back to STD_VIDEO_H265_LEVEL_IDC_INVALID. */
};
/* *INDENT-ON* */

static StdVideoH265LevelIdc
gst_vulkan_h265_level_idc (int level_idc)
{
  for (guint i = 0; i < G_N_ELEMENTS (H265LevelMap); i++) {
    if (level_idc == (int) H265LevelMap[i].gst)
      return H265LevelMap[i].vk;
  }

  GST_WARNING ("Unsupported level idc '%d'", level_idc);
  return STD_VIDEO_H265_LEVEL_IDC_INVALID;
}

static gint
gst_h265_level_idc_from_vk (StdVideoH265LevelIdc vk_level_idc)
{
  for (guint i = 0; i < G_N_ELEMENTS (H265LevelMap); i++) {
    if (vk_level_idc == (int) H265LevelMap[i].vk)
      return H265LevelMap[i].gst;
  }

  GST_WARNING ("Unsupported level idc '%d'", vk_level_idc);
  return -1;
}

static const char *
gst_vulkan_h265_level_idc_name (int level_idc)
{
  for (guint i = 0; i < G_N_ELEMENTS (H265LevelMap); i++) {
    if (level_idc == (int) H265LevelMap[i].gst)
      return H265LevelMap[i].name;
  }

  GST_WARNING ("Unsupported level idc '%d'", level_idc);
  return NULL;
}

static VkVideoComponentBitDepthFlagBitsKHR
gst_vulkan_bit_depth (guint8 depth)
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

static inline void
_configure_rate_control (GstVulkanH265Encoder * self,
    GstVulkanVideoCapabilities * vk_caps)
{
  self->rc.bitrate =
      CLAMP (self->rc.bitrate, 1, vk_caps->encoder.caps.maxBitrate / 1024);
  update_property_uint (self, &self->prop.bitrate, self->rc.bitrate,
      PROP_BITRATE);

  switch (self->rc.ratecontrol) {
    case VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR:
    case VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR:
      self->rc.max_bitrate = self->rc.bitrate;
      break;
    case VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR:
      /* by default max bitrate is 66% from vah264enc (target_percentage) */
      self->rc.max_bitrate = (guint)
          gst_util_uint64_scale_int (self->rc.bitrate, 100, 66);
      self->rc.max_bitrate =
          CLAMP (self->rc.max_bitrate, 1,
          vk_caps->encoder.caps.maxBitrate / 1024);
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
    /* the bitrate tags are expressed in bits/s while self->rc values
     * are in kbps */
    gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_NOMINAL_BITRATE,
        (guint) MIN ((guint64) self->rc.bitrate * 1024, G_MAXUINT),
        GST_TAG_MAXIMUM_BITRATE,
        (guint) MIN ((guint64) self->rc.max_bitrate * 1024, G_MAXUINT),
        GST_TAG_CODEC, "H.265", GST_TAG_ENCODER, "vulkanh265enc", NULL);

    gst_video_encoder_merge_tags (GST_VIDEO_ENCODER (self), tags,
        GST_TAG_MERGE_REPLACE);
    gst_tag_list_unref (tags);
  }
}

static guint32
_array_2_bitmask (const guint8 * array, guint8 size)
{
  guint8 i;
  guint32 bitmask = 0;

  for (i = 0; i < MIN (size, 32); i++)
    bitmask |= !!array[i] << i;
  return bitmask;
}

static void
_fill_sub_layer_hrd_params (const GstH265SubLayerHRDParams * params, guint8 cnt,
    guint8 present_flag, StdVideoH265SubLayerHrdParameters * vkparams)
{
  /* cnt is cpb_cnt_minus1, i.e. number of CPBs minus 1 */
  guint n = MIN (cnt + 1, STD_VIDEO_H265_CPB_CNT_LIST_SIZE);

  memcpy (vkparams->bit_rate_value_minus1, params->bit_rate_value_minus1,
      n * sizeof (*vkparams->bit_rate_value_minus1));

  memcpy (vkparams->cpb_size_value_minus1, params->cpb_size_value_minus1,
      n * sizeof (*vkparams->cpb_size_value_minus1));

  if (present_flag) {
    memcpy (vkparams->cpb_size_du_value_minus1,
        params->cpb_size_du_value_minus1,
        n * sizeof (*vkparams->cpb_size_du_value_minus1));

    memcpy (vkparams->bit_rate_du_value_minus1,
        params->bit_rate_du_value_minus1,
        n * sizeof (*vkparams->bit_rate_du_value_minus1));
  }

  vkparams->cbr_flag = _array_2_bitmask (params->cbr_flag, n);
}

static void
_fill_sub_layers_hrd_params (const GstH265HRDParams * hrd,
    guint8 max_sub_layers_minus1, StdVideoH265SubLayerHrdParameters * nal_hdr,
    StdVideoH265SubLayerHrdParameters * vcl_hdr)
{
  guint8 max_index =
      MIN (max_sub_layers_minus1, STD_VIDEO_H265_SUBLAYERS_LIST_SIZE - 1);

  for (int i = 0; i <= max_index; i++) {
    if (hrd->nal_hrd_parameters_present_flag) {
      _fill_sub_layer_hrd_params (&hrd->sublayer_hrd_params[i],
          hrd->cpb_cnt_minus1[i], hrd->sub_pic_hrd_params_present_flag,
          &nal_hdr[i]);
    }
    if (hrd->vcl_hrd_parameters_present_flag) {
      _fill_sub_layer_hrd_params (&hrd->sublayer_hrd_params[i],
          hrd->cpb_cnt_minus1[i], hrd->sub_pic_hrd_params_present_flag,
          &vcl_hdr[i]);
    }
  }
}

static void
_fill_ptl (const GstH265ProfileTierLevel * ptl,
    StdVideoH265ProfileTierLevel * vk_ptl)
{
  /* *INDENT-OFF* */
  *vk_ptl = (StdVideoH265ProfileTierLevel) {
    .flags = {
      .general_tier_flag = ptl->tier_flag,
      .general_progressive_source_flag = ptl->progressive_source_flag,
      .general_interlaced_source_flag = ptl->interlaced_source_flag,
      .general_non_packed_constraint_flag = ptl->non_packed_constraint_flag,
      .general_frame_only_constraint_flag = ptl->frame_only_constraint_flag,
    },
    .general_profile_idc = gst_vulkan_h265_profile_idc (ptl->profile_idc),
    .general_level_idc = gst_vulkan_h265_level_idc (ptl->level_idc),
  };
  /* *INDENT-ON* */
}

#define HRD_GST_VK(F) \
  F(nal_hrd_parameters_present_flag, flags.nal_hrd_parameters_present_flag) \
  F(vcl_hrd_parameters_present_flag, flags.vcl_hrd_parameters_present_flag) \
  F(sub_pic_hrd_params_present_flag, flags.sub_pic_hrd_params_present_flag) \
  F(sub_pic_cpb_params_in_pic_timing_sei_flag, flags.sub_pic_cpb_params_in_pic_timing_sei_flag) \
  F(tick_divisor_minus2, tick_divisor_minus2) \
  F(du_cpb_removal_delay_increment_length_minus1, du_cpb_removal_delay_increment_length_minus1) \
  F(dpb_output_delay_du_length_minus1, dpb_output_delay_du_length_minus1) \
  F(bit_rate_scale, bit_rate_scale) \
  F(cpb_size_scale, cpb_size_scale) \
  F(cpb_size_du_scale, cpb_size_du_scale) \
  F(initial_cpb_removal_delay_length_minus1, initial_cpb_removal_delay_length_minus1) \
  F(au_cpb_removal_delay_length_minus1, au_cpb_removal_delay_length_minus1) \
  F(dpb_output_delay_length_minus1, dpb_output_delay_length_minus1)
  /* The array members (fixed_pic_rate_*, low_delay_hrd_flag, cpb_cnt_minus1,
   * elemental_duration_in_tc_minus1) and the sub-layer HRD parameters are
   * filled manually below */

static void
_fill_hrd (const GstH265HRDParams * hrd, guint8 sub_layers_present_flag,
    guint8 max_num_sub_layers_minus1, StdVideoH265HrdParameters * vk,
    StdVideoH265SubLayerHrdParameters * nal_hdr,
    StdVideoH265SubLayerHrdParameters * vcl_hdr)
{
  if (sub_layers_present_flag) {
    _fill_sub_layers_hrd_params (hrd, max_num_sub_layers_minus1, nal_hdr,
        vcl_hdr);
  }

  *vk = (StdVideoH265HrdParameters) {
    /* *INDENT-OFF* */
#define FILL_HRD(gst, vk) .vk = hrd->gst,
    HRD_GST_VK(FILL_HRD)
#undef FILL_HRD
    .flags.fixed_pic_rate_general_flag =
        _array_2_bitmask (hrd->fixed_pic_rate_general_flag, 7),
    .flags.fixed_pic_rate_within_cvs_flag =
        _array_2_bitmask (hrd->fixed_pic_rate_within_cvs_flag, 7),
    .flags.low_delay_hrd_flag = _array_2_bitmask (hrd->low_delay_hrd_flag, 7),
    /* Reserved: uint16_t[3] */
    .pSubLayerHrdParametersNal =
        sub_layers_present_flag && hrd->nal_hrd_parameters_present_flag
        ? nal_hdr : NULL,
    .pSubLayerHrdParametersVcl =
        sub_layers_present_flag && hrd->vcl_hrd_parameters_present_flag
        ? vcl_hdr : NULL,
    /* *INDENT-ON* */
  };

  memcpy (vk->cpb_cnt_minus1, hrd->cpb_cnt_minus1,
      MIN (7, STD_VIDEO_H265_SUBLAYERS_LIST_SIZE)
      * sizeof (*hrd->cpb_cnt_minus1));
  memcpy (vk->elemental_duration_in_tc_minus1,
      hrd->elemental_duration_in_tc_minus1,
      MIN (7, STD_VIDEO_H265_SUBLAYERS_LIST_SIZE)
      * sizeof (*hrd->elemental_duration_in_tc_minus1));
}

static void
_fill_dpbm (const guint32 * max_latency_increase_plus1,
    const guint8 * max_dec_pic_buffering_minus1,
    const guint8 * max_num_reorder_pics, gboolean sub_layer_ordering,
    guint32 max_sub_layers, StdVideoH265DecPicBufMgr * dpbm)
{
  guint32 msl = MIN (max_sub_layers,
      (guint32) STD_VIDEO_H265_SUBLAYERS_LIST_SIZE - 1);
  guint32 i, fill_start;

  /* When sub_layer_ordering_info_present_flag is 0, the values at the highest
   * sub-layer are inferred for all lower sub-layers (H.265 7.4.3.1/7.4.3.2). */
  fill_start = sub_layer_ordering ? 0 : msl;
  if (!sub_layer_ordering) {
    for (i = 0; i < fill_start; i++) {
      dpbm->max_dec_pic_buffering_minus1[i] = max_dec_pic_buffering_minus1[msl];
      dpbm->max_num_reorder_pics[i] = max_num_reorder_pics[msl];
      if (max_latency_increase_plus1)
        dpbm->max_latency_increase_plus1[i] = max_latency_increase_plus1[msl];
    }
  }

  for (i = fill_start; i <= msl; i++) {
    dpbm->max_dec_pic_buffering_minus1[i] = max_dec_pic_buffering_minus1[i];
    dpbm->max_num_reorder_pics[i] = max_num_reorder_pics[i];
    /* FIXME: GStreamer's SPS is guint8 vs GStreamer's VPS guint32 */
    if (max_latency_increase_plus1)
      dpbm->max_latency_increase_plus1[i] = max_latency_increase_plus1[i];
  }
}

#define VPS_GST_VK(F)               \
  F(temporal_id_nesting_flag, flags.vps_temporal_id_nesting_flag)     \
  F(sub_layer_ordering_info_present_flag, flags.vps_sub_layer_ordering_info_present_flag) \
  F(timing_info_present_flag, flags.vps_timing_info_present_flag)     \
  F(id, vps_video_parameter_set_id)                                   \
  F(max_sub_layers_minus1, vps_max_sub_layers_minus1)                 \
  F(num_units_in_tick, vps_num_units_in_tick)                         \
  F(time_scale, vps_time_scale)                                       \
  F(poc_proportional_to_timing_flag, flags.vps_poc_proportional_to_timing_flag) \
  F(num_ticks_poc_diff_one_minus1, vps_num_ticks_poc_diff_one_minus1)
  /* F(base_layer_internal_flag,) \ */
  /* F(base_layer_available_flag,) \ */
  /* F(max_layers_minus1,) \ */
  /* StdVideoH265ProfileTierLevel */
  /*   F(profile_tier_level) \ */
  /* StdVideoH265DecPicBufMgr */
  /*   F(max_dec_pic_buffering_minus1[GST_H265_MAX_SUB_LAYERS],) \ */
  /*   F(max_num_reorder_pics[GST_H265_MAX_SUB_LAYERS],) \ */
  /*   F(max_latency_increase_plus1[GST_H265_MAX_SUB_LAYERS],) \ */
  /* F(max_layer_id,) \ */
  /* F(num_hrd_parameters,) \ */
  /* F(hrd_layer_set_idx,) \ */
  /* F(cprms_present_flag,) \ */
  /* StdVideoH265HrdParameters */
  /* F(hrd_params,)       \ */
  /* F(vps_extension,) \ */
  /* F(vps_extension_params,) \ */

static void
_fill_vps (const GstH265VPS * vps, VK_VPS * std_vps)
{
  gboolean add_ptl = FALSE, add_hrd = FALSE;

  if (vps->num_hrd_parameters > 1) {
    GST_FIXME ("GStreamer H.265 parser only supports one header parameters per "
        "VPS");
  }

  if (vps->profile_tier_level.profile_space == 0) {
    _fill_ptl (&vps->profile_tier_level, &std_vps->ptl);
    add_ptl =
        (std_vps->ptl.general_level_idc != STD_VIDEO_H265_LEVEL_IDC_INVALID);
  }

  _fill_dpbm (vps->max_latency_increase_plus1,
      vps->max_dec_pic_buffering_minus1, vps->max_num_reorder_pics,
      vps->sub_layer_ordering_info_present_flag, vps->max_sub_layers_minus1,
      &std_vps->dpbm);

  if (vps->timing_info_present_flag && vps->num_hrd_parameters > 0) {
    _fill_hrd (&vps->hrd_params, vps->cprms_present_flag,
        vps->max_sub_layers_minus1, &std_vps->hrd, std_vps->nal_hdr,
        std_vps->vcl_hdr);
    add_hrd = TRUE;
  }

  /* ignore the rest of HRD parameters since gsth265parser doesn't parse them */

  /* *INDENT-OFF* */
  std_vps->vps = (StdVideoH265VideoParameterSet) {
#define FILL_VPS(gst, vk) .vk = vps->gst,
    VPS_GST_VK (FILL_VPS)
#undef FILL_VPS
    /* Reserved */
    .pDecPicBufMgr = &std_vps->dpbm,
    .pHrdParameters = add_hrd ? &std_vps->hrd : NULL,
    .pProfileTierLevel = add_ptl ? &std_vps->ptl : NULL,
  };
  /* *INDENT-ON* */
}

static void
_fill_scaling_list (const GstH265ScalingList * scaling_list,
    StdVideoH265ScalingLists * vk_scaling_lists)
{
  int i;

  for (i = 0; i < STD_VIDEO_H265_SCALING_LIST_4X4_NUM_LISTS; i++) {
    gst_h265_quant_matrix_4x4_get_raster_from_uprightdiagonal
        (vk_scaling_lists->ScalingList4x4[i],
        scaling_list->scaling_lists_4x4[i]);
  }

  for (i = 0; i < STD_VIDEO_H265_SCALING_LIST_8X8_NUM_LISTS; i++) {
    gst_h265_quant_matrix_8x8_get_raster_from_uprightdiagonal
        (vk_scaling_lists->ScalingList8x8[i],
        scaling_list->scaling_lists_8x8[i]);
  }

  for (i = 0; i < STD_VIDEO_H265_SCALING_LIST_16X16_NUM_LISTS; i++) {
    gst_h265_quant_matrix_16x16_get_raster_from_uprightdiagonal
        (vk_scaling_lists->ScalingList16x16[i],
        scaling_list->scaling_lists_16x16[i]);
  }

  for (i = 0; i < STD_VIDEO_H265_SCALING_LIST_32X32_NUM_LISTS; i++) {
    gst_h265_quant_matrix_32x32_get_raster_from_uprightdiagonal
        (vk_scaling_lists->ScalingList32x32[i],
        scaling_list->scaling_lists_32x32[i]);
  }

  for (i = 0; i < STD_VIDEO_H265_SCALING_LIST_16X16_NUM_LISTS; i++) {
    vk_scaling_lists->ScalingListDCCoef16x16[i] =
        scaling_list->scaling_list_dc_coef_minus8_16x16[i] + 8;
  }

  for (i = 0; i < STD_VIDEO_H265_SCALING_LIST_32X32_NUM_LISTS; i++) {
    vk_scaling_lists->ScalingListDCCoef32x32[i] =
        scaling_list->scaling_list_dc_coef_minus8_32x32[i] + 8;
  }
}

#define VUI_GST_VK(F) \
  F(aspect_ratio_info_present_flag, flags.aspect_ratio_info_present_flag) \
  F(overscan_info_present_flag, flags.overscan_info_present_flag) \
  F(overscan_appropriate_flag, flags.overscan_appropriate_flag) \
  F(video_signal_type_present_flag, flags.video_signal_type_present_flag) \
  F(video_full_range_flag, flags.video_full_range_flag) \
  F(colour_description_present_flag, flags.colour_description_present_flag) \
  F(chroma_loc_info_present_flag, flags.chroma_loc_info_present_flag) \
  F(neutral_chroma_indication_flag, flags.neutral_chroma_indication_flag) \
  F(field_seq_flag, flags.field_seq_flag) \
  F(frame_field_info_present_flag, flags.frame_field_info_present_flag) \
  F(default_display_window_flag, flags.default_display_window_flag) \
  F(timing_info_present_flag, flags.vui_timing_info_present_flag) \
  F(poc_proportional_to_timing_flag, flags.vui_poc_proportional_to_timing_flag) \
  F(hrd_parameters_present_flag, flags.vui_hrd_parameters_present_flag) \
  F(bitstream_restriction_flag, flags.bitstream_restriction_flag) \
  F(tiles_fixed_structure_flag, flags.tiles_fixed_structure_flag) \
  F(motion_vectors_over_pic_boundaries_flag, flags.motion_vectors_over_pic_boundaries_flag) \
  F(restricted_ref_pic_lists_flag, flags.restricted_ref_pic_lists_flag) \
  F(aspect_ratio_idc, aspect_ratio_idc) \
  F(sar_width, sar_width) \
  F(sar_height, sar_height) \
  F(video_format, video_format) \
  F(colour_primaries, colour_primaries) \
  F(transfer_characteristics, transfer_characteristics) \
  F(matrix_coefficients, matrix_coeffs) \
  F(chroma_sample_loc_type_top_field, chroma_sample_loc_type_top_field) \
  F(chroma_sample_loc_type_bottom_field, chroma_sample_loc_type_bottom_field) \
  F(def_disp_win_left_offset, def_disp_win_left_offset) \
  F(def_disp_win_right_offset, def_disp_win_right_offset) \
  F(def_disp_win_top_offset, def_disp_win_top_offset) \
  F(def_disp_win_bottom_offset, def_disp_win_bottom_offset) \
  F(num_units_in_tick, vui_num_units_in_tick) \
  F(time_scale, vui_time_scale) \
  F(num_ticks_poc_diff_one_minus1, vui_num_ticks_poc_diff_one_minus1) \
  F(min_spatial_segmentation_idc, min_spatial_segmentation_idc) \
  F(max_bytes_per_pic_denom, max_bytes_per_pic_denom) \
  F(max_bits_per_min_cu_denom, max_bits_per_min_cu_denom) \
  F(log2_max_mv_length_horizontal, log2_max_mv_length_horizontal) \
  F(log2_max_mv_length_vertical, log2_max_mv_length_vertical)

static void
_fill_vui (const GstH265VUIParams * vui, guint8 max_sub_layers_minus1,
    StdVideoH265SequenceParameterSetVui * vk, StdVideoH265HrdParameters * hrd,
    StdVideoH265SubLayerHrdParameters * nal_hdr,
    StdVideoH265SubLayerHrdParameters * vcl_hdr)
{
  if (vui->hrd_parameters_present_flag) {
    _fill_hrd (&vui->hrd_params, 1, max_sub_layers_minus1, hrd, nal_hdr,
        vcl_hdr);
  }

  *vk = (StdVideoH265SequenceParameterSetVui) {
    /* *INDENT-OFF* */
#define FILL_VUI(gst, vk) .vk = vui->gst,
    VUI_GST_VK (FILL_VUI)
#undef FILL_VUI
    .pHrdParameters = vui->hrd_parameters_present_flag ? hrd : NULL,
    /* *INDENT-ON* */
  };
}

/* clears @flag unless the driver supports the @bit capability */
static inline void
_clear_flag_if_unsupported (guint flags, guint bit, guint8 * flag)
{
  if ((flags & bit) == 0)
    *flag = 0;
}

static void
_fill_strps (const GstH265ShortTermRefPicSet * strps,
    StdVideoH265ShortTermRefPicSet * vk)
{
  *vk = (StdVideoH265ShortTermRefPicSet) {
    /* *INDENT-OFF* */
    .flags = {
      .inter_ref_pic_set_prediction_flag =
          strps->inter_ref_pic_set_prediction_flag,
      .delta_rps_sign = strps->delta_rps_sign,
    },
    .delta_idx_minus1 = strps->delta_idx_minus1,
    /* GstH265SPSEXT (inter-RPS prediction) is never provided, see
     * _fill_sps() */
    .use_delta_flag = 0,
    .abs_delta_rps_minus1 = strps->abs_delta_rps_minus1,
    .num_negative_pics = strps->NumNegativePics,
    .num_positive_pics = strps->NumPositivePics,
    .used_by_curr_pic_flag = 0,
    .used_by_curr_pic_s0_flag = _array_2_bitmask (strps->UsedByCurrPicS0, 16),
    .used_by_curr_pic_s1_flag = _array_2_bitmask (strps->UsedByCurrPicS1, 16),
    /* *INDENT-ON* */
  };

  /* DeltaPocS0/S1 hold the cumulative derived POC offsets (7.4.8), while
     delta_poc_sX_minus1 is relative to the previous entry (0 for the first
     one). */
  for (int i = 0; i < strps->NumNegativePics; i++) {
    gint32 prev = (i > 0) ? strps->DeltaPocS0[i - 1] : 0;
    vk->delta_poc_s0_minus1[i] = (uint16_t) ((prev - strps->DeltaPocS0[i]) - 1);
  }

  for (int i = 0; i < strps->NumPositivePics; i++) {
    gint32 prev = (i > 0) ? strps->DeltaPocS1[i - 1] : 0;
    vk->delta_poc_s1_minus1[i] = (uint16_t) (strps->DeltaPocS1[i] - prev - 1);
  }
}

#define SPS_GST_VK(F) \
  F(temporal_id_nesting_flag, flags.sps_temporal_id_nesting_flag)       \
  F(separate_colour_plane_flag, flags.separate_colour_plane_flag)       \
  F(conformance_window_flag, flags.conformance_window_flag)             \
  F(sub_layer_ordering_info_present_flag, flags.sps_sub_layer_ordering_info_present_flag) \
  F(scaling_list_enabled_flag, flags.scaling_list_enabled_flag)         \
  F(scaling_list_data_present_flag, flags.sps_scaling_list_data_present_flag) \
  F(sample_adaptive_offset_enabled_flag, flags.sample_adaptive_offset_enabled_flag) \
  F(amp_enabled_flag, flags.amp_enabled_flag)                           \
  F(pcm_enabled_flag, flags.pcm_enabled_flag)                           \
  F(pcm_loop_filter_disabled_flag, flags.pcm_loop_filter_disabled_flag) \
  F(long_term_ref_pics_present_flag, flags.long_term_ref_pics_present_flag) \
  F(temporal_mvp_enabled_flag, flags.sps_temporal_mvp_enabled_flag)     \
  F(strong_intra_smoothing_enabled_flag, flags.strong_intra_smoothing_enabled_flag) \
  F(vui_parameters_present_flag, flags.vui_parameters_present_flag)     \
  F(sps_extension_flag, flags.sps_extension_present_flag)               \
  F(sps_range_extension_flag, flags.sps_range_extension_flag)           \
  F(sps_extension_params.transform_skip_rotation_enabled_flag, flags.transform_skip_rotation_enabled_flag) \
  F(sps_extension_params.transform_skip_context_enabled_flag, flags.transform_skip_context_enabled_flag) \
  F(sps_extension_params.implicit_rdpcm_enabled_flag, flags.implicit_rdpcm_enabled_flag) \
  F(sps_extension_params.explicit_rdpcm_enabled_flag, flags.explicit_rdpcm_enabled_flag) \
  F(sps_extension_params.extended_precision_processing_flag, flags.extended_precision_processing_flag) \
  F(sps_extension_params.intra_smoothing_disabled_flag, flags.intra_smoothing_disabled_flag) \
  F(sps_extension_params.high_precision_offsets_enabled_flag, flags.high_precision_offsets_enabled_flag) \
  F(sps_extension_params.persistent_rice_adaptation_enabled_flag, flags.persistent_rice_adaptation_enabled_flag) \
  F(sps_extension_params.cabac_bypass_alignment_enabled_flag, flags.cabac_bypass_alignment_enabled_flag) \
  F(sps_scc_extension_flag, flags.sps_scc_extension_flag)               \
  F(sps_scc_extension_params.sps_curr_pic_ref_enabled_flag, flags.sps_curr_pic_ref_enabled_flag) \
  F(sps_scc_extension_params.palette_mode_enabled_flag, flags.palette_mode_enabled_flag) \
  F(sps_scc_extension_params.sps_palette_predictor_initializers_present_flag, flags.sps_palette_predictor_initializers_present_flag) \
  F(sps_scc_extension_params.intra_boundary_filtering_disabled_flag, flags.intra_boundary_filtering_disabled_flag) \
  F(chroma_format_idc, chroma_format_idc)                               \
  F(pic_width_in_luma_samples, pic_width_in_luma_samples)               \
  F(pic_height_in_luma_samples, pic_height_in_luma_samples)             \
  F(vps_id, sps_video_parameter_set_id)                                 \
  F(max_sub_layers_minus1, sps_max_sub_layers_minus1)                   \
  F(id, sps_seq_parameter_set_id)                                       \
  F(bit_depth_luma_minus8, bit_depth_luma_minus8)                       \
  F(bit_depth_chroma_minus8, bit_depth_chroma_minus8)                   \
  F(log2_max_pic_order_cnt_lsb_minus4, log2_max_pic_order_cnt_lsb_minus4) \
  F(log2_min_luma_coding_block_size_minus3, log2_min_luma_coding_block_size_minus3) \
  F(log2_diff_max_min_luma_coding_block_size, log2_diff_max_min_luma_coding_block_size) \
  F(log2_min_transform_block_size_minus2, log2_min_luma_transform_block_size_minus2) \
  F(log2_diff_max_min_transform_block_size, log2_diff_max_min_luma_transform_block_size) \
  F(max_transform_hierarchy_depth_inter, max_transform_hierarchy_depth_inter) \
  F(max_transform_hierarchy_depth_intra, max_transform_hierarchy_depth_intra) \
  F(num_short_term_ref_pic_sets, num_short_term_ref_pic_sets)           \
  F(num_long_term_ref_pics_sps, num_long_term_ref_pics_sps)             \
  F(pcm_sample_bit_depth_luma_minus1, pcm_sample_bit_depth_luma_minus1) \
  F(pcm_sample_bit_depth_chroma_minus1, pcm_sample_bit_depth_chroma_minus1) \
  F(log2_min_pcm_luma_coding_block_size_minus3, log2_min_pcm_luma_coding_block_size_minus3) \
  F(log2_diff_max_min_pcm_luma_coding_block_size, log2_diff_max_min_pcm_luma_coding_block_size) \
  F(sps_scc_extension_params.palette_max_size, palette_max_size)        \
  F(sps_scc_extension_params.delta_palette_max_predictor_size, delta_palette_max_predictor_size) \
  F(sps_scc_extension_params.motion_vector_resolution_control_idc, motion_vector_resolution_control_idc) \
  F(sps_scc_extension_params.sps_num_palette_predictor_initializer_minus1, sps_num_palette_predictor_initializers_minus1) \
  F(conf_win_left_offset, conf_win_left_offset)                         \
  F(conf_win_right_offset, conf_win_right_offset)                       \
  F(conf_win_top_offset, conf_win_top_offset)                           \
  F(conf_win_bottom_offset, conf_win_bottom_offset)
  /* F(vps, )          \ */
  /* F(profile_tier_level, )        \ */
  /* F(max_dec_pic_buffering_minus1[GST_H265_MAX_SUB_LAYERS], ) \ */
  /* F(max_num_reorder_pics[GST_H265_MAX_SUB_LAYERS], ) \ */
  /* F(max_latency_increase_plus1[GST_H265_MAX_SUB_LAYERS], ) \ */
  /* F(scaling_list, )                   \ */
  /* F(short_term_ref_pic_set[65], )  \ */
  /* F(lt_ref_pic_poc_lsb_sps[32], ) \ */
  /* F(used_by_curr_pic_lt_sps_flag[32], ) \ */
  /* F(vui_params, )                               \ */
  /* F(sps_multilayer_extension_flag, ) \ */
  /* F(sps_3d_extension_flag, ) \ */
  /* F(sps_scc_extension_flag, ) \ */
  /* F(sps_extension_4bits, ) \ */
  /* F(sps_extension_params, )                     \ */
  /* F(sps_scc_extension_params, ) */

static void
_fill_sps (GstVulkanH265Encoder * self, GstH265SPS * sps, VK_SPS * std_sps)
{
  int i, j;
  gboolean add_ptl = FALSE, add_ppe = FALSE;
  GstVulkanVideoCapabilities vk_caps;

  /* Update SPS */
  if (gst_vulkan_encoder_caps (self->encoder, &vk_caps)) {
    VkVideoEncodeH265StdFlagBitsKHR flags =
        vk_caps.encoder.codec.h265.stdSyntaxFlags;
    guint32 min_tb_size = 0, max_tb_size = 0, max_ctb_size = 16;
    guint32 ctb_sizes = vk_caps.encoder.codec.h265.ctbSizes;
    guint32 tb_sizes = vk_caps.encoder.codec.h265.transformBlockSizes;
    guint32 max_transform_hierarchy;

    _clear_flag_if_unsupported (flags,
        VK_VIDEO_ENCODE_H265_STD_SEPARATE_COLOR_PLANE_FLAG_SET_BIT_KHR,
        &sps->separate_colour_plane_flag);
    _clear_flag_if_unsupported (flags,
        VK_VIDEO_ENCODE_H265_STD_SAMPLE_ADAPTIVE_OFFSET_ENABLED_FLAG_SET_BIT_KHR,
        &sps->sample_adaptive_offset_enabled_flag);
    _clear_flag_if_unsupported (flags,
        VK_VIDEO_ENCODE_H265_STD_SCALING_LIST_DATA_PRESENT_FLAG_SET_BIT_KHR,
        &sps->scaling_list_data_present_flag);
    _clear_flag_if_unsupported (flags,
        VK_VIDEO_ENCODE_H265_STD_PCM_ENABLED_FLAG_SET_BIT_KHR,
        &sps->pcm_enabled_flag);
    _clear_flag_if_unsupported (flags,
        VK_VIDEO_ENCODE_H265_STD_SPS_TEMPORAL_MVP_ENABLED_FLAG_SET_BIT_KHR,
        &sps->temporal_mvp_enabled_flag);

    /* coding blocks from 8x8 to max CTB size. */
    if (ctb_sizes & VK_VIDEO_ENCODE_H265_CTB_SIZE_64_BIT_KHR)
      max_ctb_size = 64;
    else if (ctb_sizes & VK_VIDEO_ENCODE_H265_CTB_SIZE_32_BIT_KHR)
      max_ctb_size = 32;

    if (tb_sizes & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_4_BIT_KHR)
      min_tb_size = 4;
    else if (tb_sizes & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_8_BIT_KHR)
      min_tb_size = 8;
    else if (tb_sizes & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_16_BIT_KHR)
      min_tb_size = 16;
    else if (tb_sizes & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_32_BIT_KHR)
      min_tb_size = 32;

    if (tb_sizes & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_32_BIT_KHR)
      max_tb_size = 32;
    else if (tb_sizes & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_16_BIT_KHR)
      max_tb_size = 16;
    else if (tb_sizes & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_8_BIT_KHR)
      max_tb_size = 8;
    else if (tb_sizes & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_4_BIT_KHR)
      max_tb_size = 4;

    sps->log2_min_luma_coding_block_size_minus3 = 0;
    sps->log2_diff_max_min_luma_coding_block_size =
        gst_util_ceil_log2 (max_ctb_size) - 3;
    /* The maximum luma transform block size is bounded by 32x32 and by the
     * maximum CTB size (ITU-T H.265 7.4.3.2.1 / A.4.1), but NOT by the
     * difference between the maximum and minimum CTB sizes. NVIDIA's
     * encoder, for example, produces truncated P-pictures when the maximum
     * transform block size is smaller than 32x32. */
    sps->log2_min_transform_block_size_minus2 =
        gst_util_ceil_log2 (min_tb_size) - 2;
    sps->log2_diff_max_min_transform_block_size =
        (guint32) gst_util_ceil_log2 (max_tb_size) -
        gst_util_ceil_log2 (min_tb_size);

    max_transform_hierarchy =
        gst_util_ceil_log2 (max_ctb_size) - gst_util_ceil_log2 (min_tb_size);
    sps->max_transform_hierarchy_depth_inter = max_transform_hierarchy;
    sps->max_transform_hierarchy_depth_intra = max_transform_hierarchy;
  } else {
    GST_WARNING_OBJECT (self, "Couldn't get Vulkan Capabilities");
  }

  _fill_vui (&sps->vui_params, sps->max_sub_layers_minus1, &std_sps->vui,
      &std_sps->vui_header, std_sps->nal_hrd, std_sps->vcl_hrd);

  if (sps->scaling_list_enabled_flag && sps->scaling_list_data_present_flag) {
    _fill_scaling_list (&sps->scaling_list, &std_sps->scaling);
  }

  if (sps->profile_tier_level.profile_space == 0) {
    _fill_ptl (&sps->profile_tier_level, &std_sps->ptl);
    add_ptl =
        (std_sps->ptl.general_level_idc != STD_VIDEO_H265_LEVEL_IDC_INVALID);
  }

  /* fill DecPicBufMgr */
  {
    /* GstH265SPS stores it as guint8, unlike GstH265VPS */
    guint32 max_latency_increase_plus1[GST_H265_MAX_SUB_LAYERS];

    for (i = 0; i < GST_H265_MAX_SUB_LAYERS; i++)
      max_latency_increase_plus1[i] = sps->max_latency_increase_plus1[i];

    _fill_dpbm (max_latency_increase_plus1, sps->max_dec_pic_buffering_minus1,
        sps->max_num_reorder_pics, sps->sub_layer_ordering_info_present_flag,
        sps->max_sub_layers_minus1, &std_sps->dpbm);
  }

  /* fill PredictorPaletteEntries */
  if (sps->sps_scc_extension_flag) {
    const GstH265SPSSccExtensionParams *scc_params =
        &sps->sps_scc_extension_params;

    if (scc_params->sps_palette_predictor_initializers_present_flag) {
      add_ppe = TRUE;
      for (i = 0; i < STD_VIDEO_H265_PREDICTOR_PALETTE_COMPONENTS_LIST_SIZE;
          i++) {
        for (j = 0; j < STD_VIDEO_H265_PREDICTOR_PALETTE_COMP_ENTRIES_LIST_SIZE;
            j++) {
          /* from guint32 to uint16_t */
          std_sps->pal.PredictorPaletteEntries[i][j] =
              scc_params->sps_palette_predictor_initializer[i][j];
        }
      }
    }
  }

  /* fill ShortTermRefPicSet */
  /* GstH265SPSEXT (predicted ST-RPS) cannot be serialized by the SPS bitstream
   * writer, so feeding the extension data here would make the session params
   * disagree with the emitted SPS. Only pass what the bitstream can express. */
  for (i = 0; i < sps->num_short_term_ref_pic_sets; i++) {
    _fill_strps (&sps->short_term_ref_pic_set[i], &std_sps->str[i]);
  }

  /* fill LongTermRefPicsSps */
  std_sps->ltr.used_by_curr_pic_lt_sps_flag =
      _array_2_bitmask (sps->used_by_curr_pic_lt_sps_flag,
      sps->num_long_term_ref_pics_sps);
  for (i = 0; i < STD_VIDEO_H265_MAX_LONG_TERM_REF_PICS_SPS; i++) {
    /* guint16 to uint32_t */
    std_sps->ltr.lt_ref_pic_poc_lsb_sps[i] = sps->lt_ref_pic_poc_lsb_sps[i];
  }

  /* *INDENT-OFF* */
  std_sps->sps = (StdVideoH265SequenceParameterSet) {
#define FILL_SPS(gst, vk) .vk = sps->gst,
    SPS_GST_VK (FILL_SPS)
#undef FILL_SPS
    .pProfileTierLevel = add_ptl ? &std_sps->ptl : NULL,
    .pDecPicBufMgr = &std_sps->dpbm,
    .pScalingLists =
        sps->scaling_list_enabled_flag
        && sps->scaling_list_data_present_flag ? &std_sps->scaling : NULL,
    .pShortTermRefPicSet =
        sps->num_short_term_ref_pic_sets > 0 ? std_sps->str : NULL,
    .pLongTermRefPicsSps =
        sps->long_term_ref_pics_present_flag ? &std_sps->ltr : NULL,
    .pSequenceParameterSetVui =
        sps->vui_parameters_present_flag ? &std_sps->vui : NULL,
    .pPredictorPaletteEntries = add_ppe ? &std_sps->pal : NULL,
  };
  /* *INDENT-ON* */
}

#define PPS_GST_VK(F) \
  F(id, pps_pic_parameter_set_id) \
  F(sps_id, pps_seq_parameter_set_id) \
  F(dependent_slice_segments_enabled_flag, flags.dependent_slice_segments_enabled_flag) \
  F(output_flag_present_flag, flags.output_flag_present_flag) \
  F(num_extra_slice_header_bits, num_extra_slice_header_bits) \
  F(sign_data_hiding_enabled_flag, flags.sign_data_hiding_enabled_flag) \
  F(cabac_init_present_flag, flags.cabac_init_present_flag) \
  F(num_ref_idx_l0_default_active_minus1, num_ref_idx_l0_default_active_minus1) \
  F(num_ref_idx_l1_default_active_minus1, num_ref_idx_l1_default_active_minus1) \
  F(init_qp_minus26, init_qp_minus26) \
  F(constrained_intra_pred_flag, flags.constrained_intra_pred_flag) \
  F(transform_skip_enabled_flag, flags.transform_skip_enabled_flag) \
  F(cu_qp_delta_enabled_flag, flags.cu_qp_delta_enabled_flag) \
  F(diff_cu_qp_delta_depth, diff_cu_qp_delta_depth) \
  F(cb_qp_offset, pps_cb_qp_offset) \
  F(cr_qp_offset, pps_cr_qp_offset) \
  F(slice_chroma_qp_offsets_present_flag, flags.pps_slice_chroma_qp_offsets_present_flag) \
  F(weighted_pred_flag, flags.weighted_pred_flag) \
  F(weighted_bipred_flag, flags.weighted_bipred_flag) \
  F(transquant_bypass_enabled_flag, flags.transquant_bypass_enabled_flag) \
  F(tiles_enabled_flag, flags.tiles_enabled_flag) \
  F(entropy_coding_sync_enabled_flag, flags.entropy_coding_sync_enabled_flag) \
  F(num_tile_columns_minus1, num_tile_columns_minus1) \
  F(num_tile_rows_minus1, num_tile_rows_minus1) \
  F(uniform_spacing_flag, flags.uniform_spacing_flag) \
  F(loop_filter_across_tiles_enabled_flag, flags.loop_filter_across_tiles_enabled_flag) \
  F(loop_filter_across_slices_enabled_flag, flags.pps_loop_filter_across_slices_enabled_flag) \
  F(deblocking_filter_control_present_flag, flags.deblocking_filter_control_present_flag) \
  F(deblocking_filter_override_enabled_flag, flags.deblocking_filter_override_enabled_flag) \
  F(deblocking_filter_disabled_flag, flags.pps_deblocking_filter_disabled_flag) \
  F(beta_offset_div2, pps_beta_offset_div2) \
  F(tc_offset_div2, pps_tc_offset_div2) \
  F(scaling_list_data_present_flag, flags.pps_scaling_list_data_present_flag) \
  F(lists_modification_present_flag, flags.lists_modification_present_flag) \
  F(log2_parallel_merge_level_minus2, log2_parallel_merge_level_minus2) \
  F(slice_segment_header_extension_present_flag, flags.slice_segment_header_extension_present_flag) \
  F(pps_extension_flag, flags.pps_extension_present_flag) \
  F(pps_range_extension_flag, flags.pps_range_extension_flag) \
  F(pps_extension_params.cross_component_prediction_enabled_flag, flags.cross_component_prediction_enabled_flag) \
  F(pps_extension_params.chroma_qp_offset_list_enabled_flag, flags.chroma_qp_offset_list_enabled_flag) \
  F(pps_scc_extension_params.pps_curr_pic_ref_enabled_flag, flags.pps_curr_pic_ref_enabled_flag) \
  F(pps_scc_extension_params.residual_adaptive_colour_transform_enabled_flag, flags.residual_adaptive_colour_transform_enabled_flag) \
  F(pps_scc_extension_params.pps_slice_act_qp_offsets_present_flag, flags.pps_slice_act_qp_offsets_present_flag) \
  F(pps_scc_extension_params.pps_palette_predictor_initializers_present_flag, flags.pps_palette_predictor_initializers_present_flag) \
  F(pps_scc_extension_params.monochrome_palette_flag, flags.monochrome_palette_flag) \
  F(pps_extension_params.log2_max_transform_skip_block_size_minus2, log2_max_transform_skip_block_size_minus2) \
  F(pps_extension_params.diff_cu_chroma_qp_offset_depth, diff_cu_chroma_qp_offset_depth) \
  F(pps_extension_params.chroma_qp_offset_list_len_minus1, chroma_qp_offset_list_len_minus1) \
  F(pps_extension_params.log2_sao_offset_scale_luma, log2_sao_offset_scale_luma) \
  F(pps_extension_params.log2_sao_offset_scale_chroma, log2_sao_offset_scale_chroma) \
  F(pps_scc_extension_params.pps_act_y_qp_offset_plus5, pps_act_y_qp_offset_plus5) \
  F(pps_scc_extension_params.pps_act_cb_qp_offset_plus5, pps_act_cb_qp_offset_plus5) \
  F(pps_scc_extension_params.pps_act_cr_qp_offset_plus3, pps_act_cr_qp_offset_plus3) \
  F(pps_scc_extension_params.pps_num_palette_predictor_initializer, pps_num_palette_predictor_initializers) \
  F(pps_scc_extension_params.luma_bit_depth_entry_minus8, luma_bit_depth_entry_minus8) \
  F(pps_scc_extension_params.chroma_bit_depth_entry_minus8, chroma_bit_depth_entry_minus8)
  /* F(column_width_minus1[20], ) \ */
  /* F(row_height_minus1[22], ) \ */
  /* F(scaling_list, ) \ */
  /* F(pps_multilayer_extension_flag,) \ */
  /* F(pps_3d_extension_flag, ) \ */
  /* F(pps_scc_extension_flag, ) \ */
  /* F(pps_extension_4bits, ) \ */
  /* F(pps_extension_params, ) \ */
  /* F(pps_scc_extension_params, ) \ */


static void
_fill_pps (GstVulkanH265Encoder * self, GstH265PPS * pps, VK_PPS * std_pps)
{
  int i, j;
  gboolean add_pal = FALSE;
  GstVulkanVideoCapabilities vk_caps;

  if (pps->scaling_list_data_present_flag)
    _fill_scaling_list (&pps->scaling_list, &std_pps->scaling);

  if (pps->pps_scc_extension_flag) {
    const GstH265PPSSccExtensionParams *scc = &pps->pps_scc_extension_params;
    if (scc->pps_palette_predictor_initializers_present_flag) {
      add_pal = TRUE;
      for (i = 0; i < STD_VIDEO_H265_PREDICTOR_PALETTE_COMPONENTS_LIST_SIZE;
          i++) {
        for (j = 0;
            j <
            pps->pps_scc_extension_params.pps_num_palette_predictor_initializer;
            j++) {
          std_pps->pal.PredictorPaletteEntries[i][j] =
              pps->pps_scc_extension_params.
              pps_palette_predictor_initializer[i][j];
        }
      }
    }
  }

  /* update PPS */
  pps->cu_qp_delta_enabled_flag = (self->rc.ratecontrol !=
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR);

  if (gst_vulkan_encoder_caps (self->encoder, &vk_caps)) {
    VkVideoEncodeH265StdFlagBitsKHR flags =
        vk_caps.encoder.codec.h265.stdSyntaxFlags;

    if ((flags & VK_VIDEO_ENCODE_H265_STD_INIT_QP_MINUS26_BIT_KHR) != 0
        && self->rc.ratecontrol ==
        VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR) {
      /* only meaningful in CQP; in CBR/VBR the driver controls the QP */
      pps->init_qp_minus26 = self->rc.qp_i - 26;
    } else {
      pps->init_qp_minus26 = 0;
    }
    _clear_flag_if_unsupported (flags,
        VK_VIDEO_ENCODE_H265_STD_WEIGHTED_PRED_FLAG_SET_BIT_KHR,
        &pps->weighted_pred_flag);
    _clear_flag_if_unsupported (flags,
        VK_VIDEO_ENCODE_H265_STD_WEIGHTED_BIPRED_FLAG_SET_BIT_KHR,
        &pps->weighted_bipred_flag);
    _clear_flag_if_unsupported (flags,
        VK_VIDEO_ENCODE_H265_STD_LOG2_PARALLEL_MERGE_LEVEL_MINUS2_BIT_KHR,
        &pps->log2_parallel_merge_level_minus2);
    _clear_flag_if_unsupported (flags,
        VK_VIDEO_ENCODE_H265_STD_SIGN_DATA_HIDING_ENABLED_FLAG_SET_BIT_KHR,
        &pps->sign_data_hiding_enabled_flag);
    if ((flags &
            VK_VIDEO_ENCODE_H265_STD_TRANSFORM_SKIP_ENABLED_FLAG_SET_BIT_KHR) ==
        0) {
      pps->transform_skip_enabled_flag = 0;
    } else if ((flags &
            VK_VIDEO_ENCODE_H265_STD_TRANSFORM_SKIP_ENABLED_FLAG_UNSET_BIT_KHR)
        == 0) {
      pps->transform_skip_enabled_flag = 1;
    }
    _clear_flag_if_unsupported (flags,
        VK_VIDEO_ENCODE_H265_STD_PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT_FLAG_SET_BIT_KHR,
        &pps->slice_chroma_qp_offsets_present_flag);
    _clear_flag_if_unsupported (flags,
        VK_VIDEO_ENCODE_H265_STD_TRANSQUANT_BYPASS_ENABLED_FLAG_SET_BIT_KHR,
        &pps->transquant_bypass_enabled_flag);
    _clear_flag_if_unsupported (flags,
        VK_VIDEO_ENCODE_H265_STD_CONSTRAINED_INTRA_PRED_FLAG_SET_BIT_KHR,
        &pps->constrained_intra_pred_flag);
    _clear_flag_if_unsupported (flags,
        VK_VIDEO_ENCODE_H265_STD_ENTROPY_CODING_SYNC_ENABLED_FLAG_SET_BIT_KHR,
        &pps->entropy_coding_sync_enabled_flag);
    _clear_flag_if_unsupported (flags,
        VK_VIDEO_ENCODE_H265_STD_DEBLOCKING_FILTER_OVERRIDE_ENABLED_FLAG_SET_BIT_KHR,
        &pps->deblocking_filter_override_enabled_flag);

    pps->dependent_slice_segments_enabled_flag =
        !!(flags &
        VK_VIDEO_ENCODE_H265_STD_DEPENDENT_SLICE_SEGMENTS_ENABLED_FLAG_SET_BIT_KHR);
  } else {
    GST_WARNING_OBJECT (self, "Couldn't get Vulkan Capabilities");
  }

  std_pps->pps = (StdVideoH265PictureParameterSet) {
    /* *INDENT-OFF* */
#define FILL_PPS(gst, vk) .vk = pps->gst,
    PPS_GST_VK(FILL_PPS)
#undef FILL_PPS
    .sps_video_parameter_set_id = pps->sps->vps_id,
    .pScalingLists = pps->scaling_list_data_present_flag ? &std_pps->scaling : NULL,
    .pPredictorPaletteEntries = add_pal ? &std_pps->pal : NULL,
    /* *INDENT-OFF* */
  };

  for (i = 0; i < MIN (STD_VIDEO_H265_CHROMA_QP_OFFSET_TILE_COLS_LIST_SIZE, pps->num_tile_columns_minus1 + 1); i++)
    std_pps->pps.column_width_minus1[i] = pps->column_width_minus1[i];

  for (i = 0; i < MIN (STD_VIDEO_H265_CHROMA_QP_OFFSET_TILE_ROWS_LIST_SIZE, pps->num_tile_rows_minus1 + 1); i++)
    std_pps->pps.row_height_minus1[i] = pps->row_height_minus1[i];

  if (pps->pps_extension_flag) {
    const GstH265PPSExtensionParams *ext = &pps->pps_extension_params;
    size_t n = (MIN (6, ext->chroma_qp_offset_list_len_minus1 + 1)) * sizeof(int8_t);
    memcpy (std_pps->pps.cb_qp_offset_list, ext->cb_qp_offset_list, n);
    memcpy (std_pps->pps.cr_qp_offset_list, ext->cr_qp_offset_list, n);
  }
}

static VkVideoChromaSubsamplingFlagBitsKHR
_h265_get_chroma_subsampling (GstVideoInfo * info)
{
  gint w_sub, h_sub;

  if (GST_VIDEO_INFO_N_COMPONENTS (info) == 1)
    return VK_VIDEO_CHROMA_SUBSAMPLING_MONOCHROME_BIT_KHR;

  w_sub = 1 << GST_VIDEO_FORMAT_INFO_W_SUB (info->finfo, 1);
  h_sub = 1 << GST_VIDEO_FORMAT_INFO_H_SUB (info->finfo, 1);

  if (w_sub == 2 && h_sub == 2)
    return VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
  else if (w_sub == 2 && h_sub == 1)
    return VK_VIDEO_CHROMA_SUBSAMPLING_422_BIT_KHR;
  else if (w_sub == 1 && h_sub == 1)
    return VK_VIDEO_CHROMA_SUBSAMPLING_444_BIT_KHR;

  return VK_VIDEO_CHROMA_SUBSAMPLING_INVALID_KHR;
}

static void
_reset_rc_props (GstVulkanH265Encoder * self,
    const GstVulkanVideoCapabilities * vk_caps)
{
  GstVulkanVideoCapabilities _vkcaps;
  gint32 min_qp_caps, max_qp_caps;

  if (!self->encoder)
    return;

  if (!vk_caps) {
    if (!gst_vulkan_encoder_caps (self->encoder, &_vkcaps)) {
      GST_DEBUG_OBJECT (self,
          "RC configuration wasn't updated: no capabilities available");
      return;
    }
    vk_caps = &_vkcaps;
  }

  /* minQp is signed and may be negative for high bit depths, while the QP
   * properties are unsigned. */
  if (vk_caps->encoder.codec.h264.minQp < 0)
    GST_FIXME ("Ignoring driver's negative minQp capability");

  min_qp_caps = MAX (vk_caps->encoder.codec.h265.minQp, 0);
  max_qp_caps = vk_caps->encoder.codec.h265.maxQp;

  GST_OBJECT_LOCK (self);
  self->rc.ratecontrol = self->prop.ratecontrol;
  self->rc.min_qp = (self->prop.min_qp > 0) ?
      MAX ((gint32) self->prop.min_qp, min_qp_caps) : 0;
  self->rc.max_qp = (self->prop.max_qp > 0) ?
      MIN ((gint32) self->prop.max_qp, max_qp_caps) : 0;
  GST_OBJECT_UNLOCK (self);

  gst_vulkan_encoder_set_rc_mode (self->encoder, vk_caps,
      self->rc.ratecontrol);
  self->rc.ratecontrol = gst_vulkan_encoder_rc_mode (self->encoder);
  update_property_uint (self, &self->prop.ratecontrol, self->rc.ratecontrol,
      PROP_RATECONTROL);

  if (self->rc.ratecontrol ==
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR) {
    GST_OBJECT_LOCK (self);
    self->rc.qp_i = CLAMP ((gint32) self->prop.qp_i, min_qp_caps, max_qp_caps);
    self->rc.qp_p = CLAMP ((gint32) self->prop.qp_p, min_qp_caps, max_qp_caps);
    self->rc.qp_b = CLAMP ((gint32) self->prop.qp_b, min_qp_caps, max_qp_caps);
    GST_OBJECT_UNLOCK (self);
  } else {
    self->rc.qp_i = 0;
    self->rc.qp_p = 0;
    self->rc.qp_b = 0;
  }

  update_property_uint (self, &self->prop.qp_i, self->rc.qp_i, PROP_QP_I);
  update_property_uint (self, &self->prop.qp_p, self->rc.qp_p, PROP_QP_P);
  update_property_uint (self, &self->prop.qp_b, self->rc.qp_b, PROP_QP_B);
  update_property_uint (self, &self->prop.min_qp, self->rc.min_qp, PROP_MIN_QP);
  update_property_uint (self, &self->prop.max_qp, self->rc.max_qp, PROP_MAX_QP);
}

static gboolean
_get_vk_caps (GstVulkanH265Encoder * self,
    GstVulkanVideoProfile * profile, GstVulkanVideoCapabilities * vkcaps)
{
  GstVulkanPhysicalDevice *gpu = self->device->physical_device;
  GError *err = NULL;

  if (!gst_vulkan_video_get_capabilities (gpu, profile, vkcaps, &err)) {
    if (err) {
      GST_ERROR_OBJECT (self, "Error getting video capabilites: %s",
          err->message);
      g_clear_error (&err);
    }
    return FALSE;
  }

  return TRUE;
}

static gboolean
_get_vk_video_params (GstH265ProfileIDC profile_idc, GstVideoInfo * in_info,
    StdVideoH265ProfileIdc * vk_profile,
    VkVideoChromaSubsamplingFlagBitsKHR * chroma_subsampling,
    VkVideoComponentBitDepthFlagsKHR * bit_depth_luma,
    VkVideoComponentBitDepthFlagsKHR * bit_depth_chroma)
{
  *chroma_subsampling = _h265_get_chroma_subsampling (in_info);

  *bit_depth_luma =
      gst_vulkan_bit_depth (GST_VIDEO_INFO_COMP_DEPTH (in_info, 0));
  if (*bit_depth_luma == VK_VIDEO_COMPONENT_BIT_DEPTH_INVALID_KHR)
    return FALSE;

  *bit_depth_chroma =
      gst_vulkan_bit_depth (GST_VIDEO_INFO_COMP_DEPTH (in_info, 1));
  if (*bit_depth_chroma == VK_VIDEO_COMPONENT_BIT_DEPTH_INVALID_KHR)
    return FALSE;

  *vk_profile = gst_vulkan_h265_profile_idc (profile_idc);
  if (*vk_profile == STD_VIDEO_H265_PROFILE_IDC_INVALID)
    return FALSE;

  return TRUE;
}

static void
_build_profile (StdVideoH265ProfileIdc vk_profile,
    VkVideoChromaSubsamplingFlagBitsKHR chroma_subsampling,
    VkVideoComponentBitDepthFlagsKHR bit_depth_luma,
    VkVideoComponentBitDepthFlagsKHR bit_depth_chroma,
    GstVulkanVideoProfile * profile)
{
  *profile = (GstVulkanVideoProfile) {
    /* *INDENT-OFF* */
    .profile = (VkVideoProfileInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
      .pNext = &profile->usage.encode,
      .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR,
      .chromaSubsampling = chroma_subsampling,
      .chromaBitDepth = bit_depth_chroma,
      .lumaBitDepth = bit_depth_luma,
    },
    .usage.encode = (VkVideoEncodeUsageInfoKHR) {
      .pNext = &profile->codec.h265enc,
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_USAGE_INFO_KHR,
      .videoUsageHints = VK_VIDEO_ENCODE_USAGE_DEFAULT_KHR,
      .videoContentHints = VK_VIDEO_ENCODE_CONTENT_DEFAULT_KHR,
      .tuningMode = VK_VIDEO_ENCODE_TUNING_MODE_DEFAULT_KHR,
    },
    .codec.h265enc = (VkVideoEncodeH265ProfileInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PROFILE_INFO_KHR,
      .stdProfileIdc = vk_profile,
    },
    /* *INDENT-ON* */
  };
}

static inline void
_set_tier_flag (GstH265ProfileTierLevel * ptl, guint bitrate,
    const GstH265LevelDescriptor * desc)
{
  /* 0xff means GST_H265_TIER_AUTO */
  if (ptl->tier_flag != 0xff)
    return;

  if (desc->max_br_tier_high == 0 || bitrate <= desc->max_br_tier_main)
    ptl->tier_flag = 0;
  else
    ptl->tier_flag = 1;
}

static const GstH265LevelDescriptor *
_pick_level_desc (GstVulkanH265Encoder * self, GstH265ProfileTierLevel * ptl,
    guint bitrate, GstVideoInfo * info, guint dpb_size)
{
  const GstH265LevelDescriptor *desc;
  gboolean tier_auto = ptl->tier_flag == 0xff;

  if (tier_auto)
    ptl->tier_flag = 0;         /* main by default */

  desc = gst_h265_get_level_descriptor (ptl, bitrate, info, 1, 1, 1, dpb_size);
  if (!desc)
    GST_WARNING_OBJECT (self, "Cannot find a suitable H.265 level descriptor");

  if (tier_auto)
    ptl->tier_flag = 0xff;      /* back to auto */

  return desc;
}

static GstFlowReturn
gst_vulkan_h265_encoder_new_sequence (GstH265Encoder * encoder,
    GstVideoCodecState * in_state, GstH265ProfileTierLevel * ptl)
{
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (encoder);
  GError *err = NULL;
  GstVideoInfo *in_info = &in_state->info;
  VkVideoChromaSubsamplingFlagBitsKHR chroma_subsampling;
  VkVideoComponentBitDepthFlagsKHR bit_depth_luma, bit_depth_chroma;
  StdVideoH265ProfileIdc vk_profile;
  GstVulkanVideoCapabilities vk_caps;
  VkVideoEncodeH265CapabilitiesKHR *vk_h265_caps;
  StdVideoH265LevelIdc vk_max_level;
  VkVideoEncodeH265SessionCreateInfoKHR vk_h265_session;
  gpointer session_create = NULL;
  GstVulkanVideoProfile profile_tmp;
  guint dpb_size = 0;
  gboolean skip_start = TRUE;

  if (!self->encoder) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("The vulkan encoder has not been initialized properly"), (NULL));
    return GST_FLOW_ERROR;
  }

  /* profile configuration */
  if (!_get_vk_video_params (ptl->profile_idc, &in_state->info, &vk_profile,
          &chroma_subsampling, &bit_depth_luma, &bit_depth_chroma))
    return GST_FLOW_ERROR;

  skip_start &= (self->profile.profile.chromaSubsampling == chroma_subsampling
      && self->profile.profile.chromaBitDepth == bit_depth_chroma
      && self->profile.profile.lumaBitDepth == bit_depth_luma
      && self->profile.codec.h265enc.stdProfileIdc == vk_profile);

  _build_profile (vk_profile, chroma_subsampling, bit_depth_luma,
      bit_depth_chroma, &profile_tmp);

  if (!_get_vk_caps (self, &profile_tmp, &vk_caps))
    return GST_FLOW_ERROR;

  self->std_syntax_flags = vk_caps.encoder.codec.h265.stdSyntaxFlags;
  self->offset_alignment = vk_caps.caps.minBitstreamBufferOffsetAlignment;

  self->coded_width = GST_ROUND_UP_N (GST_VIDEO_INFO_WIDTH (in_info),
      vk_caps.encoder.caps.encodeInputPictureGranularity.width);
  self->coded_height = GST_ROUND_UP_N (GST_VIDEO_INFO_HEIGHT (in_info),
      vk_caps.encoder.caps.encodeInputPictureGranularity.height);

  gst_h265_encoder_set_surface_size (encoder, self->coded_width,
      self->coded_height);

  gst_h265_encoder_set_max_num_references (encoder,
      vk_caps.encoder.codec.h265.maxPPictureL0ReferenceCount,
      vk_caps.encoder.codec.h265.maxL1ReferenceCount);

  /* B-pyramid requires B pictures as references in both lists. */
  gst_h265_encoder_set_b_pyramid_supported (encoder,
      (vk_caps.encoder.codec.h265.flags &
          (VK_VIDEO_ENCODE_H265_CAPABILITY_B_FRAME_IN_L0_LIST_BIT_KHR |
              VK_VIDEO_ENCODE_H265_CAPABILITY_B_FRAME_IN_L1_LIST_BIT_KHR)) ==
      (VK_VIDEO_ENCODE_H265_CAPABILITY_B_FRAME_IN_L0_LIST_BIT_KHR |
          VK_VIDEO_ENCODE_H265_CAPABILITY_B_FRAME_IN_L1_LIST_BIT_KHR));

  if (!gst_h265_encoder_generate_gop_structure (encoder, &dpb_size)) {
    GST_WARNING_OBJECT (self, "Could not generate GOP structure");
    return GST_FLOW_ERROR;
  }

  if (dpb_size > vk_caps.caps.maxDpbSlots) {
    GST_WARNING_OBJECT (self, "DPB size (%u) is bigger than available slots "
        "(%u)", dpb_size, vk_caps.caps.maxDpbSlots);
    return GST_FLOW_ERROR;
  }

  if (self->rc.bitrate == 0) {
    const GstH265LevelDescriptor *desc =
        _pick_level_desc (self, ptl, 0, &in_state->info, dpb_size);

    if (!desc)
      return GST_FLOW_ERROR;

    _set_tier_flag (ptl, 0, desc);

    self->rc.bitrate = (ptl->tier_flag == 1)
        ? desc->max_br_tier_high : desc->max_br_tier_main;
  }

  if (ptl->level_idc == 0) {
    const GstH265LevelDescriptor *desc =
        _pick_level_desc (self, ptl, self->rc.bitrate, &in_state->info,
        dpb_size);

    if (!desc)
      return GST_FLOW_ERROR;

    _set_tier_flag (ptl, self->rc.bitrate, desc);

    ptl->level_idc = desc->level_idc;
  }

  if (ptl->tier_flag == 0xff) {
    const GstH265LevelDescriptor *desc =
        _pick_level_desc (self, ptl, self->rc.bitrate, &in_state->info,
        dpb_size);

    if (!desc)
      return GST_FLOW_ERROR;

    _set_tier_flag (ptl, self->rc.bitrate, desc);
  }

  vk_max_level = vk_caps.encoder.codec.h265.maxLevelIdc;
  if (vk_max_level != STD_VIDEO_H265_LEVEL_IDC_INVALID) {
    gint max_level = gst_h265_level_idc_from_vk (vk_max_level);
    if (max_level > 0)
      ptl->level_idc = MIN (max_level, ptl->level_idc);
  }

  vk_max_level = gst_vulkan_h265_level_idc (ptl->level_idc);
  if (vk_max_level == STD_VIDEO_H265_LEVEL_IDC_INVALID) {
    /* levels 6.3 and 7.x cannot be expressed with the Vulkan-Headers in use
     * (see H265LevelMap). Fall back to the highest expressible level instead
     * of dropping the whole profile_tier_level from the std VPS/SPS. */
    ptl->level_idc = H265LevelMap[G_N_ELEMENTS (H265LevelMap) - 1].gst;
    vk_max_level = H265LevelMap[G_N_ELEMENTS (H265LevelMap) - 1].vk;
  }
  skip_start &= self->level_idc == vk_max_level;

  /* update quality and rate control since they might changed */
  {
    /* Designated initializer: all members not listed (e.g. pNext) are
     * zero-initialized, so the output struct has no garbage in its chain. */
    GstVulkanEncoderQualityProperties qprop = {
      .codec.h265 = {
          .sType =
            VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_QUALITY_LEVEL_PROPERTIES_KHR}
    };

    _reset_rc_props (self, &vk_caps);
    /* configure rate control right after setting rate control mode */
    _configure_rate_control (self, &vk_caps);

    /* quality set should go after setting the rate control mode */
    self->rc.quality =
        MIN (self->rc.quality, vk_caps.encoder.caps.maxQualityLevels - 1);
    if (!gst_vulkan_encoder_set_quality_level (self->encoder,
            self->rc.quality, &profile_tmp, &vk_caps, &qprop, &err)) {
      GST_ERROR_OBJECT (self, "Unable to set encoder quality: %s",
          err->message);
      g_clear_error (&err);
      self->rc.quality = gst_vulkan_encoder_quality_level (self->encoder);
    }

    update_property_uint (self, &self->prop.quality, self->rc.quality,
        PROP_QUALITY);
  }

  if (gst_h265_encoder_is_live (encoder)) {
    /* low latency */
    gst_h265_encoder_set_preferred_output_delay (encoder, 0);
  } else {
    /* experimental best value for VA */
    gst_h265_encoder_set_preferred_output_delay (encoder, 4);
  }

  self->level_idc = vk_max_level;

  if (gst_vulkan_encoder_is_started (self->encoder)) {
    if (skip_start) {
      /* if the video info didn't change, don't renegotiate */
      if (self->in_state
          && gst_video_info_is_equal (&self->in_state->info, &in_state->info))
        return GST_FLOW_OK;
      goto renegotiate;
    } else {
      GST_DEBUG_OBJECT (self, "Restarting vulkan encoder");
      gst_vulkan_encoder_stop (self->encoder);
    }
  }

  {
    /* *INDENT-OFF* */
    vk_h265_session = (VkVideoEncodeH265SessionCreateInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_CREATE_INFO_KHR,
      .useMaxLevelIdc = VK_TRUE,
      .maxLevelIdc = vk_max_level,
    };
    /* *INDENT-ON* */

    session_create = &vk_h265_session;

    /* copy profile and fix chaining pointers */
    self->profile = profile_tmp;
    self->profile.profile.pNext = &self->profile.usage.encode;
    self->profile.usage.encode.pNext = &self->profile.codec.h265enc;
  }

  if (!gst_vulkan_encoder_start (self->encoder, &self->profile,
          session_create, &err)) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Unable to start vulkan encoder with error %s", err->message), (NULL));
    g_clear_error (&err);
    return GST_FLOW_ERROR;
  }

  if (!gst_vulkan_encoder_caps (self->encoder, &vk_caps)) {
    GST_ERROR_OBJECT (self, "Couldn't get Vulkan Capabilities");
    return GST_FLOW_ERROR;
  }
  vk_h265_caps = &vk_caps.encoder.codec.h265;

  GST_LOG_OBJECT (self, "H.265 encoder capabilities:\n"
      "    Standard capability flags:\n"
      "        separate color plane: %i\n"
      "        sample adaptive offset enabled: %i\n"
      "        scaling list data present: %i\n"
      "        pcm enabled: %i\n"
      "        sps temporal mvp enabled: %i\n"
      "        init qp: %i\n"
      "        weighted:%s%s\n"
      "        parallel merge level: %i\n"
      "        sign data hiding: %i\n"
      "        transform skip:%s%s\n"
      "        pps slice chroma qp offsets present: %i\n"
      "        transquant bypass enabled: %i\n"
      "        constrained intra pred: %i\n"
      "        entropy coding sync: %i\n"
      "        deblocking filter override: %i\n"
      "        dependent slice segments:%s%s\n"
      "        slice qp delta: %i\n"
      "        different slice qp delta: %i\n"
      "    Capability flags:\n"
      "        hrd compliance: %i\n"
      "        prediction weight table generated: %i\n"
      "        row unaligned slice: %i\n"
      "        different slice segment type: %i\n"
      "        b frame in l0 list: %i\n"
      "        b frame in l1 list: %i\n"
      "        per picture type min max qp: %i\n"
      "        per slice segment constant qp: %i\n"
      "        multiple tiles per slice segment: %i\n"
      "        multiple slice segments per title: %i\n"
      "        b picture infra refresh: %i\n"
      "        cu qp diff wraparound: %i\n"
      "    Capabilities:\n"
      "        maxLevelIdc: %i\n"
      "        maxSliceSegmentCount: %i\n"
      "        maxTiles: %ix%i\n"
      "        cbtSizes: 0x%x\n"
      "        transformBlockSizes: 0x%x\n"
      "        max(P/B)PictureL0ReferenceCount: %i P's; %i B's\n"
      "        maxL1ReferenceCount: %i\n"
      "        maxSubLayerCount: %i\n"
      "        expectDyadicTemporalSubLayerPattern: %i\n"
      "        min/max Qp: %i P's, %i B's\n"
      "        prefersGopRemainingFrames: %i\n"
      "        requiresGopRemainingFrames: %i",
      !!(vk_h265_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H265_STD_SEPARATE_COLOR_PLANE_FLAG_SET_BIT_KHR),
      !!(vk_h265_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H265_STD_SAMPLE_ADAPTIVE_OFFSET_ENABLED_FLAG_SET_BIT_KHR),
      !!(vk_h265_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H265_STD_SCALING_LIST_DATA_PRESENT_FLAG_SET_BIT_KHR),
      !!(vk_h265_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H265_STD_PCM_ENABLED_FLAG_SET_BIT_KHR),
      !!(vk_h265_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H265_STD_SPS_TEMPORAL_MVP_ENABLED_FLAG_SET_BIT_KHR),
      !!(vk_h265_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H265_STD_INIT_QP_MINUS26_BIT_KHR),
      vk_h265_caps->stdSyntaxFlags &
      VK_VIDEO_ENCODE_H265_STD_WEIGHTED_PRED_FLAG_SET_BIT_KHR ? " pred" : "",
      vk_h265_caps->stdSyntaxFlags &
      VK_VIDEO_ENCODE_H265_STD_WEIGHTED_BIPRED_FLAG_SET_BIT_KHR ? " bipred" :
      "",
      !!(vk_h265_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H265_STD_LOG2_PARALLEL_MERGE_LEVEL_MINUS2_BIT_KHR),
      !!(vk_h265_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H265_STD_SIGN_DATA_HIDING_ENABLED_FLAG_SET_BIT_KHR),
      vk_h265_caps->stdSyntaxFlags &
      VK_VIDEO_ENCODE_H265_STD_TRANSFORM_SKIP_ENABLED_FLAG_SET_BIT_KHR ?
      " set" : "",
      vk_h265_caps->stdSyntaxFlags &
      VK_VIDEO_ENCODE_H265_STD_TRANSFORM_SKIP_ENABLED_FLAG_UNSET_BIT_KHR ?
      " unset" : "",
      !!(vk_h265_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H265_STD_PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT_FLAG_SET_BIT_KHR),
      !!(vk_h265_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H265_STD_TRANSQUANT_BYPASS_ENABLED_FLAG_SET_BIT_KHR),
      !!(vk_h265_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H265_STD_CONSTRAINED_INTRA_PRED_FLAG_SET_BIT_KHR),
      !!(vk_h265_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H265_STD_ENTROPY_CODING_SYNC_ENABLED_FLAG_SET_BIT_KHR),
      !!(vk_h265_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H265_STD_DEBLOCKING_FILTER_OVERRIDE_ENABLED_FLAG_SET_BIT_KHR),
      vk_h265_caps->stdSyntaxFlags &
      VK_VIDEO_ENCODE_H265_STD_DEPENDENT_SLICE_SEGMENTS_ENABLED_FLAG_SET_BIT_KHR
      ? " enabled" : "",
      vk_h265_caps->stdSyntaxFlags &
      VK_VIDEO_ENCODE_H265_STD_DEPENDENT_SLICE_SEGMENT_FLAG_SET_BIT_KHR ?
      " set" : "",
      !!(vk_h265_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H265_STD_SLICE_QP_DELTA_BIT_KHR),
      !!(vk_h265_caps->stdSyntaxFlags &
          VK_VIDEO_ENCODE_H265_STD_DIFFERENT_SLICE_QP_DELTA_BIT_KHR),
      !!(vk_h265_caps->flags &
          VK_VIDEO_ENCODE_H265_CAPABILITY_HRD_COMPLIANCE_BIT_KHR),
      !!(vk_h265_caps->flags &
          VK_VIDEO_ENCODE_H265_CAPABILITY_PREDICTION_WEIGHT_TABLE_GENERATED_BIT_KHR),
      !!(vk_h265_caps->flags &
          VK_VIDEO_ENCODE_H265_CAPABILITY_ROW_UNALIGNED_SLICE_SEGMENT_BIT_KHR),
      !!(vk_h265_caps->flags &
          VK_VIDEO_ENCODE_H265_CAPABILITY_DIFFERENT_SLICE_SEGMENT_TYPE_BIT_KHR),
      !!(vk_h265_caps->flags &
          VK_VIDEO_ENCODE_H265_CAPABILITY_B_FRAME_IN_L0_LIST_BIT_KHR),
      !!(vk_h265_caps->flags &
          VK_VIDEO_ENCODE_H265_CAPABILITY_B_FRAME_IN_L1_LIST_BIT_KHR),
      !!(vk_h265_caps->flags &
          VK_VIDEO_ENCODE_H265_CAPABILITY_PER_PICTURE_TYPE_MIN_MAX_QP_BIT_KHR),
      !!(vk_h265_caps->flags &
          VK_VIDEO_ENCODE_H265_CAPABILITY_PER_SLICE_SEGMENT_CONSTANT_QP_BIT_KHR),
      !!(vk_h265_caps->flags &
          VK_VIDEO_ENCODE_H265_CAPABILITY_MULTIPLE_TILES_PER_SLICE_SEGMENT_BIT_KHR),
      !!(vk_h265_caps->flags &
          VK_VIDEO_ENCODE_H265_CAPABILITY_MULTIPLE_SLICE_SEGMENTS_PER_TILE_BIT_KHR),
      !!(vk_h265_caps->flags &
          VK_VIDEO_ENCODE_H265_CAPABILITY_B_PICTURE_INTRA_REFRESH_BIT_KHR),
      !!(vk_h265_caps->flags &
          VK_VIDEO_ENCODE_H265_CAPABILITY_CU_QP_DIFF_WRAPAROUND_BIT_KHR),
      vk_h265_caps->maxLevelIdc, vk_h265_caps->maxSliceSegmentCount,
      vk_h265_caps->maxTiles.width, vk_h265_caps->maxTiles.height,
      vk_h265_caps->ctbSizes, vk_h265_caps->transformBlockSizes,
      vk_h265_caps->maxPPictureL0ReferenceCount,
      vk_h265_caps->maxBPictureL0ReferenceCount,
      vk_h265_caps->maxL1ReferenceCount, vk_h265_caps->maxSubLayerCount,
      vk_h265_caps->expectDyadicTemporalSubLayerPattern, vk_h265_caps->maxQp,
      vk_h265_caps->minQp, vk_h265_caps->prefersGopRemainingFrames,
      vk_h265_caps->requiresGopRemainingFrames);

  /* validate the rounded coded size, which is what actually gets encoded */
  if (self->coded_width > vk_caps.caps.maxCodedExtent.width
      || self->coded_height > vk_caps.caps.maxCodedExtent.height
      || self->coded_width < vk_caps.caps.minCodedExtent.width
      || self->coded_height < vk_caps.caps.minCodedExtent.height) {
    GST_ERROR_OBJECT (self, "Frame size is out of driver limits");
    gst_vulkan_encoder_stop (self->encoder);
    return GST_FLOW_NOT_NEGOTIATED;
  }

renegotiate:
  if (self->in_state)
    gst_video_codec_state_unref (self->in_state);
  self->in_state = gst_video_codec_state_ref (in_state);

  return GST_FLOW_OK;
}

static gboolean
_h265_parameters_parse (GstVulkanH265Encoder * self, gconstpointer data,
    gsize data_size, GstH265VPS * vps, GstH265SPS * sps,
    GstH265SPSEXT * sps_ext, GstH265PPS * pps)
{
  GstH265ParserResult res, pres;
  GstH265NalUnit nalu = { 0, };
  GstH265Parser parser = { 0, };
  guint offset = 0;

  if (data_size == 0) {
    GST_WARNING_OBJECT (self, "No overridden parameters to parse");
    return FALSE;
  }

  /* NO_NAL_END means the last NAL has no following start code, so stop after
   * parsing it; any other non-OK identify result is a hard failure. */
  do {
    if (offset >= data_size)
      break;

    res =
        gst_h265_parser_identify_nalu (&parser, data, offset, data_size, &nalu);

    if (res == GST_H265_PARSER_NO_NAL_END)
      res = GST_H265_PARSER_OK;

    if (res != GST_H265_PARSER_OK) {
      GST_WARNING_OBJECT (self, "Failed to parse overridden parameters");
      return FALSE;
    }

    switch (nalu.type) {
      case GST_H265_NAL_VPS:
        pres = gst_h265_parser_parse_vps (&parser, &nalu, vps);
        if (pres != GST_H265_PARSER_OK) {
          GST_ERROR_OBJECT (self, "Failed to parse overridden VPS");
          return FALSE;
        }
        break;
      case GST_H265_NAL_SPS:
        pres = gst_h265_parser_parse_sps_ext (&parser, &nalu, sps, sps_ext,
            TRUE);
        if (pres != GST_H265_PARSER_OK) {
          GST_ERROR_OBJECT (self, "Failed to parse overridden SPS");
          return FALSE;
        }
        break;
      case GST_H265_NAL_PPS:
        pres = gst_h265_parser_parse_pps (&parser, &nalu, pps);
        if (pres != GST_H265_PARSER_OK) {
          GST_ERROR_OBJECT (self, "Failed to parse overridden PPS");
          return FALSE;
        }
        break;
      default:
        GST_ERROR_OBJECT (self, "Unexpected NAL identified: %d", nalu.type);
    }

    if (nalu.size == 0) {
      GST_WARNING_OBJECT (self, "Zero-sized NAL at offset %u, aborting parse",
          offset);
      break;
    }
    offset = nalu.offset + nalu.size;

  } while (res == GST_H265_PARSER_OK);

  /* Drop the parsed VPS/SPS/PPS cached in the parser, mirroring the H.264
   * backend cleanup, to avoid keeping stale state in the reused parser. */
  memset (&parser.vps, 0, sizeof (parser.vps));
  memset (&parser.sps, 0, sizeof (parser.sps));
  memset (&parser.pps, 0, sizeof (parser.pps));

  return TRUE;
}

static GstFlowReturn
gst_vulkan_h265_encoder_update_parameters (GstVulkanH265Encoder * self,
    GstH265VPS * vps, GstH265SPS * sps, GstH265PPS * pps)
{
  GError *err = NULL;
  GstVulkanEncoderParameters params;
  VkVideoEncodeH265SessionParametersAddInfoKHR params_add;

  _fill_vps (vps, &self->params.vps);
  _fill_sps (self, sps, &self->params.sps);
  _fill_pps (self, pps, &self->params.pps);

  /* *INDENT-OFF* */
  params_add = (VkVideoEncodeH265SessionParametersAddInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_ADD_INFO_KHR,
    .pStdVPSs = &self->params.vps.vps,
    .stdVPSCount = 1,
    .pStdSPSs = &self->params.sps.sps,
    .stdSPSCount = 1,
    .pStdPPSs = &self->params.pps.pps,
    .stdPPSCount = 1,
  };
  params.h265 = (VkVideoEncodeH265SessionParametersCreateInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_CREATE_INFO_KHR,
    .maxStdVPSCount = params_add.stdVPSCount,
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
gst_vulkan_h265_encoder_new_parameters (GstH265Encoder * encoder,
    GstH265VPS * vps, GstH265SPS * sps, GstH265PPS * pps)
{
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (encoder);
  GError *err = NULL;
  GstVulkanEncoderParametersOverrides overrides;
  GstVulkanEncoderParametersFeedback feedback;
  GstFlowReturn ret;
  gpointer data = NULL;
  gsize data_size = 0;

  if (!self->encoder) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("The vulkan encoder has not been initialized properly"), (NULL));
    return GST_FLOW_ERROR;
  }

  ret = gst_vulkan_h265_encoder_update_parameters (self, vps, sps, pps);
  if (ret != GST_FLOW_OK)
    return ret;

  overrides = (GstVulkanEncoderParametersOverrides) {
    .h265 = {
      .sType =
          VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_GET_INFO_KHR,
      .stdVPSId = self->params.vps.vps.vps_video_parameter_set_id,
      .stdSPSId = self->params.sps.sps.sps_seq_parameter_set_id,
      .stdPPSId = self->params.pps.pps.pps_pic_parameter_set_id,
      .writeStdVPS = VK_TRUE,
      .writeStdSPS = VK_TRUE,
      .writeStdPPS = VK_TRUE,
    }
  };

  feedback = (GstVulkanEncoderParametersFeedback) {
    .h265 = {
      .sType =
          VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_FEEDBACK_INFO_KHR,
    }
  };

  if (!gst_vulkan_encoder_video_session_parameters_overrides (self->encoder,
          &overrides, &feedback, &data_size, &data, &err)) {
    GST_ERROR_OBJECT (self, "Unable to get overriden parameters: %s",
        err ? err->message : "");
    g_clear_error (&err);
    return GST_FLOW_ERROR;
  }

  if (feedback.h265.hasStdVPSOverrides || feedback.h265.hasStdSPSOverrides ||
      feedback.h265.hasStdPPSOverrides) {
    GstH265VPS new_vps = { 0, };
    GstH265SPS new_sps = { 0, };
    GstH265PPS new_pps = { 0, };
    GstH265SPSEXT new_sps_ext = { 0, };

    GST_LOG_OBJECT (self, "Vulkan driver overrode parameters:%s%s%s",
        feedback.h265.hasStdVPSOverrides ? " VPS" : "",
        feedback.h265.hasStdSPSOverrides ? " SPS" : "",
        feedback.h265.hasStdPPSOverrides ? " PPS" : "");

    if (!_h265_parameters_parse (self, data, data_size, &new_vps, &new_sps,
            &new_sps_ext, &new_pps)) {
      GST_ELEMENT_ERROR (self, RESOURCE, READ,
          ("Unable to parse overriden parameters"), (NULL));
      g_free (data);
      return GST_FLOW_ERROR;
    }

    if (feedback.h265.hasStdVPSOverrides)
      *vps = new_vps;
    if (feedback.h265.hasStdSPSOverrides)
      *sps = new_sps;
    if (feedback.h265.hasStdPPSOverrides)
      *pps = new_pps;
  }

  g_free (data);

  /* copy it to calculate coded buffer size */
  self->vps = *vps;
  self->sps = *sps;
  self->pps = *pps;
  self->pps.sps = &self->sps;
  self->sps.vps = &self->vps;

  {
    GstVideoEncoder *vencoder = GST_VIDEO_ENCODER_CAST (self);
    GstCaps *caps;
    GstVideoInfo *info = &self->in_state->info;
    const char *profile, *level, *tier;
    GstVideoCodecState *out_state;

    profile =
        gst_h265_profile_to_string (gst_h265_profile_tier_level_get_profile
        (&self->vps.profile_tier_level));
    if (!profile)
      return GST_FLOW_ERROR;
    level =
        gst_vulkan_h265_level_idc_name (self->vps.profile_tier_level.level_idc);
    if (!level)
      return GST_FLOW_ERROR;
    tier = self->vps.profile_tier_level.tier_flag == 1 ? "high" : "main";

    caps =
        gst_caps_new_simple ("video/x-h265", "profile", G_TYPE_STRING, profile,
        "level", G_TYPE_STRING, level, "tier", G_TYPE_STRING, tier, "width",
        G_TYPE_INT, GST_VIDEO_INFO_WIDTH (info), "height", G_TYPE_INT,
        GST_VIDEO_INFO_HEIGHT (info), "alignment", G_TYPE_STRING, "au",
        "stream-format", G_TYPE_STRING, "byte-stream", NULL);

    out_state = gst_video_encoder_get_output_state (vencoder);
    if (out_state) {
      gboolean early_return = FALSE;

      if (out_state->caps)
        early_return = gst_caps_is_subset (out_state->caps, caps);
      gst_video_codec_state_unref (out_state);
      if (early_return) {
        gst_caps_unref (caps);
        return GST_FLOW_OK;
      }
    }

    out_state =
        gst_video_encoder_set_output_state (vencoder, caps, self->in_state);
    gst_video_codec_state_unref (out_state);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vulkan_h265_encoder_new_output (GstH265Encoder * encoder,
    GstVideoCodecFrame * frame, GstH265EncoderFrame * h265_frame)
{
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (encoder);
  GstVulkanH265EncoderFrame *vk_frame;

  vk_frame = gst_vulkan_h265_encoder_frame_new (self, frame);
  if (!vk_frame)
    return GST_FLOW_NOT_NEGOTIATED;

  gst_h265_encoder_frame_set_user_data (h265_frame, vk_frame,
      gst_vulkan_h265_encoder_frame_free);

  return GST_FLOW_OK;
}

#define FILLER_NAL_HEADER_SIZE 7        /* header overhead of a filler NAL, in bytes */

/* converts @nal_buf into the output buffer at @offset, advancing it */
static gboolean
_emit_nal (GstVulkanH265Encoder * self, GstMapInfo * info, guint * offset,
    guint orig_size, guint8 * nal_buf, guint nal_size, const gchar * what)
{
  GstH265BitWriterResult res;
  guint size = orig_size - *offset;

  res = gst_h265_bit_writer_convert_to_nal (4, FALSE, TRUE, FALSE, nal_buf,
      nal_size * 8, info->data + *offset, &size);
  if (res != GST_H265_BIT_WRITER_OK) {
    GST_ERROR_OBJECT (self, "Failed to generate the %s bytes", what);
    return FALSE;
  }

  *offset += size;
  return TRUE;
}

static gboolean
_write_headers (GstVulkanH265Encoder * self,
    GstVulkanH265EncoderFrame * vk_frame)
{
  GstMapInfo info;
  guint aligned_offset, offset, orig_size, fillers;
  GstH265BitWriterResult res;
  guint8 aud_pic_type, nal_buf[4096] = { 0, };
  guint nal_size;
  gboolean aud, ret = FALSE;
  StdVideoH265PictureType pic_type = vk_frame->pic_info.pic_type;
  GstBuffer *buffer = vk_frame->picture.out_buffer;

  if (!gst_buffer_map (buffer, &info, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Failed to map output buffer");
    return FALSE;
  }

  offset = 0;
  orig_size = info.size;

  GST_OBJECT_LOCK (self);
  aud = self->prop.aud;
  GST_OBJECT_UNLOCK (self);

  if (aud) {
    nal_size = sizeof (nal_buf);

    switch (pic_type) {
      case STD_VIDEO_H265_PICTURE_TYPE_IDR:
      case STD_VIDEO_H265_PICTURE_TYPE_I:
        aud_pic_type = 0;
        break;
      case STD_VIDEO_H265_PICTURE_TYPE_P:
        aud_pic_type = 1;
        break;
      case STD_VIDEO_H265_PICTURE_TYPE_B:
        aud_pic_type = 2;
        break;
      default:
        GST_ERROR_OBJECT (self,
            "Unexpected picture type %d for AUD, using I type", pic_type);
        aud_pic_type = 0;
        break;
    }

    res = gst_h265_bit_writer_aud (aud_pic_type, TRUE, nal_buf, &nal_size);
    if (res != GST_H265_BIT_WRITER_OK) {
      GST_ERROR_OBJECT (self, "Failed to generate the AUD header");
      goto bail;
    }

    if (!_emit_nal (self, &info, &offset, orig_size, nal_buf, nal_size, "AUD"))
      goto bail;
  }

  if (pic_type == STD_VIDEO_H265_PICTURE_TYPE_IDR) {
    /* Write VPS */
    nal_size = sizeof (nal_buf);
    res = gst_h265_bit_writer_vps (&self->vps, TRUE, nal_buf, &nal_size);
    if (res != GST_H265_BIT_WRITER_OK) {
      GST_ERROR_OBJECT (self, "Failed to generate the VPS header");
      goto bail;
    }

    if (!_emit_nal (self, &info, &offset, orig_size, nal_buf, nal_size, "VPS"))
      goto bail;

    /* Write SPS */
    /* TODO: support GstH265SPSEXT */
    nal_size = sizeof (nal_buf);
    res = gst_h265_bit_writer_sps (&self->sps, TRUE, nal_buf, &nal_size);
    if (res != GST_H265_BIT_WRITER_OK) {
      GST_ERROR_OBJECT (self, "Failed to generate the sequence header");
      goto bail;
    }

    if (!_emit_nal (self, &info, &offset, orig_size, nal_buf, nal_size, "SPS"))
      goto bail;
  }

  if (pic_type == STD_VIDEO_H265_PICTURE_TYPE_I
      || pic_type == STD_VIDEO_H265_PICTURE_TYPE_IDR) {
    nal_size = sizeof (nal_buf);
    res = gst_h265_bit_writer_pps (&self->pps, TRUE, nal_buf, &nal_size);
    if (res != GST_H265_BIT_WRITER_OK) {
      GST_ERROR_OBJECT (self, "Failed to generate the picture header");
      goto bail;
    }

    if (!_emit_nal (self, &info, &offset, orig_size, nal_buf, nal_size, "PPS"))
      goto bail;
  }

  aligned_offset = GST_ROUND_UP_N (offset, self->offset_alignment);

  fillers = aligned_offset - offset;
  if (fillers > 0) {
    guint filler_target;

    /* A filler NAL can't be smaller than its header overhead; if the gap is
     * too small, grow it by whole alignment units until a NAL fits. */
    while (fillers < FILLER_NAL_HEADER_SIZE)
      fillers += self->offset_alignment;

    /* Pin the end of the headers to the aligned offset the driver expects,
     * rather than relying on the emitted filler NAL's exact byte count. */
    filler_target = offset + fillers;
    if (filler_target > orig_size) {
      GST_ERROR_OBJECT (self,
          "Not enough space for filler NAL: need %u, have %u", filler_target,
          orig_size);
      goto bail;
    }

    fillers -= FILLER_NAL_HEADER_SIZE;

    nal_size = sizeof (nal_buf);
    res = gst_h265_bit_writer_filler (TRUE, fillers, nal_buf, &nal_size);
    if (res != GST_H265_BIT_WRITER_OK) {
      GST_ERROR_OBJECT (self, "Failed to generate fillers");
      goto bail;
    }

    if (!_emit_nal (self, &info, &offset, orig_size, nal_buf, nal_size,
            "fillers"))
      goto bail;

    offset = filler_target;
  }

  vk_frame->picture.bitstream_header_size = offset;

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
  GstVulkanH265Encoder *self = data;
  GstVulkanH265EncoderFrame *vk_frame = (GstVulkanH265EncoderFrame *) pic;
  GstH265Encoder *h265enc = GST_H265_ENCODER (self);
  guint32 idr_period, num_bframes;
  gboolean b_pyramid;
  VkVideoEncodeH265RateControlFlagsKHR rc_flag;

  idr_period = gst_h265_encoder_get_idr_period (h265enc);
  num_bframes = gst_h265_encoder_get_num_b_frames (h265enc);
  b_pyramid = gst_h265_encoder_gop_is_b_pyramid (h265enc);

  rc_flag = b_pyramid
      ? VK_VIDEO_ENCODE_H265_RATE_CONTROL_REFERENCE_PATTERN_DYADIC_BIT_KHR
      : VK_VIDEO_ENCODE_H265_RATE_CONTROL_REFERENCE_PATTERN_FLAT_BIT_KHR;

  /* *INDENT-OFF* */
  vk_frame->vkrc_info = (VkVideoEncodeH265RateControlInfoKHR){
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_RATE_CONTROL_INFO_KHR,
    .flags = rc_flag | VK_VIDEO_ENCODE_H265_RATE_CONTROL_REGULAR_GOP_BIT_KHR,
    .pNext = NULL,
    .gopFrameCount = idr_period,
    .idrPeriod = idr_period,
    .consecutiveBFrameCount = num_bframes,
  };
  /* *INDENT-ON* */

  rc_info->pNext = &vk_frame->vkrc_info;

  if (rc_info->rateControlMode !=
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
    rc_info->initialVirtualBufferSizeInMs = self->rc.cpb_size * 3 / 4;

    /* *INDENT-OFF* */
    vk_frame->vkrc_layer_info = (VkVideoEncodeH265RateControlLayerInfoKHR){
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_RATE_CONTROL_LAYER_INFO_KHR,

      .useMinQp = self->rc.min_qp > 0,
      .minQp.qpI = self->rc.min_qp,
      .minQp.qpP = self->rc.min_qp,
      .minQp.qpB = self->rc.min_qp,

      .useMaxQp = self->rc.max_qp > 0,
      .maxQp.qpI = self->rc.max_qp,
      .maxQp.qpP = self->rc.max_qp,
      .maxQp.qpB = self->rc.max_qp,

      .useMaxFrameSize = 0,
    };
    /* *INDENT-ON* */

    rc_layer->pNext = &vk_frame->vkrc_layer_info;
  }
}

static void
_setup_codec_pic (GstVulkanEncoderPicture * pic, VkVideoEncodeInfoKHR * info,
    gpointer data)
{
  GstVulkanH265EncoderFrame *vk_frame = (GstVulkanH265EncoderFrame *) pic;

  info->pNext = &vk_frame->vkpic_info;
  pic->dpb_slot.pNext = &vk_frame->dpb_slot_info;

  /* *INDENT-OFF* */
  vk_frame->vkpic_info = (VkVideoEncodeH265PictureInfoKHR){
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PICTURE_INFO_KHR,
    .pNext = NULL,
    .naluSliceSegmentEntryCount = 1,
    .pNaluSliceSegmentEntries = &vk_frame->vkslice_info, /* filled in _setup_slice() */
    .pStdPictureInfo = &vk_frame->pic_info,              /* filled in encode_frame() */
  };
  vk_frame->dpb_slot_info = (VkVideoEncodeH265DpbSlotInfoKHR){
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_DPB_SLOT_INFO_KHR,
    .pStdReferenceInfo = &vk_frame->ref_info, /* filled in encode_frame() */
  };
  /* *INDENT-ON* */
}

static guint8
_get_slot_index (GArray * list, int i)
{
  GstH265EncoderFrame *h265_frame;

  h265_frame = g_array_index (list, GstH265EncoderFrame *, i);
  return _GET_FRAME (h265_frame)->picture.dpb_slot.slotIndex;
}

static void
_setup_ref_lists (GstH265EncoderFrame * h265_frame,
    GstH265SliceHdr * slice_hdr, GArray * list0, GArray * list1)
{
  int i;
  GstVulkanH265EncoderFrame *vk_frame = _GET_FRAME (h265_frame);

  /* *INDENT-OFF* */
  vk_frame->ref_list_info = (StdVideoEncodeH265ReferenceListsInfo){
    .flags = {
      .ref_pic_list_modification_flag_l0 =
          slice_hdr->ref_pic_list_modification.ref_pic_list_modification_flag_l0,
      .ref_pic_list_modification_flag_l1 =
          slice_hdr->ref_pic_list_modification.ref_pic_list_modification_flag_l1,
      .reserved = 0,
    },
    .num_ref_idx_l0_active_minus1 =
        MIN (slice_hdr->num_ref_idx_l0_active_minus1,
        STD_VIDEO_H265_MAX_NUM_LIST_REF - 1),
    .num_ref_idx_l1_active_minus1 =
        MIN (slice_hdr->num_ref_idx_l1_active_minus1,
        STD_VIDEO_H265_MAX_NUM_LIST_REF - 1),
    .RefPicList0 = { 0, }, /* filled below */
    .RefPicList1 = { 0, }, /* filled below */
    .list_entry_l0 = { 0, }, /* filled below */
    .list_entry_l1 = { 0, }, /* filled below */
  };
  /* *INDENT-ON* */

  for (i = 0; i < STD_VIDEO_H265_MAX_NUM_LIST_REF; i++) {
    if (i < list0->len) {
      vk_frame->ref_list_info.RefPicList0[i] = _get_slot_index (list0, i);
    } else {
      vk_frame->ref_list_info.RefPicList0[i] =
          STD_VIDEO_H265_NO_REFERENCE_PICTURE;
    }

    if (i < list1->len) {
      vk_frame->ref_list_info.RefPicList1[i] = _get_slot_index (list1, i);
    } else {
      vk_frame->ref_list_info.RefPicList1[i] =
          STD_VIDEO_H265_NO_REFERENCE_PICTURE;
    }
  }

  /* Fill list_entry_l0/l1 if modification flags are set */
  if (slice_hdr->ref_pic_list_modification.ref_pic_list_modification_flag_l0) {
    for (i = 0; i < STD_VIDEO_H265_MAX_NUM_LIST_REF; i++) {
      vk_frame->ref_list_info.list_entry_l0[i] =
          slice_hdr->ref_pic_list_modification.list_entry_l0[i];
    }
  }
  if (slice_hdr->ref_pic_list_modification.ref_pic_list_modification_flag_l1) {
    for (i = 0; i < STD_VIDEO_H265_MAX_NUM_LIST_REF; i++) {
      vk_frame->ref_list_info.list_entry_l1[i] =
          slice_hdr->ref_pic_list_modification.list_entry_l1[i];
    }
  }

  vk_frame->pic_info.pRefLists = &vk_frame->ref_list_info;

  if (!GST_H26X_GOP_IS_IDR (&h265_frame->gop)) {
    StdVideoEncodeH265LongTermRefPics *l_srp;
    GstH265ShortTermRefPicSet st_rps = slice_hdr->short_term_ref_pic_sets;

    /* The in-band ST RPS is always fully materialized by the base encoder
     * (inter_ref_pic_set_prediction_flag = 0). A predicted set would resolve
     * delta_idx_minus1 against the SPS list, which is wrong under in-band
     * semantics where it indexes the sets already present in the slice
     * (ITU-T H.265 7.4.8). */
    if (st_rps.inter_ref_pic_set_prediction_flag) {
      GST_WARNING_OBJECT (vk_frame->encoder,
          "Slice ST RPS is inter-predicted but signalled in-band; "
          "forcing non-predicted");
      st_rps.inter_ref_pic_set_prediction_flag = 0;
    }

    vk_frame->pic_info.flags.short_term_ref_pic_set_sps_flag = 0;

    _fill_strps (&st_rps, &vk_frame->short_term_ref_pic_set);

    /* *INDENT-OFF* */
    vk_frame->long_term_ref_pic_set = (StdVideoEncodeH265LongTermRefPics){
        .num_long_term_sps = slice_hdr->num_long_term_sps,
        .num_long_term_pics = slice_hdr->num_long_term_pics,
        .used_by_curr_pic_lt_flag =
            _array_2_bitmask(slice_hdr->used_by_curr_pic_lt_flag, 16),
    };
    /* *INDENT-ON* */

    l_srp = &vk_frame->long_term_ref_pic_set;
    for (int i = 0; i < slice_hdr->num_long_term_sps; i++)
      l_srp->lt_idx_sps[i] = slice_hdr->lt_idx_sps[i];
    for (int i = 0; i < slice_hdr->num_long_term_pics; i++)
      l_srp->poc_lsb_lt[i] = slice_hdr->poc_lsb_lt[i];
    for (int i = 0; i < 16; i++) {
      l_srp->delta_poc_msb_cycle_lt[i] = slice_hdr->delta_poc_msb_cycle_lt[i];
      l_srp->delta_poc_msb_present_flag[i] =
          slice_hdr->delta_poc_msb_present_flag[i];
    }
  } else {
    /* IDR pictures carry no reference pictures; the spec allows NULL here. */
    vk_frame->pic_info.pShortTermRefPicSet = NULL;
    vk_frame->pic_info.pLongTermRefPics = NULL;
    return;
  }

  vk_frame->pic_info.pShortTermRefPicSet = &vk_frame->short_term_ref_pic_set;
  vk_frame->pic_info.pLongTermRefPics = &vk_frame->long_term_ref_pic_set;
}

static void
_fill_weight_table (const GstH265PredWeightTable * wt,
    StdVideoEncodeH265WeightTable * vk)
{
  int i, n = MIN (STD_VIDEO_H265_MAX_NUM_LIST_REF,
      G_N_ELEMENTS (wt->delta_luma_weight_l0));

  /* *INDENT-OFF* */
  *vk = (StdVideoEncodeH265WeightTable) {
    .flags = {
      .luma_weight_l0_flag = _array_2_bitmask (wt->luma_weight_l0_flag, n),
      .chroma_weight_l0_flag =
          _array_2_bitmask (wt->chroma_weight_l0_flag, n),
      .luma_weight_l1_flag = _array_2_bitmask (wt->luma_weight_l1_flag, n),
      .chroma_weight_l1_flag =
          _array_2_bitmask (wt->chroma_weight_l1_flag, n),
    },
    .luma_log2_weight_denom = wt->luma_log2_weight_denom,
    .delta_chroma_log2_weight_denom = wt->delta_chroma_log2_weight_denom,
  };
  /* *INDENT-ON* */

  for (i = 0; i < n; i++) {
    vk->delta_luma_weight_l0[i] = wt->delta_luma_weight_l0[i];
    vk->luma_offset_l0[i] = wt->luma_offset_l0[i];
    vk->delta_luma_weight_l1[i] = wt->delta_luma_weight_l1[i];
    vk->luma_offset_l1[i] = wt->luma_offset_l1[i];
  }

  for (i = 0; i < n; i++) {
    for (int p = 0; p < STD_VIDEO_H265_MAX_CHROMA_PLANES; p++) {
      if (p < G_N_ELEMENTS (wt->delta_chroma_weight_l0[i])) {
        vk->delta_chroma_weight_l0[i][p] = wt->delta_chroma_weight_l0[i][p];
        vk->delta_chroma_offset_l0[i][p] = wt->delta_chroma_offset_l0[i][p];
        vk->delta_chroma_weight_l1[i][p] = wt->delta_chroma_weight_l1[i][p];
        vk->delta_chroma_offset_l1[i][p] = wt->delta_chroma_offset_l1[i][p];
      }
    }
  }
}

static void
_setup_slice (GstVulkanH265Encoder * self,
    GstH265EncoderFrame * h265_frame, GstH265SliceHdr * slice_hdr)
{
  GstVulkanH265EncoderFrame *vk_frame = _GET_FRAME (h265_frame);
  guint8 max_num_merge_cand;
  gint32 constant_qp;

  if (self->rc.ratecontrol ==
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR) {
    constant_qp = (slice_hdr->type == GST_H265_P_SLICE) ? self->rc.qp_p
        : (slice_hdr->type == GST_H265_B_SLICE) ? self->rc.qp_b : self->rc.qp_i;
  } else {
    constant_qp = 0;
  }

  if ((self->std_syntax_flags &
          VK_VIDEO_ENCODE_H265_STD_SLICE_QP_DELTA_BIT_KHR) != 0
      && self->rc.ratecontrol ==
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR) {
    /* only meaningful in CQP; under CBR/VBR the driver controls the QP.
       the delta is relative to the effective PPS init_qp_minus26, i.e.
       the one written in the bitstream, which the driver may have
       overridden */
    slice_hdr->qp_delta = constant_qp - (self->pps.init_qp_minus26 + 26);
  }
  /* we ignore VK_VIDEO_ENCODE_H265_STD_DIFFERENT_SLICE_QP_DELTA_BIT_KHR since
     all the slices have the same QP */

  max_num_merge_cand = 5 - slice_hdr->five_minus_max_num_merge_cand;
  max_num_merge_cand = CLAMP (max_num_merge_cand, 1, 5);

  _fill_weight_table (&slice_hdr->pred_weight_table, &vk_frame->slice_wt);

  /* *INDENT-OFF* */
  vk_frame->slice_hdr = (StdVideoEncodeH265SliceSegmentHeader){
      .flags =
          (StdVideoEncodeH265SliceSegmentHeaderFlags){
              .first_slice_segment_in_pic_flag =
                  slice_hdr->first_slice_segment_in_pic_flag,
              .slice_sao_luma_flag = slice_hdr->sao_luma_flag,
              .slice_sao_chroma_flag = slice_hdr->sao_chroma_flag,
              .num_ref_idx_active_override_flag =
                  slice_hdr->num_ref_idx_active_override_flag,
              .mvd_l1_zero_flag = slice_hdr->mvd_l1_zero_flag,
              .cabac_init_flag = slice_hdr->cabac_init_flag,
              .cu_chroma_qp_offset_enabled_flag =
                  slice_hdr->cu_chroma_qp_offset_enabled_flag,
              .deblocking_filter_override_flag =
                  slice_hdr->deblocking_filter_override_flag,
              .slice_deblocking_filter_disabled_flag =
                  slice_hdr->deblocking_filter_disabled_flag,
              .collocated_from_l0_flag = slice_hdr->collocated_from_l0_flag,
              .slice_loop_filter_across_slices_enabled_flag =
                  slice_hdr->loop_filter_across_slices_enabled_flag,
          },
      .slice_type = gst_vulkan_h265_slice_type(slice_hdr->type),
      .slice_segment_address = slice_hdr->segment_address,
      .collocated_ref_idx = slice_hdr->collocated_ref_idx,
      .MaxNumMergeCand = max_num_merge_cand,
      .slice_cb_qp_offset = slice_hdr->cb_qp_offset,
      .slice_cr_qp_offset = slice_hdr->cr_qp_offset,
      .slice_beta_offset_div2 = slice_hdr->beta_offset_div2,
      .slice_tc_offset_div2 = slice_hdr->tc_offset_div2,
      .slice_act_y_qp_offset = slice_hdr->slice_act_y_qp_offset,
      .slice_act_cb_qp_offset = slice_hdr->slice_act_cb_qp_offset,
      .slice_act_cr_qp_offset = slice_hdr->slice_act_cr_qp_offset,
      .slice_qp_delta = slice_hdr->qp_delta,
      .pWeightTable = &vk_frame->slice_wt,
  };

  vk_frame->vkslice_info = (VkVideoEncodeH265NaluSliceSegmentInfoKHR){
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_NALU_SLICE_SEGMENT_INFO_KHR,
      .pNext = NULL,
      .constantQp = constant_qp,
      .pStdSliceSegmentHeader = &vk_frame->slice_hdr,
  };
  /* *INDENT-ON* */
}

static void
update_properties_unlocked (GstVulkanH265Encoder * self)
{
  if (!self->update_props)
    return;

  GST_OBJECT_UNLOCK (self);
  _reset_rc_props (self, NULL);
  GST_OBJECT_LOCK (self);

  self->update_props = FALSE;
}

static StdVideoH265PictureType
gst_vulkan_h265_picture_type (GstH26XGOPType pic_type, gboolean is_idr)
{
  const StdVideoH265PictureType pic_map[] = {
    [GST_H26X_GOP_TYPE_I] = STD_VIDEO_H265_PICTURE_TYPE_I,
    [GST_H26X_GOP_TYPE_P] = STD_VIDEO_H265_PICTURE_TYPE_P,
    [GST_H26X_GOP_TYPE_B] = STD_VIDEO_H265_PICTURE_TYPE_B,
  };

  if (is_idr)
    return STD_VIDEO_H265_PICTURE_TYPE_IDR;

  if ((guint) pic_type < G_N_ELEMENTS (pic_map))
    return pic_map[pic_type];

  return STD_VIDEO_H265_PICTURE_TYPE_INVALID;
}

static GstFlowReturn
gst_vulkan_h265_encoder_encode_frame (GstH265Encoder * encoder,
    GstVideoCodecFrame * frame, GstH265EncoderFrame * h265_frame,
    GstH265SliceHdr * slice_hdr, GArray * list0, GArray * list1)
{
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (encoder);
  GstVulkanEncoderPicture *ref_pics[STD_VIDEO_H265_MAX_NUM_LIST_REF] = {
    NULL,
  };
  gint i, j;
  GstVulkanH265EncoderFrame *vk_frame = _GET_FRAME (h265_frame);

  if (!gst_vulkan_encoder_is_started (self->encoder))
    return GST_FLOW_NOT_NEGOTIATED;

  GST_OBJECT_LOCK (self);
  update_properties_unlocked (self);
  GST_OBJECT_UNLOCK (self);

  /* *INDENT-OFF* */
  vk_frame->pic_info = (StdVideoEncodeH265PictureInfo) {
    .flags = {
      .is_reference = h265_frame->gop.is_ref,
      .IrapPicFlag = GST_H26X_GOP_IS_IDR(&h265_frame->gop),
      /* FIXME: CRA/BLA IRAP pictures are not representable with the current
       * GOP metadata, so only IDR is flagged as an IRAP. */
      .used_for_long_term_reference = 0,
      .discardable_flag =
          !GST_H26X_GOP_IS_IDR (&h265_frame->gop) && !h265_frame->gop.is_ref,
      .cross_layer_bla_flag = 0,
      .pic_output_flag = slice_hdr->pic_output_flag,
      .no_output_of_prior_pics_flag = slice_hdr->no_output_of_prior_pics_flag,
      .short_term_ref_pic_set_sps_flag = 0,
      .slice_temporal_mvp_enabled_flag =
      self->params.sps.sps.flags.sps_temporal_mvp_enabled_flag,
    },
    .pic_type = gst_vulkan_h265_picture_type (h265_frame->gop.type,
        GST_H26X_GOP_IS_IDR (&h265_frame->gop)),
    .sps_video_parameter_set_id =
        self->params.vps.vps.vps_video_parameter_set_id,
    .pps_seq_parameter_set_id =
        self->params.sps.sps.sps_seq_parameter_set_id,
    .pps_pic_parameter_set_id =
        self->params.pps.pps.pps_pic_parameter_set_id,
    .PicOrderCntVal = h265_frame->poc,
    .TemporalId = 0,
    /* Reserved */
    .pRefLists = NULL, /* filled in during setup_refs */
    .pShortTermRefPicSet = NULL,
    .pLongTermRefPics = NULL,
  };

  vk_frame->ref_info = (StdVideoEncodeH265ReferenceInfo) {
    .flags = {
      .used_for_long_term_reference = 0,
      .unused_for_reference = 0,
    },
    .pic_type = vk_frame->pic_info.pic_type,
    .PicOrderCntVal = vk_frame->pic_info.PicOrderCntVal,
    .TemporalId = vk_frame->pic_info.TemporalId,
  };
  /* *INDENT-ON* */

  _setup_ref_lists (h265_frame, slice_hdr, list0, list1);
  _setup_slice (self, h265_frame, slice_hdr);

  vk_frame->picture.codec_rc_info = &vk_frame->vkrc_info;

  g_assert (list0->len + list1->len <= STD_VIDEO_H265_MAX_NUM_LIST_REF);
  for (i = 0; i < list0->len; i++) {
    GstH265EncoderFrame *pic = g_array_index (list0, GstH265EncoderFrame *, i);
    ref_pics[i] = &_GET_FRAME (pic)->picture;
  }
  for (j = 0; j < list1->len; j++) {
    GstH265EncoderFrame *pic = g_array_index (list1, GstH265EncoderFrame *, j);
    ref_pics[i++] = &_GET_FRAME (pic)->picture;
  }

  if (!_write_headers (self, vk_frame)) {
    GST_ERROR_OBJECT (self, "Failed to write headers");
    return GST_FLOW_ERROR;
  }

  if (!gst_vulkan_encoder_encode (self->encoder, &self->in_state->info,
          &vk_frame->picture, i, ref_pics)) {
    GST_ERROR_OBJECT (self, "Encode frame error");
    return GST_FLOW_ERROR;
  }
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vulkan_h265_encoder_prepare_output (GstH265Encoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstH265EncoderFrame *h265_frame;
  GstVulkanH265EncoderFrame *vk_frame;

  h265_frame = gst_video_codec_frame_get_user_data (frame);
  vk_frame = _GET_FRAME (h265_frame);

  gst_buffer_replace (&frame->output_buffer, vk_frame->picture.out_buffer);

  return GST_FLOW_OK;
}

static void
gst_vulkan_h265_encoder_reset (GstH265Encoder * encoder)
{
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (encoder);

  GST_OBJECT_LOCK (self);
  self->rc.bitrate = self->prop.bitrate;
  self->rc.quality = self->prop.quality;
  GST_OBJECT_UNLOCK (self);

  self->coded_buffer_size = 0;
}

static gboolean
gst_vulkan_h265_encoder_open (GstVideoEncoder * encoder)
{
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (encoder);
  GstVulkanH265EncoderClass *klass = GST_VULKAN_H265_ENCODER_GET_CLASS (self);
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

  self->encode_queue =
      gst_vulkan_device_select_queue (self->device,
      VK_QUEUE_VIDEO_ENCODE_BIT_KHR);
  if (!self->encode_queue) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Failed to create/retrieve vulkan H.265 encoder queue"), (NULL));
    gst_clear_object (&self->instance);
    return FALSE;
  }

  self->encoder =
      gst_vulkan_encoder_create_from_queue (self->encode_queue,
      VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR);

  if (!self->encoder) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Failed to retrieve vulkan encoder"), (NULL));
    return FALSE;
  }

  gst_vulkan_encoder_set_callbacks (self->encoder, &callbacks, self, NULL);

  return TRUE;
}

static gboolean
gst_vulkan_h265_encoder_close (GstVideoEncoder * encoder)
{
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (encoder);

  gst_clear_object (&self->encoder);
  gst_clear_object (&self->encode_queue);
  gst_clear_object (&self->device);
  gst_clear_object (&self->instance);

  return TRUE;
}

static gboolean
gst_vulkan_h265_encoder_stop (GstVideoEncoder * encoder)
{
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (encoder);

  if (self->in_state)
    gst_video_codec_state_unref (self->in_state);
  self->in_state = NULL;

  gst_vulkan_encoder_stop (self->encoder);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->stop (encoder);
}

static gboolean
_query_context (GstVulkanH265Encoder * self, GstQuery * query,
    GstPadDirection continue_direction)
{
  if (!self->encoder)
    return FALSE;

  if (gst_vulkan_requested_extensions_handle_context_query (GST_ELEMENT (self),
          query, continue_direction, self->instance))
    return TRUE;

  if (gst_vulkan_handle_context_query (GST_ELEMENT (self), query, NULL,
          self->instance, self->device))
    return TRUE;

  if (gst_vulkan_queue_handle_context_query (GST_ELEMENT (self), query,
          self->encode_queue))
    return TRUE;

  return FALSE;
}

static gboolean
gst_vulkan_h265_encoder_src_query (GstVideoEncoder * encoder, GstQuery * query)
{
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      ret = _query_context (GST_VULKAN_H265_ENCODER (encoder), query,
          GST_PAD_SINK);
      break;
    default:
      ret = GST_VIDEO_ENCODER_CLASS (parent_class)->src_query (encoder, query);
      break;
  }

  return ret;
}

static gboolean
gst_vulkan_h265_encoder_sink_query (GstVideoEncoder * encoder, GstQuery * query)
{
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      ret = _query_context (GST_VULKAN_H265_ENCODER (encoder), query,
          GST_PAD_SRC);
      break;
    default:
      ret = GST_VIDEO_ENCODER_CLASS (parent_class)->sink_query (encoder, query);
      break;
  }

  return ret;
}

static gboolean
gst_vulkan_h265_encoder_propose_allocation (GstVideoEncoder * venc,
    GstQuery * query)
{
  gboolean need_pool;
  GstCaps *caps, *profile_caps;
  GstVideoInfo info;
  guint size;
  GstBufferPool *pool = NULL;
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (venc);

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
      GST_WARNING_OBJECT (self, "Couldn't get Vulkan Capabilities");
      gst_structure_free (config);
      g_object_unref (pool);
      return FALSE;
    }
    if ((vk_caps.caps.flags &
            VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR) == 0) {
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
      self->sps.max_dec_pic_buffering_minus1[0] + 1, 0);
  if (pool)
    gst_object_unref (pool);

  if (!gst_vulkan_encoder_create_dpb_pool (self->encoder, caps)) {
    GST_ERROR_OBJECT (self, "Unable to create the dpb pool");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_vulkan_h265_encoder_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  gboolean ret;

  ret = GST_VIDEO_ENCODER_CLASS (parent_class)->set_format (encoder, state);
  if (ret)
    ret = gst_h265_encoder_reconfigure (GST_H265_ENCODER (encoder), TRUE);
  return ret;
}

static void
gst_vulkan_h265_encoder_init (GTypeInstance * instance, gpointer g_class)
{
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (instance);

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
gst_vulkan_h265_encoder_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (object);

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
gst_vulkan_h265_encoder_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVulkanH265Encoder *self = GST_VULKAN_H265_ENCODER (object);
  GstH265Encoder *h265enc = GST_H265_ENCODER (object);
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
    gst_h265_encoder_reconfigure (h265enc, FALSE);
}

static void
gst_vulkan_h265_encoder_class_init (gpointer g_klass, gpointer class_data)
{
  GstVulkanH265EncoderClass *klass = g_klass;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *encoder_class = GST_VIDEO_ENCODER_CLASS (klass);
  GstH265EncoderClass *h265encoder_class = GST_H265_ENCODER_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  struct CData *cdata = class_data;
  GParamFlags param_flags = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
      G_PARAM_CONSTRUCT | GST_PARAM_MUTABLE_PLAYING;
  gchar *long_name;
  const gchar *name;
  GstPadTemplate *sink_pad_template, *src_pad_template;
  GstCaps *sink_doc_caps, *src_doc_caps;

  name = "Vulkan H.265 encoder";
  if (cdata->description)
    long_name = g_strdup_printf ("%s on %s", name, cdata->description);
  else
    long_name = g_strdup (name);

  klass->device_index = cdata->device_index;

  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Encoder/Video/Hardware",
      "A H.265 video encoder based on Vulkan",
      "Víctor Jáquez <vjaquez@igalia.com>");

  parent_class = g_type_class_peek_parent (klass);

  src_doc_caps = gst_caps_from_string ("video/x-h265, "
      "profile = { (string) main, (string) main-10 }, "
      "stream-format = (string) byte-stream, alignment = (string) au");
  sink_doc_caps =
      gst_caps_from_string (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
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
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_get_property);

  encoder_class->open = GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_open);
  encoder_class->close = GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_close);
  encoder_class->stop = GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_stop);
  encoder_class->src_query =
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_src_query);
  encoder_class->sink_query =
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_sink_query);
  encoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_propose_allocation);
  encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_set_format);

  h265encoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_new_sequence);
  h265encoder_class->new_parameters =
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_new_parameters);
  h265encoder_class->new_output =
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_new_output);
  h265encoder_class->encode_frame =
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_encode_frame);
  h265encoder_class->prepare_output =
      GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_prepare_output);
  h265encoder_class->reset = GST_DEBUG_FUNCPTR (gst_vulkan_h265_encoder_reset);

  /**
   * GstVulkanH265Encoder:bitrate:
   *
   * Bitrate is the amount of data (in kilobits) to process per second. It's
   * both a function of the encoded bitstream data size of the encoded pictures
   * as well as the frame rate used by the video sequence
   *
   * A higher bitrate will result in a better visual quality but it will result
   * in a bigger file. A lower bitrate will result in a smaller file, but it
   * will also result in a worse visual quality.
   *
   * The desired bitrate expressed in kbps (0: auto-calculate).
   *
   * Since: 1.30
   */
  properties[PROP_BITRATE] = g_param_spec_uint ("bitrate", "Bitrate (kbps)",
      "The desired bitrate expressed in kbps (0: auto-calculate)", 0, G_MAXUINT,
      0, param_flags);

  /**
   * GstVulkanH265Encoder:aud:
   *
   * Insert the AU (Access Unit) delimeter for each frame.
   *
   * Since: 1.30
   */
  properties[PROP_AUD] =
      g_param_spec_boolean ("aud", "Insert AUD",
      "Insert AU (Access Unit) delimeter for each frame", TRUE, param_flags);

  /**
   * GstVulkanH265Encoder:qp-i:
   *
   * Indicates the quantization parameter for all the slices in each I frame.
   * It's only applied when the rate control mode
   * (#GstVulkanH265Encoder:rc-mode) is CQP (constant quantization parameter).
   *
   * Lower QP values mean higher video quality, but larger file sizes or higher
   * bitrates.
   *
   * Since: 1.30
   */
  properties[PROP_QP_I] =
      g_param_spec_uint ("qp-i", "Constant I frame QP",
      "Constant quantization value for each I-frame slice", 0,
      51, 26, param_flags);

  /**
   * GstVulkanH265Encoder:qp-p:
   *
   * Indicates the quantization parameter for all the slices in each P frame.
   * It's only applied when the rate control mode
   * (#GstVulkanH265Encoder:rc-mode) is CQP (constant quantization parameter).
   *
   * Lower QP values mean higher video quality, but larger file sizes or higher
   * bitrates.
   *
   * Since: 1.30
   */
  properties[PROP_QP_P] =
      g_param_spec_uint ("qp-p", "Constant P frame QP",
      "Constant quantization value for each P-frame slice", 0,
      51, 26, param_flags);

  /**
   * GstVulkanH265Encoder:qp-b:
   *
   * Indicates the quantization parameter for all the slices in each B frame.
   * It's only applied when the rate control mode
   * (#GstVulkanH265Encoder:rc-mode) is CQP (constant quantization parameter).
   *
   * Lower QP values mean higher video quality, but larger file sizes or higher
   * bitrates.
   *
   * Since: 1.30
   */
  properties[PROP_QP_B] =
      g_param_spec_uint ("qp-b", "Constant B frame QP",
      "Constant quantization value for each B-frame slice", 0,
      51, 26, param_flags);

  /**
   * GstVulkanH265Encoder:max-qp:
   *
   * Indicates the quantization parameter upper bound for each frame. It's only
   * applied when the rate control mode (#GstVulkanH265Encoder:rc-mode) is
   * either CBR (constant bitrate) or VBR (variable bitrate).
   *
   * Lower QP values mean higher video quality, but larger file sizes or higher
   * bitrates.
   *
   * If zero, the upper bound will not be clamped.
   *
   * Since: 1.30
   */
  properties[PROP_MAX_QP] = g_param_spec_uint ("max-qp", "Maximum QP",
      "Maximum quantization value for each frame (0: disabled)", 0, 51, 0,
      param_flags);

  /**
   * GstVulkanH265Encoder:min-qp:
   *
   * Indicates the quantization parameter lower bound for each frame. It's only
   * applied when the rate control mode (#GstVulkanH265Encoder::rc-mode) is
   * either CBR (constant bitrate) or VBR (variable bitrate).
   *
   * Lower QP values mean higher video quality, but larger file sizes or higher
   * bitrates.
   *
   * If zero, the lower bound will not be clamped.
   *
   * Since: 1.30
   */
  properties[PROP_MIN_QP] = g_param_spec_uint ("min-qp", "Minimum QP",
      "Minimum quantization value for each frame (0: disabled)", 0, 51, 0,
      param_flags);

  /**
   * GstVulkanH265Encoder:rate-control:
   *
   * Rate control algorithms adjust encoding parameters dynamically to regulate
   * the output bitrate. This can involve managing Quantization Parameters (QP),
   * quality, or other encoding parameters.
   *
   * Since: 1.30
   */
  properties[PROP_RATECONTROL] = g_param_spec_enum ("rate-control",
      "rate control mode", "The encoding rate control mode to use",
      GST_TYPE_VULKAN_ENCODER_RATE_CONTROL_MODE,
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR, param_flags);

  /**
   * GstVulkanH265Encoder:quality:
   *
   * Video encode quality level.
   *
   * Higher quality levels may produce higher quality videos at the cost of
   * additional processing time.
   *
   * Since: 1.30
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
gst_vulkan_h265_encoder_register (GstPlugin * plugin,
    GstVulkanDevice * device, guint rank)
{
  static GOnce debug_once = G_ONCE_INIT;
  GType type;
  GTypeInfo type_info = {
    .class_size = sizeof (GstVulkanH265EncoderClass),
    .class_init = gst_vulkan_h265_encoder_class_init,
    .instance_size = sizeof (GstVulkanH265Encoder),
    .instance_init = gst_vulkan_h265_encoder_init,
  };
  struct CData *cdata;
  gboolean ret;
  gchar *type_name, *feature_name;
  GstCaps *codec = NULL, *raw = NULL;

  g_return_val_if_fail (GST_IS_PLUGIN (plugin), FALSE);
  g_return_val_if_fail (GST_IS_VULKAN_DEVICE (device), FALSE);

  if (!gst_vulkan_physical_device_codec_caps (device->physical_device,
          VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR, &codec, &raw)) {
    gst_plugin_add_status_warning (plugin,
        "Unable to query H.265 encoder properties");
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

  gst_vulkan_create_feature_name (device, "GstVulkanH265Encoder",
      "GstVulkanH265Device%dEncoder", &type_name,
      "vulkanh265enc", "vulkanh265device%denc",
      &feature_name, &cdata->description, &rank);

  type_info.class_data = cdata;

  g_once (&debug_once, _register_debug_category, NULL);
  type =
      g_type_register_static (GST_TYPE_H265_ENCODER, type_name, &type_info, 0);

  ret = gst_vulkan_register_feature (plugin, device, type, feature_name, rank);

  g_free (type_name);
  g_free (feature_name);

  return ret;
}
