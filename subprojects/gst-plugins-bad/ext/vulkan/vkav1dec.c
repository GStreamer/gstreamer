/* GStreamer
 * Copyright (C) 2025 Collabora, Ltd.
 * Copyright (C) 2025 Igalia, S.L.
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

#include "vkav1dec.h"

#include <gst/video/video.h>
#include <gst/vulkan/vulkan.h>
#include <gst/codecparsers/gstav1parser.h>
#include "gst/vulkan/gstvkphysicaldevice-private.h"
#include "gst/vulkan/gstvkdecoder-private.h"
#include "gstvulkanelements.h"

#define GST_VULKAN_AV1_DECODER(obj)            ((GstVulkanAV1Decoder *) obj)
#define GST_VULKAN_AV1_DECODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_FROM_INSTANCE (obj), GstVulkanAV1DecoderClass))
#define GST_VULKAN_AV1_DECODER_CLASS(klass)    ((GstVulkanAV1DecoderClass *) klass)


#define GST_VULKAN_AV1_MAX_DPB_SLOTS 32

typedef struct _GstVulkanAV1Decoder GstVulkanAV1Decoder;
typedef struct _GstVulkanAV1DecoderClass GstVulkanAV1DecoderClass;
typedef struct _GstVulkanAV1Picture GstVulkanAV1Picture;

struct _GstVulkanAV1Decoder
{
  GstAV1Decoder parent;

  GstVulkanInstance *instance;
  GstVulkanDevice *device;
  GstVulkanQueue *graphic_queue, *decode_queue;

  GstVulkanDecoder *decoder;

  gboolean need_negotiation;
  gboolean resolution_changed;

  gint width, height;
  gint coded_width, coded_height;
  gint dpb_size;

  VkSamplerYcbcrRange range;
  VkChromaLocation chroma_location;

  GstVideoCodecState *output_state;
  struct
  {
    StdVideoAV1SequenceHeader sequence;
    StdVideoAV1TimingInfo timing_info;
    StdVideoAV1ColorConfig color_config;
  } vk;

  guint32 free_slot_mask;
};

struct _GstVulkanAV1DecoderClass
{
  GstAV1DecoderClass parent;

  gint device_index;
};

static GstElementClass *parent_class = NULL;

static GstStaticPadTemplate gst_vulkan_av1dec_sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-av1, alignment=frame"));

static GstStaticPadTemplate gst_vulkan_av1dec_src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE, "NV12")));

GST_DEBUG_CATEGORY (gst_vulkan_av1_decoder_debug);
#define GST_CAT_DEFAULT gst_vulkan_av1_decoder_debug

struct _GstVulkanAV1Picture
{
  GstVulkanDecoderPicture base;

  /* Picture refs */
  StdVideoDecodeAV1ReferenceInfo std_refs[GST_AV1_NUM_REF_FRAMES];
  VkVideoDecodeAV1DpbSlotInfoKHR vk_slots[GST_AV1_NUM_REF_FRAMES];

  /* Current picture */
  StdVideoDecodeAV1ReferenceInfo std_ref;
  VkVideoDecodeAV1DpbSlotInfoKHR vk_slot;
  guint16 width_in_sbs_minus1[64];
  guint16 height_in_sbs_minus1[64];
  guint16 mi_col_starts[64];
  guint16 mi_row_starts[64];
  StdVideoAV1TileInfo tile_info;
  StdVideoAV1Quantization quantization;
  StdVideoAV1Segmentation segmentation;
  StdVideoAV1LoopFilter loop_filter;
  StdVideoAV1CDEF cdef;
  StdVideoAV1LoopRestoration loop_restoration;
  StdVideoAV1GlobalMotion global_motion;
  StdVideoAV1FilmGrain film_grain;

  GArray *tile_sizes;
  GArray *tile_offsets;
  guint num_tiles;
  guint32 tile_data_sz;

  VkVideoDecodeAV1PictureInfoKHR vk_av1pic;
  StdVideoDecodeAV1PictureInfo std_av1pic;

  gint32 slot_idx;

  // Used to update the mask when this picture is freed.
  guint32 *free_slot_mask;
};

static gpointer
_register_debug_category (gpointer data)
{
  GST_DEBUG_CATEGORY_INIT (gst_vulkan_av1_decoder_debug, "vulkanav1dec", 0,
      "Vulkan AV1 decoder");

  return NULL;
}

static void
gst_vulkan_av1_decoder_set_context (GstElement * element, GstContext * context)
{
  GstVulkanAV1Decoder *self = GST_VULKAN_AV1_DECODER (element);

  gst_vulkan_handle_set_context (element, context, NULL, &self->instance);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
_query_context (GstVulkanAV1Decoder * self, GstQuery * query)
{
  if (gst_vulkan_handle_context_query (GST_ELEMENT (self), query, NULL,
          self->instance, self->device))
    return TRUE;

  if (gst_vulkan_queue_handle_context_query (GST_ELEMENT (self), query,
          self->graphic_queue))
    return TRUE;

  return FALSE;
}

static gboolean
gst_vulkan_av1_decoder_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      ret = _query_context (GST_VULKAN_AV1_DECODER (decoder), query);
      break;
    default:
      ret = GST_VIDEO_DECODER_CLASS (parent_class)->src_query (decoder, query);
      break;
  }

  return ret;
}

static gboolean
gst_vulkan_av1_decoder_sink_query (GstVideoDecoder * decoder, GstQuery * query)
{
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      ret = _query_context (GST_VULKAN_AV1_DECODER (decoder), query);
      break;
    default:
      ret = GST_VIDEO_DECODER_CLASS (parent_class)->sink_query (decoder, query);
      break;
  }

  return ret;
}

static gboolean
_find_queues (GstVulkanDevice * device, GstVulkanQueue * queue, gpointer data)
{
  GstVulkanAV1Decoder *self = data;
  guint32 flags =
      device->physical_device->queue_family_props[queue->family].queueFlags;
  guint32 codec =
      device->physical_device->queue_family_ops[queue->family].video;

  if (!self->graphic_queue
      && ((flags & VK_QUEUE_GRAPHICS_BIT) == VK_QUEUE_GRAPHICS_BIT)) {
    self->graphic_queue = gst_object_ref (queue);
  }

  if (!self->decode_queue
      && ((codec & VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR)
          == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR)
      && ((flags & VK_QUEUE_VIDEO_DECODE_BIT_KHR)
          == VK_QUEUE_VIDEO_DECODE_BIT_KHR)) {
    self->decode_queue = gst_object_ref (queue);
  }

  return !(self->decode_queue && self->graphic_queue);
}

static gboolean
gst_vulkan_av1_decoder_open (GstVideoDecoder * decoder)
{
  GstVulkanAV1Decoder *self = GST_VULKAN_AV1_DECODER (decoder);

  if (!gst_vulkan_ensure_element_data (GST_ELEMENT (decoder), NULL,
          &self->instance)) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Failed to retrieve vulkan instance"), (NULL));
    return FALSE;
  }

  if (!gst_vulkan_ensure_element_device (GST_ELEMENT (decoder), self->instance,
          &self->device, 0)) {
    return FALSE;
  }

  if (!gst_vulkan_queue_run_context_query (GST_ELEMENT (self),
          &self->graphic_queue)) {
    GST_DEBUG_OBJECT (self, "No graphic queue retrieved from peer elements");
  }

  gst_vulkan_device_foreach_queue (self->device, _find_queues, self);

  if (!self->decode_queue) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Failed to create/retrieve vulkan AV1 decoder queue"), (NULL));
    return FALSE;
  }

  self->decoder = gst_vulkan_decoder_new_from_queue (self->decode_queue,
      VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR);
  if (!self->decoder) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Failed to create vulkan AV1 decoder"), (NULL));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_vulkan_av1_decoder_close (GstVideoDecoder * decoder)
{
  GstVulkanAV1Decoder *self = GST_VULKAN_AV1_DECODER (decoder);

  gst_clear_object (&self->decoder);
  gst_clear_object (&self->decode_queue);
  gst_clear_object (&self->graphic_queue);
  gst_clear_object (&self->device);
  gst_clear_object (&self->instance);

  return TRUE;
}

static gboolean
gst_vulkan_av1_decoder_stop (GstVideoDecoder * decoder)
{
  GstVulkanAV1Decoder *self = GST_VULKAN_AV1_DECODER (decoder);

  if (self->decoder)
    gst_vulkan_decoder_stop (self->decoder);

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);

  return GST_VIDEO_DECODER_CLASS (parent_class)->stop (decoder);
}

static gboolean
gst_vulkan_av1_decoder_negotiate (GstVideoDecoder * decoder)
{
  GstVulkanAV1Decoder *self = GST_VULKAN_AV1_DECODER (decoder);
  GstAV1Decoder *av1dec = GST_AV1_DECODER (decoder);
  VkVideoFormatPropertiesKHR format_prop;
  GstVideoFormat format;

  /* Ignore downstream renegotiation request. */
  if (!self->need_negotiation)
    return TRUE;

  if (!gst_vulkan_decoder_out_format (self->decoder, &format_prop))
    return FALSE;

  self->need_negotiation = FALSE;

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);

  format = gst_vulkan_format_to_video_format (format_prop.format);
  self->output_state = gst_video_decoder_set_interlaced_output_state (decoder,
      format, GST_VIDEO_INTERLACE_MODE_PROGRESSIVE, self->width, self->height,
      av1dec->input_state);

  self->output_state->caps = gst_video_info_to_caps (&self->output_state->info);
  gst_caps_set_features_simple (self->output_state->caps,
      gst_caps_features_new_static_str (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE,
          NULL));

  GST_INFO_OBJECT (self, "Negotiated caps %" GST_PTR_FORMAT,
      self->output_state->caps);

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_vulkan_av1_decoder_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstVulkanAV1Decoder *self = GST_VULKAN_AV1_DECODER (decoder);
  GstCaps *new_caps, *profile_caps, *caps = NULL, *dpb_caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  guint size, min, max;
  gboolean update_pool;
  VkImageUsageFlags usage;
  GstVulkanVideoCapabilities vk_caps;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps)
    return FALSE;
  if (!gst_vulkan_decoder_caps (self->decoder, &vk_caps))
    return FALSE;

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    update_pool = TRUE;
  } else {
    GstVideoInfo vinfo;

    gst_video_info_from_caps (&vinfo, caps);
    size = GST_VIDEO_INFO_SIZE (&vinfo);
    min = 2;
    max = 0;
    update_pool = FALSE;
  }

  if (!(pool && GST_IS_VULKAN_IMAGE_BUFFER_POOL (pool))) {
    gst_clear_object (&pool);
    pool = gst_vulkan_image_buffer_pool_new (self->device);
  }

  usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
      | VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;

  if (!self->decoder->dedicated_dpb) {
    min = MAX (min, MIN (self->dpb_size, vk_caps.caps.maxDpbSlots));
    max = 0;
    usage |= VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
  }

  new_caps = gst_caps_copy (caps);
  gst_caps_set_simple (new_caps, "width", G_TYPE_INT, self->coded_width,
      "height", G_TYPE_INT, self->coded_height, NULL);
  profile_caps = gst_vulkan_decoder_profile_caps (self->decoder);

  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, new_caps, size, min, max);

  gst_vulkan_image_buffer_pool_config_set_allocation_params (config, usage,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR,
      VK_ACCESS_TRANSFER_WRITE_BIT);
  gst_vulkan_image_buffer_pool_config_set_decode_caps (config, profile_caps);

  gst_caps_unref (profile_caps);

  if (!gst_buffer_pool_set_config (pool, config))
    goto bail;

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  dpb_caps = gst_caps_copy (caps);
  gst_caps_set_simple (dpb_caps, "width", G_TYPE_INT,
      vk_caps.caps.maxCodedExtent.width, "height", G_TYPE_INT,
      vk_caps.caps.maxCodedExtent.height, NULL);

  if (!gst_vulkan_decoder_create_dpb_pool (self->decoder, dpb_caps))
    goto bail;

  gst_caps_unref (dpb_caps);
  gst_caps_unref (new_caps);

  return TRUE;

bail:
  {
    gst_clear_caps (&new_caps);
    gst_clear_caps (&dpb_caps);
    gst_clear_object (&pool);
    return FALSE;
  }
}

static VkVideoChromaSubsamplingFlagBitsKHR
_get_chroma_subsampling_flag (const GstAV1SequenceHeaderOBU * seq_hdr)
{
  if (seq_hdr->color_config.mono_chrome) {
    return VK_VIDEO_CHROMA_SUBSAMPLING_MONOCHROME_BIT_KHR;
  } else if (seq_hdr->color_config.subsampling_x == 0
      && seq_hdr->color_config.subsampling_y == 0) {
    return VK_VIDEO_CHROMA_SUBSAMPLING_444_BIT_KHR;
  } else if (seq_hdr->color_config.subsampling_x == 1
      && seq_hdr->color_config.subsampling_y == 0) {
    return VK_VIDEO_CHROMA_SUBSAMPLING_422_BIT_KHR;
  } else if (seq_hdr->color_config.subsampling_x == 1
      && seq_hdr->color_config.subsampling_y == 1) {
    return VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
  } else {
    return VK_VIDEO_CHROMA_SUBSAMPLING_INVALID_KHR;
  }
}

static VkVideoComponentBitDepthFlagBitsKHR
_get_component_bit_depth (const GstAV1SequenceHeaderOBU * seq_hdr)
{
  switch (seq_hdr->bit_depth) {
    case 8:
      return VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    case 10:
      return VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR;
    case 12:
      return VK_VIDEO_COMPONENT_BIT_DEPTH_12_BIT_KHR;
    default:
      return VK_VIDEO_COMPONENT_BIT_DEPTH_INVALID_KHR;
  }
}

static StdVideoAV1Profile
_get_av1_profile (const GstAV1SequenceHeaderOBU * seq_hdr)
{
  switch (seq_hdr->seq_profile) {
    case GST_AV1_PROFILE_0:
      return STD_VIDEO_AV1_PROFILE_MAIN;
    case GST_AV1_PROFILE_1:
      return STD_VIDEO_AV1_PROFILE_HIGH;
    case GST_AV1_PROFILE_2:
      return STD_VIDEO_AV1_PROFILE_PROFESSIONAL;
    default:
      return STD_VIDEO_AV1_PROFILE_INVALID;
  }
}

static void
gst_vulkan_video_profile_from_av1_sequence_hdr (GstVulkanVideoProfile * profile,
    const GstAV1SequenceHeaderOBU * seq_hdr)
{
  /* *INDENT-OFF* */
  *profile = (GstVulkanVideoProfile) {
    .profile = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
      .pNext = &profile->usage,
      .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR,
      .chromaSubsampling = _get_chroma_subsampling_flag (seq_hdr),
      .lumaBitDepth = _get_component_bit_depth (seq_hdr),
      .chromaBitDepth = _get_component_bit_depth (seq_hdr),
    },
    .usage.decode = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_USAGE_INFO_KHR,
      .videoUsageHints = VK_VIDEO_DECODE_USAGE_DEFAULT_KHR,
      .pNext = &profile->codec,
    },
    .codec.av1dec = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_PROFILE_INFO_KHR,
      .stdProfile = _get_av1_profile (seq_hdr),
      .filmGrainSupport = VK_FALSE,
    },
  };
  /* *INDENT-ON* */
}

static GstFlowReturn
_update_parameters (GstVulkanAV1Decoder * self,
    const GstAV1SequenceHeaderOBU * seq)
{
  GError *error = NULL;
  /* *INDENT-OFF* */
  self->vk.timing_info = (StdVideoAV1TimingInfo) {
    .flags = {
      .equal_picture_interval = seq->timing_info.equal_picture_interval,
    },
    .num_units_in_display_tick = seq->timing_info.num_units_in_display_tick,
    .time_scale = seq->timing_info.time_scale,
    .num_ticks_per_picture_minus_1 =
        seq->timing_info.num_ticks_per_picture_minus_1,
  };

  self->vk.color_config = (StdVideoAV1ColorConfig) {
    .flags = {
      .mono_chrome = seq->color_config.mono_chrome,
      .color_range = seq->color_config.color_range,
      .separate_uv_delta_q = seq->color_config.separate_uv_delta_q,
    },
    .BitDepth = seq->color_config.twelve_bit    ? 12 :
                seq->color_config.high_bitdepth ? 10 : 8,
    .subsampling_x = seq->color_config.subsampling_x,
    .subsampling_y = seq->color_config.subsampling_y,
    .color_primaries =
        (StdVideoAV1ColorPrimaries) seq->color_config.color_primaries,
    .transfer_characteristics =
        (StdVideoAV1TransferCharacteristics) seq->color_config.transfer_characteristics,
    .matrix_coefficients =
        (StdVideoAV1MatrixCoefficients) seq->color_config.matrix_coefficients,
  };

  self->vk.sequence = (StdVideoAV1SequenceHeader) {
    .flags = {
      .still_picture = seq->still_picture,
      .reduced_still_picture_header = seq->reduced_still_picture_header,
      .use_128x128_superblock = seq->use_128x128_superblock,
      .enable_filter_intra = seq->enable_filter_intra,
      .enable_intra_edge_filter = seq->enable_intra_edge_filter,
      .enable_interintra_compound = seq->enable_interintra_compound,
      .enable_masked_compound = seq->enable_masked_compound,
      .enable_warped_motion = seq->enable_warped_motion,
      .enable_dual_filter = seq->enable_dual_filter,
      .enable_order_hint = seq->enable_order_hint,
      .enable_jnt_comp = seq->enable_jnt_comp,
      .enable_ref_frame_mvs = seq->enable_ref_frame_mvs,
      .frame_id_numbers_present_flag = seq->frame_id_numbers_present_flag,
      .enable_superres = seq->enable_superres,
      .enable_cdef = seq->enable_cdef,
      .enable_restoration = seq->enable_restoration,
      .film_grain_params_present = seq->film_grain_params_present,
      .timing_info_present_flag = seq->timing_info_present_flag,
      .initial_display_delay_present_flag = seq->initial_display_delay_present_flag,
    },
    .seq_profile = _get_av1_profile (seq),
    .frame_width_bits_minus_1 = seq->frame_width_bits_minus_1,
    .frame_height_bits_minus_1 = seq->frame_height_bits_minus_1,
    .max_frame_width_minus_1 = seq->max_frame_width_minus_1,
    .max_frame_height_minus_1 = seq->max_frame_height_minus_1,
    .delta_frame_id_length_minus_2 = seq->delta_frame_id_length_minus_2,
    .additional_frame_id_length_minus_1 = seq->additional_frame_id_length_minus_1,
    .order_hint_bits_minus_1 = seq->order_hint_bits_minus_1,
    .seq_force_integer_mv = seq->seq_force_integer_mv,
    .seq_force_screen_content_tools = seq->seq_force_screen_content_tools,
    .pTimingInfo = &self->vk.timing_info,
    .pColorConfig = &self->vk.color_config,
  };

  GstVulkanDecoderParameters dec_params = {
    .av1 = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_SESSION_PARAMETERS_CREATE_INFO_KHR,
      .pNext = NULL,
      .pStdSequenceHeader = &self->vk.sequence,
    },
  };
  /* *INDENT-ON* */

  if (!gst_vulkan_decoder_update_video_session_parameters (self->decoder,
          &dec_params, &error)) {
    if (error) {
      GST_ERROR_OBJECT (self, "Couldn't set codec parameters: %s",
          error->message);
      g_clear_error (&error);
    }
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vulkan_av1_decoder_new_sequence (GstAV1Decoder * decoder,
    const GstAV1SequenceHeaderOBU * seq_hdr, gint max_dpb_size)
{
  GstVulkanAV1Decoder *self = GST_VULKAN_AV1_DECODER (decoder);
  GstVulkanVideoProfile profile;
  GstVulkanVideoCapabilities vk_caps;
  GError *error = NULL;
  gint width = seq_hdr->max_frame_width_minus_1 + 1;
  gint height = seq_hdr->max_frame_height_minus_1 + 1;
  VkFormat old_format = VK_FORMAT_UNDEFINED;
  VkVideoFormatPropertiesKHR format_prop;
  GstFlowReturn ret;

  gst_vulkan_video_profile_from_av1_sequence_hdr (&profile, seq_hdr);

  if (gst_vulkan_decoder_is_started (self->decoder)) {
    if (!gst_vulkan_video_profile_is_equal (&self->decoder->profile, &profile)) {
      if (gst_vulkan_decoder_out_format (self->decoder, &format_prop))
        old_format = format_prop.format;
      gst_vulkan_decoder_stop (self->decoder);
    } else {
      self->need_negotiation = FALSE;
    }
  }

  if (!gst_vulkan_decoder_is_started (self->decoder)) {
    self->need_negotiation = TRUE;
    if (!gst_vulkan_decoder_start (self->decoder, &profile, &error)) {
      GST_ERROR_OBJECT (self, "Couldn't start decoder: %s",
          error ? error->message : "");
      g_clear_error (&error);
      return GST_FLOW_ERROR;
    }
  }

  ret = _update_parameters (self, seq_hdr);
  if (ret != GST_FLOW_OK)
    return ret;

  self->dpb_size = CLAMP (max_dpb_size, 0, GST_VULKAN_AV1_MAX_DPB_SLOTS);

  gst_vulkan_decoder_caps (self->decoder, &vk_caps);
  if (width < vk_caps.caps.minCodedExtent.width
      || height < vk_caps.caps.minCodedExtent.height
      || width > vk_caps.caps.maxCodedExtent.width
      || height > vk_caps.caps.maxCodedExtent.height) {

    GST_ERROR_OBJECT (self,
        "The following sequence can not be decoded because the frame dimension does not fit the decoder bounds: %dx%d"
        ", minCodedExtent=%dx%d, maxCodedExtent=%dx%d",
        width, height, vk_caps.caps.minCodedExtent.width,
        vk_caps.caps.minCodedExtent.height, vk_caps.caps.maxCodedExtent.width,
        vk_caps.caps.maxCodedExtent.height);
    return GST_FLOW_ERROR;
  }

  self->coded_width = width;
  self->coded_height = height;

  self->resolution_changed = self->coded_width > 0 && self->coded_height > 0
      && (width != self->coded_width || height != self->coded_height);
  self->need_negotiation &= (width != self->width || height != self->height);
  self->width = width;
  self->height = height;

  /* Ycbcr sampler */
  {
    VkSamplerYcbcrRange range;
    VkChromaLocation chroma_location;
    gboolean ret;

    ret = gst_vulkan_decoder_out_format (self->decoder, &format_prop);
    g_assert (ret);

    range = (seq_hdr->color_config.color_range) ?
        VK_SAMPLER_YCBCR_RANGE_ITU_FULL : VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;

    switch (seq_hdr->color_config.chroma_sample_position) {
      case GST_AV1_CSP_COLOCATED:
        chroma_location = VK_CHROMA_LOCATION_COSITED_EVEN;
        break;
      default:
        chroma_location = VK_CHROMA_LOCATION_MIDPOINT;
    }

    if (old_format != format_prop.format || range != self->range ||
        chroma_location != self->chroma_location) {
      self->range = range;
      self->chroma_location = chroma_location;
      ret =
          gst_vulkan_decoder_update_ycbcr_sampler (self->decoder, range,
          VK_CHROMA_LOCATION_COSITED_EVEN, chroma_location, &error);
      if (!ret && error) {
        GST_WARNING_OBJECT (self, "Unable to create Ycbcr sampler: %s",
            error->message);
        g_clear_error (&error);
      }
    }
  }

  return GST_FLOW_OK;
}

static GstVulkanAV1Picture *
gst_vulkan_av1_picture_new (GstVulkanAV1Decoder * self, GstBuffer * out)
{
  GstVulkanAV1Picture *pic;

  pic = g_new0 (GstVulkanAV1Picture, 1);
  gst_vulkan_decoder_picture_init (self->decoder, &pic->base, out);

  pic->tile_sizes = g_array_new (TRUE, TRUE, sizeof (guint32));
  pic->tile_offsets = g_array_new (TRUE, TRUE, sizeof (guint32));
  pic->tile_data_sz = 0;
  pic->slot_idx = -1;
  pic->free_slot_mask = &self->free_slot_mask;

  return pic;
}

static void
gst_vulkan_av1_picture_free (gpointer data)
{
  GstVulkanAV1Picture *pic = data;

  // Mark our slot as free in the decoder, if we were assigned any.
  if (pic->slot_idx >= 0)
    *pic->free_slot_mask &= ~(1 << pic->slot_idx);

  gst_vulkan_decoder_picture_release (&pic->base);
  g_clear_pointer (&pic->tile_offsets, g_array_unref);
  g_clear_pointer (&pic->tile_sizes, g_array_unref);
  g_free (pic);
}

static GstFlowReturn
_check_resolution_change (GstVulkanAV1Decoder * self, GstAV1Picture * picture)
{
  const GstAV1FrameHeaderOBU *frame_hdr = &picture->frame_hdr;

  if (!self->output_state) {
    GST_DEBUG_OBJECT (self, "output_state not yet initialized");
    return GST_FLOW_OK;
  }

  if (self->resolution_changed
      || self->coded_width != frame_hdr->frame_width
      || self->coded_height != frame_hdr->frame_height) {
    GstVideoInfo *info = &self->output_state->info;
    GST_VIDEO_INFO_WIDTH (info) = self->coded_width = frame_hdr->frame_width;
    GST_VIDEO_INFO_HEIGHT (info) = self->coded_height = frame_hdr->frame_height;

    self->need_negotiation = TRUE;

    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
      GST_ERROR_OBJECT (self, "Resolution changed, but failed to"
          " negotiate with downstream");
      return GST_FLOW_NOT_NEGOTIATED;
    }
    self->resolution_changed = TRUE;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vulkan_av1_decoder_new_picture (GstAV1Decoder * decoder,
    GstVideoCodecFrame * frame, GstAV1Picture * picture)
{
  GstVulkanAV1Decoder *self = GST_VULKAN_AV1_DECODER (decoder);
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstFlowReturn ret;
  GstVulkanAV1Picture *pic;

  GST_TRACE_OBJECT (self, "New picture");

  ret = _check_resolution_change (self, picture);
  if (ret != GST_FLOW_OK)
    return ret;

  if (self->need_negotiation) {
    if (!gst_video_decoder_negotiate (vdec)) {
      GST_ERROR_OBJECT (self, "Failed downstream negotiation.");
      return GST_FLOW_ERROR;
    }
  }

  ret = gst_video_decoder_allocate_output_frame (vdec, frame);
  if (ret != GST_FLOW_OK)
    goto allocation_failed;

  pic = gst_vulkan_av1_picture_new (self, frame->output_buffer);
  gst_av1_picture_set_user_data (picture, pic, gst_vulkan_av1_picture_free);

  return GST_FLOW_OK;

allocation_failed:
  {
    GST_WARNING_OBJECT (self, "Failed to allocated input or output buffer: %s",
        gst_flow_get_name (ret));
    return ret;
  }
}

static void
_fill_ref_slot (GstVulkanAV1Decoder * self, GstAV1Picture * picture,
    VkVideoReferenceSlotInfoKHR * slot, VkVideoPictureResourceInfoKHR * res,
    VkVideoDecodeAV1DpbSlotInfoKHR * vkav1_slot,
    StdVideoDecodeAV1ReferenceInfo * stdav1_ref, GstVulkanDecoderPicture ** ref)
{
  GstVulkanAV1Picture *pic = gst_av1_picture_get_user_data (picture);
  GstAV1FrameHeaderOBU *fh = &picture->frame_hdr;
  guint8 ref_frame_sign_bias = 0;
  guint8 i;

  for (i = 0; i < STD_VIDEO_AV1_NUM_REF_FRAMES; i++) {
    ref_frame_sign_bias |= (fh->ref_frame_sign_bias[i] <= 0) << i;
    stdav1_ref->SavedOrderHints[i] = fh->order_hints[i];
  }

  /* *INDENT-OFF* */
  *stdav1_ref = (StdVideoDecodeAV1ReferenceInfo) {
    .flags = (StdVideoDecodeAV1ReferenceInfoFlags) {
      .disable_frame_end_update_cdf = fh->disable_frame_end_update_cdf,
      .segmentation_enabled = fh->segmentation_params.segmentation_enabled,
    },
    .frame_type = (StdVideoAV1FrameType)fh->frame_type,
    .RefFrameSignBias = ref_frame_sign_bias,
    .OrderHint = fh->order_hint,
  };

  *vkav1_slot = (VkVideoDecodeAV1DpbSlotInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_DPB_SLOT_INFO_KHR,
    .pStdReferenceInfo = stdav1_ref,
  };

  *res = (VkVideoPictureResourceInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
    .codedExtent = { self->coded_width, self->coded_height },
    .baseArrayLayer = (self->decoder->layered_dpb && self->decoder->dedicated_dpb) ? pic->slot_idx : 0,
    .imageViewBinding = pic->base.img_view_ref->view,
  };

  *slot = (VkVideoReferenceSlotInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR,
    .pNext = vkav1_slot,
    .slotIndex = pic->slot_idx,
    .pPictureResource = res,
  };
  /* *INDENT-ON* */

  if (ref)
    *ref = &pic->base;

  GST_TRACE_OBJECT (self, "0x%" G_GUINT64_FORMAT "x slotIndex: %d",
      res->imageViewBinding, slot->slotIndex);
}

static gint32
_find_next_slot_idx (GstVulkanAV1Decoder * self)
{
  gint32 i;
  g_return_val_if_fail (self != NULL, -1);


  for (i = 0; i < self->dpb_size; i++)
    if (!(self->free_slot_mask & (1 << i))) {
      // Mark as used.
      self->free_slot_mask |= (1 << i);
      return i;
    }

  GST_ERROR_OBJECT (self,
      "Failed to find free DPB slot (dpb_size=%d, free_mask=0x%08x)",
      self->dpb_size, self->free_slot_mask);
  return -1;
}

static inline guint8
gst_vulkan_av1_dec_get_lr_unit_size (guint size)
{
  switch (size) {
    case 32:
      return 0;
    case 64:
      return 1;
    case 128:
      return 2;
    case 256:
      return 3;
    default:
      break;
  }

  return 3;
}

static GstFlowReturn
gst_vulkan_av1_decoder_start_picture (GstAV1Decoder * decoder,
    GstAV1Picture * picture, GstAV1Dpb * dpb)
{

  GstVulkanAV1Decoder *self = GST_VULKAN_AV1_DECODER (decoder);
  GstAV1FrameHeaderOBU *fh = &picture->frame_hdr;
  GstAV1QuantizationParams *qp = &fh->quantization_params;
  GstAV1LoopFilterParams *lf = &fh->loop_filter_params;
  GstAV1SegmenationParams *seg = &fh->segmentation_params;
  GstAV1LoopRestorationParams *lr = &fh->loop_restoration_params;
  GstAV1TileInfo *ti = &fh->tile_info;
  GstAV1CDEFParams *cdef = &fh->cdef_params;
  GstAV1FilmGrainParams *fg = &fh->film_grain_params;
  GstAV1GlobalMotionParams *gm = &fh->global_motion_params;
  GstVulkanAV1Picture *pic = gst_av1_picture_get_user_data (picture);
  guint num_refs = 0;
  guint i, j;

  GST_TRACE_OBJECT (self, "Start picture");

  /* *INDENT-OFF* */
  pic->tile_info = (StdVideoAV1TileInfo) {
    .flags = (StdVideoAV1TileInfoFlags) {
      .uniform_tile_spacing_flag = ti->uniform_tile_spacing_flag,
    },
    .TileCols = ti->tile_cols,
    .TileRows = ti->tile_rows,
    .context_update_tile_id = ti->context_update_tile_id,
    .tile_size_bytes_minus_1 = ti->tile_size_bytes_minus_1,
    .pWidthInSbsMinus1 = pic->width_in_sbs_minus1,
    .pHeightInSbsMinus1 = pic->height_in_sbs_minus1,
    .pMiColStarts = pic->mi_col_starts,
    .pMiRowStarts = pic->mi_row_starts,
  };
  /* *INDENT-ON* */

  for (guint i = 0; i < 64; i++) {
    pic->width_in_sbs_minus1[i] = ti->width_in_sbs_minus_1[i];
    pic->height_in_sbs_minus1[i] = ti->height_in_sbs_minus_1[i];
    pic->mi_col_starts[i] = ti->mi_col_starts[i];
    pic->mi_row_starts[i] = ti->mi_row_starts[i];
  }
  /* *INDENT-OFF* */
  pic->quantization = (StdVideoAV1Quantization) {
    .flags = (StdVideoAV1QuantizationFlags) {
      .diff_uv_delta = qp->diff_uv_delta,
      .using_qmatrix = qp->using_qmatrix,
    },
    .base_q_idx = qp->base_q_idx,
    .DeltaQYDc = qp->delta_q_y_dc,
    .DeltaQUDc = qp->delta_q_u_dc,
    .DeltaQUAc = qp->delta_q_u_ac,
    .DeltaQVDc = qp->delta_q_v_dc,
    .DeltaQVAc = qp->delta_q_v_ac,
    .qm_y = qp->qm_y,
    .qm_u = qp->qm_u,
    .qm_v = qp->qm_v,
  };


  pic->loop_filter = (StdVideoAV1LoopFilter) {
    .flags = (StdVideoAV1LoopFilterFlags) {
      .loop_filter_delta_enabled = lf->loop_filter_delta_enabled,
      .loop_filter_delta_update = lf->loop_filter_delta_update,
    },
    .loop_filter_sharpness = lf->loop_filter_sharpness,
  };
  /* *INDENT-ON* */

  for (i = 0; i < STD_VIDEO_AV1_TOTAL_REFS_PER_FRAME; i++)
    pic->loop_filter.loop_filter_ref_deltas[i] = lf->loop_filter_ref_deltas[i];


  for (i = 0; i < STD_VIDEO_AV1_LOOP_FILTER_ADJUSTMENTS; i++)
    pic->loop_filter.loop_filter_mode_deltas[i] =
        lf->loop_filter_mode_deltas[i];


  for (i = 0; i < STD_VIDEO_AV1_MAX_LOOP_FILTER_STRENGTHS; i++)
    pic->loop_filter.loop_filter_level[i] = lf->loop_filter_level[i];

  /* *INDENT-OFF* */
  pic->cdef = (StdVideoAV1CDEF) {
    .cdef_damping_minus_3 = cdef->cdef_damping - 3,
    .cdef_bits = cdef->cdef_bits,
  };
  /* *INDENT-ON* */

  for (i = 0; i < STD_VIDEO_AV1_MAX_CDEF_FILTER_STRENGTHS; i++) {
    pic->cdef.cdef_y_pri_strength[i] = cdef->cdef_y_pri_strength[i];
    // Trick from gstnvav1dec.c
    pic->cdef.cdef_y_sec_strength[i] =
        cdef->cdef_y_sec_strength[i] == 4 ? 3 : cdef->cdef_y_sec_strength[i];
    pic->cdef.cdef_uv_pri_strength[i] = cdef->cdef_uv_pri_strength[i];
    // Trick from gstnvav1dec.c
    pic->cdef.cdef_uv_sec_strength[i] =
        cdef->cdef_uv_sec_strength[i] == 4 ? 3 : cdef->cdef_uv_sec_strength[i];
  }

  for (i = 0; i < 3; i++) {
    pic->loop_restoration.FrameRestorationType[i] =
        (StdVideoAV1FrameRestorationType) lr->frame_restoration_type[i];
    pic->loop_restoration.LoopRestorationSize[i] =
        gst_vulkan_av1_dec_get_lr_unit_size (lr->loop_restoration_size[i]);
  }

  for (i = 0; i < GST_AV1_MAX_SEGMENTS; i++) {
    pic->segmentation.FeatureEnabled[i] = 0;
    for (j = 0; j < GST_AV1_SEG_LVL_MAX; j++) {
      pic->segmentation.FeatureEnabled[i] |= seg->feature_enabled[i][j] << j;
      pic->segmentation.FeatureData[i][j] = seg->feature_data[i][j];
    }
  }
  /* *INDENT-OFF* */
  pic->film_grain = (StdVideoAV1FilmGrain) {
    .flags = (StdVideoAV1FilmGrainFlags) {
      .chroma_scaling_from_luma = fg->chroma_scaling_from_luma,
      .overlap_flag = fg->overlap_flag,
      fg->clip_to_restricted_range = fg->clip_to_restricted_range,
    },
    .grain_scaling_minus_8 = fg->grain_scaling_minus_8,
    .ar_coeff_lag = fg->ar_coeff_lag,
    .ar_coeff_shift_minus_6 = fg->ar_coeff_shift_minus_6,
    .grain_scale_shift = fg->grain_scale_shift,
    .grain_seed = fg->grain_seed,
    .film_grain_params_ref_idx = fg->film_grain_params_ref_idx,
    .num_y_points = fg->num_y_points,
    .num_cb_points = fg->num_cb_points,
    .num_cr_points = fg->num_cr_points,
    .cb_mult = fg->cb_mult,
    .cb_luma_mult = fg->cb_luma_mult,
    .cb_offset = fg->cb_offset,
    .cr_mult = fg->cr_mult,
    .cr_luma_mult = fg->cr_luma_mult,
    .cr_offset = fg->cr_offset,
  };
  /* *INDENT-ON* */

  if (fg->apply_grain) {
    for (i = 0; i < STD_VIDEO_AV1_MAX_NUM_Y_POINTS; i++) {
      pic->film_grain.point_y_value[i] = fg->point_y_value[i];
      pic->film_grain.point_y_scaling[i] = fg->point_y_scaling[i];
    }

    for (i = 0; i < STD_VIDEO_AV1_MAX_NUM_CB_POINTS; i++) {
      pic->film_grain.point_cb_value[i] = fg->point_cb_value[i];
      pic->film_grain.point_cb_scaling[i] = fg->point_cb_scaling[i];
      pic->film_grain.point_cr_value[i] = fg->point_cr_value[i];
      pic->film_grain.point_cr_scaling[i] = fg->point_cr_scaling[i];
    }

    for (i = 0; i < STD_VIDEO_AV1_MAX_NUM_POS_LUMA; i++)
      pic->film_grain.ar_coeffs_y_plus_128[i] = fg->ar_coeffs_y_plus_128[i];

    for (i = 0; i < STD_VIDEO_AV1_MAX_NUM_POS_CHROMA; i++) {
      pic->film_grain.ar_coeffs_cb_plus_128[i] = fg->ar_coeffs_cb_plus_128[i];
      pic->film_grain.ar_coeffs_cr_plus_128[i] = fg->ar_coeffs_cr_plus_128[i];
    }
  }

  for (i = 0; i < 8; i++) {
    pic->global_motion.GmType[i] = gm->gm_type[i];
    for (j = 0; j < STD_VIDEO_AV1_GLOBAL_MOTION_PARAMS; j++) {
      pic->global_motion.gm_params[i][j] = gm->gm_params[i][j];
    }
  }
  /* *INDENT-OFF* */
  pic->std_av1pic = (StdVideoDecodeAV1PictureInfo) {
    .flags = (StdVideoDecodeAV1PictureInfoFlags) {
            .error_resilient_mode = fh->error_resilient_mode,
            .disable_cdf_update = fh->disable_cdf_update,
            .use_superres = fh->use_superres,
            .render_and_frame_size_different = fh->render_and_frame_size_different,
            .allow_screen_content_tools = fh->allow_screen_content_tools,
            .is_filter_switchable = fh->is_filter_switchable,
            .force_integer_mv = fh->force_integer_mv,
            .frame_size_override_flag = fh->frame_size_override_flag,
            .buffer_removal_time_present_flag = fh->buffer_removal_time_present_flag,
            .allow_intrabc = fh->allow_intrabc,
            .frame_refs_short_signaling = fh->frame_refs_short_signaling,
            .allow_high_precision_mv = fh->allow_high_precision_mv,
            .is_motion_mode_switchable = fh->is_motion_mode_switchable,
            .use_ref_frame_mvs = fh->use_ref_frame_mvs,
            .disable_frame_end_update_cdf = fh->disable_frame_end_update_cdf,
            .allow_warped_motion = fh->allow_warped_motion,
            .reduced_tx_set = fh->reduced_tx_set,
            .reference_select = fh->reference_select,
            .skip_mode_present = fh->skip_mode_present,
            .delta_q_present = qp->delta_q_present,
            .delta_lf_present = lf->delta_lf_present,
            .delta_lf_multi = lf->delta_lf_multi,
            .segmentation_enabled = seg->segmentation_enabled,
            .segmentation_update_map = seg->segmentation_update_map,
            .segmentation_temporal_update = seg->segmentation_temporal_update,
            .segmentation_update_data = seg->segmentation_update_data,
            .UsesLr = lr->uses_lr,
    },
    .frame_type = (StdVideoAV1FrameType) fh->frame_type,
    .current_frame_id = fh->current_frame_id,
    .OrderHint = fh->order_hint,
    .primary_ref_frame = fh->primary_ref_frame,
    .refresh_frame_flags = fh->refresh_frame_flags,
    .interpolation_filter = (StdVideoAV1InterpolationFilter) fh->interpolation_filter,
    .TxMode = (StdVideoAV1TxMode) fh->tx_mode,
    .delta_q_res = qp->delta_q_res,
    .delta_lf_res = lf->delta_lf_res,
    .SkipModeFrame[0] = fh->skip_mode_frame[0],
    .SkipModeFrame[1] = fh->skip_mode_frame[1],
    .coded_denom = fh->use_superres ? fh->superres_denom - 9 : 0,
    /* .OrderHints (filled below) */
    .pTileInfo = &pic->tile_info,
    .pQuantization = &pic->quantization,
    .pSegmentation = &pic->segmentation,
    .pLoopFilter = &pic->loop_filter,
    .pCDEF = &pic->cdef,
    .pLoopRestoration = &pic->loop_restoration,
    .pGlobalMotion = &pic->global_motion,
    .pFilmGrain = &pic->film_grain,
  };

  for (i = 0; i < VK_MAX_VIDEO_AV1_REFERENCES_PER_FRAME_KHR; i++)
    pic->std_av1pic.OrderHints[i] = fh->order_hints[i];

  pic->vk_av1pic = (VkVideoDecodeAV1PictureInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_PICTURE_INFO_KHR,
    .pStdPictureInfo = &pic->std_av1pic,
    .frameHeaderOffset = 0, /* ?? */
    /*
     * Filled in end_picture():
     *
     * uint32_t                               tileCount;
     * const uint32_t*                        pTileOffsets;
     * const uint32_t*                        pTileSizes;
     */

  };
  /* *INDENT-ON* */

  for (i = 0; i < VK_MAX_VIDEO_AV1_REFERENCES_PER_FRAME_KHR; i++) {
    gint ref_idx = fh->ref_frame_idx[i];
    if (ref_idx >= 0) {
      GstAV1Picture *ref_pic = dpb->pic_list[ref_idx];
      if (ref_pic) {
        GstVulkanAV1Picture *ref_vk_pic =
            gst_av1_picture_get_user_data (ref_pic);

        pic->vk_av1pic.referenceNameSlotIndices[i] = ref_vk_pic->slot_idx;
      }
    } else {
      pic->vk_av1pic.referenceNameSlotIndices[i] = -1;
    }
  }

  pic->slot_idx = _find_next_slot_idx (self);
  if (pic->slot_idx < 0) {
    GST_ERROR_OBJECT (self, "No free DPB slots available");
    return GST_FLOW_ERROR;
  }
  /* fill main slot */
  _fill_ref_slot (self, picture, &pic->base.slot, &pic->base.pic_res,
      &pic->vk_slot, &pic->std_ref, NULL);

  for (i = 0; i < VK_MAX_VIDEO_AV1_REFERENCES_PER_FRAME_KHR; i++) {
    gint ref_idx = fh->ref_frame_idx[i];
    if (ref_idx >= 0) {
      GstAV1Picture *ref_pic = dpb->pic_list[ref_idx];
      int found = 0;

      if (ref_pic) {
        GstVulkanAV1Picture *ref_vk_pic =
            gst_av1_picture_get_user_data (ref_pic);

        for (j = 0; j < num_refs; j++) {
          if (pic->base.slots[j].slotIndex == ref_vk_pic->slot_idx) {
            found = 1;
            break;
          }
        }

        if (found)
          continue;

        _fill_ref_slot (self, ref_pic, &pic->base.slots[num_refs],
            &pic->base.pics_res[num_refs], &pic->vk_slots[num_refs],
            &pic->std_refs[num_refs], &pic->base.refs[num_refs]);

        num_refs++;
      }
    }
  }

  /* *INDENT-OFF* */
  pic->base.decode_info = (VkVideoDecodeInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR,
    .pNext = &pic->vk_av1pic,
    .flags = 0x0,
    .pSetupReferenceSlot = &pic->base.slot,
    .referenceSlotCount = num_refs,
    .pReferenceSlots = (const VkVideoReferenceSlotInfoKHR *) &pic->base.slots,
    .dstPictureResource = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
    // .codedOffset = {0, 0} /* is there any cropping rectangle in AV1? */
      .codedExtent = { self->coded_width, self->coded_height },
      .baseArrayLayer = 0,
      .imageViewBinding = pic->base.img_view_out->view,
    },
  };
  /* *INDENT-ON* */

  self->resolution_changed = FALSE;

  /* only wait if there's a buffer processed */
  if (GST_CODEC_PICTURE_FRAME_NUMBER (picture) > 0) {
    if (!gst_vulkan_decoder_wait (self->decoder)) {
      GST_ERROR_OBJECT (self, "Error at waiting for decoding operation to end");
      return GST_FLOW_ERROR;
    }
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vulkan_av1_decoder_decode_tile (GstAV1Decoder * decoder,
    GstAV1Picture * picture, GstAV1Tile * tile)
{
  GstVulkanAV1Decoder *self = GST_VULKAN_AV1_DECODER (decoder);
  GstAV1TileGroupOBU *tile_group = &tile->tile_group;
  GstVulkanAV1Picture *pic;
  guint i;

  GST_TRACE_OBJECT (self, "Decode tile");

  pic = gst_av1_picture_get_user_data (picture);
  g_assert (pic);

  if (!gst_vulkan_decoder_append_slice (self->decoder, &pic->base,
          tile->obu.data, tile->obu.obu_size, FALSE))
    return GST_FLOW_ERROR;

  for (i = tile_group->tg_start; i <= tile_group->tg_end; i++) {
    guint32 offset = tile_group->entry[i].tile_offset + pic->tile_data_sz;

    g_array_append_val (pic->tile_sizes, tile_group->entry[i].tile_size);
    g_array_append_val (pic->tile_offsets, offset);
    pic->num_tiles++;
  }

  pic->tile_data_sz += tile->obu.obu_size;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vulkan_av1_decoder_end_picture (GstAV1Decoder * decoder,
    GstAV1Picture * picture)
{
  GstVulkanAV1Decoder *self = GST_VULKAN_AV1_DECODER (decoder);
  GstVulkanAV1Picture *pic;
  GError *error = NULL;
  VkVideoDecodeAV1InlineSessionParametersInfoKHR inline_params = {
    .sType =
        VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_INLINE_SESSION_PARAMETERS_INFO_KHR,
    .pStdSequenceHeader = &self->vk.sequence,
  };

  GST_TRACE_OBJECT (self, "End picture");

  pic = gst_av1_picture_get_user_data (picture);
  g_assert (pic);

  if (pic->base.slice_offs->len == 0)
    return GST_FLOW_OK;

  pic->vk_av1pic.pTileOffsets = &g_array_index (pic->tile_offsets, guint32, 0);
  pic->vk_av1pic.tileCount = pic->num_tiles;
  pic->vk_av1pic.pTileSizes = &g_array_index (pic->tile_sizes, guint32, 0);

  if (gst_vulkan_decoder_has_feature (self->decoder,
          GST_VULKAN_DECODER_FEATURE_INLINE_PARAMS))
    vk_link_struct (&pic->base.decode_info, &inline_params);

  GST_LOG_OBJECT (self, "Decoding frame, %d", picture->display_frame_id);

  if (!gst_vulkan_decoder_decode (self->decoder, &pic->base, &error)) {
    GST_ERROR_OBJECT (self, "Couldn't decode frame: %s",
        error ? error->message : "");
    g_clear_error (&error);
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vulkan_av1_decoder_output_picture (GstAV1Decoder * decoder,
    GstVideoCodecFrame * frame, GstAV1Picture * picture)
{
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstVulkanAV1Decoder *self = GST_VULKAN_AV1_DECODER (decoder);

  GST_TRACE_OBJECT (self, "Output picture");

  GST_LOG_OBJECT (self,
      "Outputting picture %p (poc %d)", picture, picture->display_frame_id);

  if (GST_CODEC_PICTURE (picture)->discont_state) {
    self->need_negotiation = TRUE;
    if (!gst_video_decoder_negotiate (vdec)) {
      gst_av1_picture_unref (picture);
      GST_ERROR_OBJECT (self, "Could not re-negotiate with updated state");
      return GST_FLOW_ERROR;
    }
  }

  gst_av1_picture_unref (picture);

  return gst_video_decoder_finish_frame (vdec, frame);
}

static GstAV1Picture *
gst_vulkan_av1_decoder_duplicate_picture (GstAV1Decoder * decoder,
    GstVideoCodecFrame * frame, GstAV1Picture * picture)
{
  GstVulkanAV1Decoder *self = GST_VULKAN_AV1_DECODER (decoder);
  GstVulkanAV1Picture *pic, *new_pic;
  GstAV1Picture *new_picture;

  pic = gst_av1_picture_get_user_data (picture);
  if (!pic) {
    GST_ERROR_OBJECT (self, "Parent picture does not have a vulkan picture");
    return NULL;
  }

  new_picture = gst_av1_picture_new ();
  new_picture->frame_hdr = picture->frame_hdr;
  new_pic = gst_vulkan_av1_picture_new (self, pic->base.out);

  frame->output_buffer = gst_buffer_ref (new_pic->base.out);

  GST_LOG_OBJECT (self, "Duplicate output with buffer %" GST_PTR_FORMAT, pic);

  gst_av1_picture_set_user_data (new_picture, new_pic,
      gst_vulkan_av1_picture_free);

  return new_picture;
}

static void
gst_vulkan_av1_decoder_init (GTypeInstance * instance, gpointer klass)
{
  gst_vulkan_buffer_memory_init_once ();
}

struct CData
{
  gchar *description;
  gint device_index;
};

static void
gst_vulkan_av1_decoder_class_init (gpointer klass, gpointer class_data)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstAV1DecoderClass *av1decoder_class = GST_AV1_DECODER_CLASS (klass);
  GstVulkanAV1DecoderClass *vk_av1decoder_class =
      GST_VULKAN_AV1_DECODER_CLASS (klass);
  struct CData *cdata = class_data;
  gchar *long_name;
  const gchar *name;


  name = "Vulkan AV1 decoder";
  if (cdata->description)
    long_name = g_strdup_printf ("%s on %s", name, cdata->description);
  else
    long_name = g_strdup (name);

  vk_av1decoder_class->device_index = cdata->device_index;

  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Decoder/Video/Hardware", "An AV1 video decoder based on Vulkan",
      "Daniel Almeida <daniel.almeida@collabora.com>");

  parent_class = g_type_class_peek_parent (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_vulkan_av1dec_sink_template);

  gst_element_class_add_static_pad_template (element_class,
      &gst_vulkan_av1dec_src_template);

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_vulkan_av1_decoder_set_context);

  decoder_class->src_query =
      GST_DEBUG_FUNCPTR (gst_vulkan_av1_decoder_src_query);
  decoder_class->sink_query =
      GST_DEBUG_FUNCPTR (gst_vulkan_av1_decoder_sink_query);
  decoder_class->open = GST_DEBUG_FUNCPTR (gst_vulkan_av1_decoder_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_vulkan_av1_decoder_close);
  decoder_class->stop = GST_DEBUG_FUNCPTR (gst_vulkan_av1_decoder_stop);
  decoder_class->negotiate =
      GST_DEBUG_FUNCPTR (gst_vulkan_av1_decoder_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_vulkan_av1_decoder_decide_allocation);

  av1decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_vulkan_av1_decoder_new_sequence);
  av1decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_vulkan_av1_decoder_new_picture);
  av1decoder_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_vulkan_av1_decoder_start_picture);

  av1decoder_class->decode_tile =
      GST_DEBUG_FUNCPTR (gst_vulkan_av1_decoder_decode_tile);
  av1decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_vulkan_av1_decoder_end_picture);
  av1decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_vulkan_av1_decoder_output_picture);
  av1decoder_class->duplicate_picture =
      GST_DEBUG_FUNCPTR (gst_vulkan_av1_decoder_duplicate_picture);

  g_free (long_name);
  g_free (cdata->description);
  g_free (cdata);
}

gboolean
gst_vulkan_av1_decoder_register (GstPlugin * plugin, GstVulkanDevice * device,
    guint rank)
{
  static GOnce debug_once = G_ONCE_INIT;
  GType type;
  GTypeInfo type_info = {
    .class_size = sizeof (GstVulkanAV1DecoderClass),
    .class_init = gst_vulkan_av1_decoder_class_init,
    .instance_size = sizeof (GstVulkanAV1Decoder),
    .instance_init = gst_vulkan_av1_decoder_init,
  };
  struct CData *cdata;
  gboolean ret;
  gchar *type_name, *feature_name;

  cdata = g_new (struct CData, 1);
  cdata->description = NULL;
  cdata->device_index = device->physical_device->device_index;

  g_return_val_if_fail (GST_IS_PLUGIN (plugin), FALSE);

  gst_vulkan_create_feature_name (device, "GstVulkanAV1Decoder",
      "GstVulkanAV1Device%dDecoder", &type_name, "vulkanav1dec",
      "vulkanav1device%ddec", &feature_name, &cdata->description, &rank);

  type_info.class_data = cdata;

  g_once (&debug_once, _register_debug_category, NULL);
  type =
      g_type_register_static (GST_TYPE_AV1_DECODER, type_name, &type_info, 0);

  ret = gst_element_register (plugin, feature_name, rank, type);

  g_free (type_name);
  g_free (feature_name);

  return ret;
}
