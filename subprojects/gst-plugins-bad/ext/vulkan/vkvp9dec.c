/* GStreamer
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

/**
 * SECTION:element-vulkanvp9dec
 * @title: vulkanvp9dec
 * @short_description: A Vulkan based VP9 video decoder
 *
 * vulkanvp9dec decodes VP9 bitstreams into raw video surfaces using
 * Vulkan video extensions.
 *
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 filesrc location=video.webm ! matroskademux ! vp9parse ! vulkanvp9dec ! vulkandownload ! videoconvert ! autovideosink
 * ```
 *
 * Since: 1.28
 */

#include "vkvp9dec.h"

#include <gst/video/video.h>
#include <gst/vulkan/vulkan.h>
#include <vk_video/vulkan_video_codec_vp9std.h>
#include <vk_video/vulkan_video_codec_vp9std_decode.h>
#include <vulkan/vulkan_core.h>
#include "glib.h"
#include "gst/codecparsers/gstvp9parser.h"
#include "gst/codecs/gstvp9picture.h"
#include "gst/gstpad.h"
#include "gst/vulkan/gstvkdecoder-private.h"

#include "gstvulkanelements.h"

GST_DEBUG_CATEGORY_STATIC (gst_vulkan_vp9_decoder_debug);
#define GST_CAT_DEFAULT gst_vulkan_vp9_decoder_debug


#define GST_VULKAN_VP9_DECODER(obj)            ((GstVulkanVp9Decoder *) obj)
#define GST_VULKAN_VP9_DECODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_FROM_INSTANCE (obj), GstVulkanVp9DecoderClass))
#define GST_VULKAN_VP9_DECODER_CLASS(klass)    ((GstVulkanVp9DecoderClass *) klass)

static GstElementClass *parent_class = NULL;

#define GST_VULKAN_VP9_MAX_DPB_SLOTS 32

struct CData
{
  gchar *description;
  gint device_index;
};

typedef struct _GstVulkanVp9Decoder GstVulkanVp9Decoder;
typedef struct _GstVulkanVp9DecoderClass GstVulkanVp9DecoderClass;
typedef struct _GstVulkanVp9Picture GstVulkanVp9Picture;

struct _GstVulkanVp9Decoder
{
  GstVp9Decoder parent;

  GstVulkanInstance *instance;
  GstVulkanDevice *device;
  GstVulkanQueue *graphic_queue, *decode_queue;

  GstVulkanDecoder *decoder;

  gboolean need_negotiation;
  gboolean resolution_changed;

  gint coded_width, coded_height;
  gint dpb_size;

  VkSamplerYcbcrRange range;
  VkChromaLocation yloc;

  GstVideoCodecState *output_state;
  GstVideoCodecState *input_state;
  struct
  {
    StdVideoVP9ColorConfig color_config;
  } vk;

  guint32 free_slot_mask;
  gboolean last_show_frame;
};

struct _GstVulkanVp9Picture
{
  GstVulkanDecoderPicture base;

  StdVideoVP9Segmentation segmentation;
  StdVideoVP9LoopFilter loop_filter;

  VkVideoDecodeVP9PictureInfoKHR vk_pic;
  StdVideoDecodeVP9PictureInfo std_pic;

  gint32 slot_idx;

  /* Used to update the mask when this picture is freed. */
  guint32 *free_slot_mask;
};
struct _GstVulkanVp9DecoderClass
{
  GstVp9DecoderClass parent;

  gint device_index;
};

static GstStaticPadTemplate gst_vulkan_vp9dec_sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vp9, "
        "profile = (string) { 0, 1, 2, 3 }, " "alignment = (string) frame"));

static GstStaticPadTemplate gst_vulkan_vp9dec_src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE, "NV12")));

#define gst_vulkan_vp9_decoder_parent_class parent_class

static gpointer
_register_debug_category (gpointer data)
{
  GST_DEBUG_CATEGORY_INIT (gst_vulkan_vp9_decoder_debug, "vulkanvp9dec", 0,
      "Vulkan VP9 decoder");

  return NULL;
}

static gboolean
_find_queues (GstVulkanDevice * device, GstVulkanQueue * queue, gpointer data)
{
  GstVulkanVp9Decoder *self = (GstVulkanVp9Decoder *) data;
  guint32 flags =
      device->physical_device->queue_family_props[queue->family].queueFlags;
  guint32 codec =
      device->physical_device->queue_family_ops[queue->family].video;

  if (!self->graphic_queue
      && ((flags & VK_QUEUE_GRAPHICS_BIT) == VK_QUEUE_GRAPHICS_BIT)) {
    self->graphic_queue = (GstVulkanQueue *) gst_object_ref (queue);
  }

  if (!self->decode_queue
      && ((codec & VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR)
          == VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR)
      && ((flags & VK_QUEUE_VIDEO_DECODE_BIT_KHR)
          == VK_QUEUE_VIDEO_DECODE_BIT_KHR)) {
    self->decode_queue = (GstVulkanQueue *) gst_object_ref (queue);
  }

  return !(self->decode_queue && self->graphic_queue);
}

static gboolean
gst_vulkan_vp9_decoder_open (GstVideoDecoder * decoder)
{
  GstVulkanVp9Decoder *self = GST_VULKAN_VP9_DECODER (decoder);

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
        ("Failed to create/retrieve vulkan VP9 decoder queue"), (NULL));
    return FALSE;
  }

  self->decoder = gst_vulkan_decoder_new_from_queue (self->decode_queue,
      VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR);
  if (!self->decoder) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Failed to create vulkan VP9 decoder"), (NULL));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_vulkan_vp9_decoder_close (GstVideoDecoder * decoder)
{
  GstVulkanVp9Decoder *self = GST_VULKAN_VP9_DECODER (decoder);

  gst_clear_object (&self->decoder);
  gst_clear_object (&self->decode_queue);
  gst_clear_object (&self->graphic_queue);
  gst_clear_object (&self->device);
  gst_clear_object (&self->instance);

  return TRUE;
}

static gboolean
gst_vulkan_vp9_decoder_stop (GstVideoDecoder * decoder)
{
  GstVulkanVp9Decoder *self = GST_VULKAN_VP9_DECODER (decoder);

  if (self->decoder)
    gst_vulkan_decoder_stop (self->decoder);

  g_clear_pointer (&self->output_state, gst_video_codec_state_unref);
  g_clear_pointer (&self->input_state, gst_video_codec_state_unref);

  return GST_VIDEO_DECODER_CLASS (parent_class)->stop (decoder);
}

static void
gst_vulkan_vp9_decoder_set_context (GstElement * element, GstContext * context)
{
  GstVulkanVp9Decoder *self = GST_VULKAN_VP9_DECODER (element);

  gst_vulkan_handle_set_context (element, context, NULL, &self->instance);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
_query_context (GstVulkanVp9Decoder * self, GstQuery * query)
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
gst_vulkan_vp9_decoder_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      ret = _query_context (GST_VULKAN_VP9_DECODER (decoder), query);
      break;
    default:
      ret = GST_VIDEO_DECODER_CLASS (parent_class)->src_query (decoder, query);
      break;
  }

  return ret;
}

static gboolean
gst_vulkan_vp9_decoder_sink_query (GstVideoDecoder * decoder, GstQuery * query)
{
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      ret = _query_context (GST_VULKAN_VP9_DECODER (decoder), query);
      break;
    default:
      ret = GST_VIDEO_DECODER_CLASS (parent_class)->sink_query (decoder, query);
      break;
  }

  return ret;
}

static gboolean
gst_vulkan_vp9_decoder_negotiate (GstVideoDecoder * decoder)
{
  GstVulkanVp9Decoder *self = GST_VULKAN_VP9_DECODER (decoder);
  VkVideoFormatPropertiesKHR format_prop;
  GstVideoFormat format;

  /* Ignore downstream renegotiation request. */
  if (!self->need_negotiation) {
    GST_DEBUG_OBJECT (decoder,
        "Input state hasn't changed, no need to reconfigure downstream caps");
    goto bail;
  }

  if (!gst_vulkan_decoder_out_format (self->decoder, &format_prop))
    return FALSE;

  self->need_negotiation = FALSE;

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);

  format = gst_vulkan_format_to_video_format (format_prop.format);
  self->output_state = gst_video_decoder_set_interlaced_output_state (decoder,
      format, GST_VIDEO_INTERLACE_MODE_PROGRESSIVE, self->coded_width,
      self->coded_height, self->input_state);

  self->output_state->caps = gst_video_info_to_caps (&self->output_state->info);
  gst_caps_set_features_simple (self->output_state->caps,
      gst_caps_features_new_static_str (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE,
          NULL));

  GST_INFO_OBJECT (self, "Negotiated caps %" GST_PTR_FORMAT,
      self->output_state->caps);

bail:
  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_vulkan_vp9_decoder_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstVulkanVp9Decoder *self = GST_VULKAN_VP9_DECODER (decoder);
  GstCaps *new_caps, *profile_caps, *caps = NULL;
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
  gst_caps_unref (new_caps);

  if (!gst_buffer_pool_set_config (pool, config))
    goto bail;

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  gst_vulkan_decoder_create_dpb_pool (self->decoder, new_caps);

  return TRUE;

bail:
  {
    gst_clear_caps (&new_caps);
    gst_clear_object (&pool);
    return FALSE;
  }
}

static VkVideoChromaSubsamplingFlagBitsKHR
_get_chroma_subsampling_flag (const GstVp9FrameHeader * seq_hdr)
{
  switch (seq_hdr->profile) {
    case GST_VP9_PROFILE_0:
    case GST_VP9_PROFILE_2:
      return VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
      /* TODO: Add caps negociation to support these video formats
       * such as GST_VIDEO_FORMAT_Y42B or GST_VIDEO_FORMAT_Y444 etc. */
    case GST_VP9_PROFILE_1:
    case GST_VP9_PROFILE_3:
      if (seq_hdr->subsampling_x == 1 && seq_hdr->subsampling_y == 0)
        return VK_VIDEO_CHROMA_SUBSAMPLING_422_BIT_KHR;
      else if (seq_hdr->subsampling_x == 0 && seq_hdr->subsampling_y == 0)
        return VK_VIDEO_CHROMA_SUBSAMPLING_444_BIT_KHR;
      break;
    default:
      break;
  }
  return VK_VIDEO_CHROMA_SUBSAMPLING_INVALID_KHR;
}

static VkVideoComponentBitDepthFlagBitsKHR
_get_component_bit_depth (const GstVp9FrameHeader * seq_hdr)
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

static StdVideoVP9Profile
_get_vp9_profile (const GstVp9FrameHeader * seq_hdr)
{
  switch (seq_hdr->profile) {
    case GST_VP9_PROFILE_0:
      return STD_VIDEO_VP9_PROFILE_0;
    case GST_VP9_PROFILE_1:
      return STD_VIDEO_VP9_PROFILE_1;
    case GST_VP9_PROFILE_2:
      return STD_VIDEO_VP9_PROFILE_2;
    case GST_VP9_PROFILE_3:
      return STD_VIDEO_VP9_PROFILE_3;
    default:
      return STD_VIDEO_VP9_PROFILE_INVALID;
  }
}

static void
gst_vulkan_video_profile_from_vp9_frame_hdr (GstVulkanVideoProfile * profile,
    const GstVp9FrameHeader * frame_hdr)
{
  /* *INDENT-OFF* */
  *profile = (GstVulkanVideoProfile) {
    .profile = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
      .pNext = &profile->usage,
      .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR,
      .chromaSubsampling = _get_chroma_subsampling_flag (frame_hdr),
      .lumaBitDepth = _get_component_bit_depth(frame_hdr),
      .chromaBitDepth = _get_component_bit_depth (frame_hdr),
    },
    .usage.decode = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_USAGE_INFO_KHR,
      .pNext = &profile->codec,
      .videoUsageHints = VK_VIDEO_DECODE_USAGE_DEFAULT_KHR,
    },
    .codec.vp9dec = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_VP9_PROFILE_INFO_KHR,
      .stdProfile = _get_vp9_profile (frame_hdr),
    },
  };
  /* *INDENT-ON* */
}

static GstFlowReturn
gst_vulkan_vp9_decoder_new_sequence (GstVp9Decoder * decoder,
    const GstVp9FrameHeader * frame_hdr, gint max_dpb_size)
{
  GstVulkanVp9Decoder *self = GST_VULKAN_VP9_DECODER (decoder);
  GstVulkanVideoProfile profile;
  GstVulkanVideoCapabilities vk_caps;
  GError *error = NULL;
  gint width = frame_hdr->width;
  gint height = frame_hdr->height;
  VkFormat old_format = VK_FORMAT_UNDEFINED;
  VkVideoFormatPropertiesKHR format_prop;

  GST_DEBUG_OBJECT (self, "new sequence %dx%d", width, height);

  gst_vulkan_video_profile_from_vp9_frame_hdr (&profile, frame_hdr);

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

  gst_vulkan_decoder_caps (self->decoder, &vk_caps);

  if (frame_hdr->width < vk_caps.caps.minCodedExtent.width
      || frame_hdr->height < vk_caps.caps.minCodedExtent.height
      || frame_hdr->width > vk_caps.caps.maxCodedExtent.width
      || frame_hdr->height > vk_caps.caps.maxCodedExtent.height) {

    GST_ERROR_OBJECT (self,
        "The following sequence can not be decoded because the frame dimension does not fit the decoder bounds: %dx%d"
        ", minCodedExtent=%dx%d, maxCodedExtent=%dx%d",
        frame_hdr->width, frame_hdr->height, vk_caps.caps.minCodedExtent.width,
        vk_caps.caps.minCodedExtent.height, vk_caps.caps.maxCodedExtent.width,
        vk_caps.caps.maxCodedExtent.height);
    return GST_FLOW_ERROR;
  }
  self->resolution_changed = self->coded_width > 0 && self->coded_height > 0
      && (width != self->coded_width || height != self->coded_height);
  self->need_negotiation &= (width != self->coded_width
      || height != self->coded_height);

  self->coded_width = frame_hdr->width;
  self->coded_height = frame_hdr->height;

  self->vk.color_config = (StdVideoVP9ColorConfig) {
    /* *INDENT-OFF* */
    .flags = {
        .color_range = frame_hdr->color_range,
    },
    .BitDepth = frame_hdr->bit_depth,
    .subsampling_x = frame_hdr->subsampling_x,
    .subsampling_y = frame_hdr->subsampling_y,
    .color_space = (StdVideoVP9ColorSpace)frame_hdr->color_space,
    /* *INDENT-ON* */
  };

  self->dpb_size = MAX (self->dpb_size, max_dpb_size);

  g_clear_pointer (&self->input_state, gst_video_codec_state_unref);
  self->input_state = gst_video_codec_state_ref (decoder->input_state);

  /* Ycbcr sampler */
  {
    VkSamplerYcbcrRange range;
    VkChromaLocation yloc;
    gboolean ret;

    ret = gst_vulkan_decoder_out_format (self->decoder, &format_prop);
    g_assert (ret);

    range = (frame_hdr->color_range) ?
        VK_SAMPLER_YCBCR_RANGE_ITU_FULL : VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;

    yloc = VK_CHROMA_LOCATION_MIDPOINT;

    if (old_format != format_prop.format || range != self->range ||
        yloc != self->yloc) {
      self->range = range;
      self->yloc = yloc;
      ret =
          gst_vulkan_decoder_update_ycbcr_sampler (self->decoder, range,
          VK_CHROMA_LOCATION_COSITED_EVEN, yloc, &error);
      if (!ret && error) {
        GST_WARNING_OBJECT (self, "Unable to create Ycbcr sampler: %s",
            error->message);
        g_clear_error (&error);
      }
    }
  }

  return GST_FLOW_OK;
}

static GstVulkanVp9Picture *
gst_vulkan_vp9_picture_new (GstVulkanVp9Decoder * self, GstBuffer * out)
{
  GstVulkanVp9Picture *pic;

  pic = g_new0 (GstVulkanVp9Picture, 1);
  gst_vulkan_decoder_picture_init (self->decoder, &pic->base, out);

  pic->slot_idx = -1;
  pic->free_slot_mask = &self->free_slot_mask;

  return pic;
}

static void
gst_vulkan_vp9_picture_free (gpointer data)
{
  GstVulkanVp9Picture *pic = (GstVulkanVp9Picture *) data;

  // Mark our slot as free in the decoder, if we were assigned any.
  if (pic->slot_idx >= 0 && pic->slot_idx < GST_VULKAN_VP9_MAX_DPB_SLOTS)
    *pic->free_slot_mask &= ~(1 << pic->slot_idx);

  gst_vulkan_decoder_picture_release (&pic->base);

  g_free (pic);
}

static GstFlowReturn
_check_resolution_change (GstVulkanVp9Decoder * self, GstVp9Picture * picture)
{
  const GstVp9FrameHeader *frame_hdr = &picture->frame_hdr;
  GstVideoInfo *info = &self->output_state->info;

  if (self->resolution_changed || self->coded_width != frame_hdr->width
      || self->coded_height != frame_hdr->height) {
    GST_VIDEO_INFO_WIDTH (info) = self->coded_width = frame_hdr->width;
    GST_VIDEO_INFO_HEIGHT (info) = self->coded_height = frame_hdr->height;

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
gst_vulkan_vp9_decoder_new_picture (GstVp9Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp9Picture * picture)
{
  GstVulkanVp9Decoder *self = GST_VULKAN_VP9_DECODER (decoder);
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstFlowReturn ret;
  GstVulkanVp9Picture *pic;

  GST_TRACE_OBJECT (self, "New picture");

  ret = _check_resolution_change (self, picture);
  if (ret != GST_FLOW_OK)
    return ret;

  if (self->need_negotiation) {
    if (!gst_video_decoder_negotiate (vdec)) {
      GST_ERROR_OBJECT (self, "Failed to negotiate with downstream");
      return GST_FLOW_NOT_NEGOTIATED;
    }
  }

  ret = gst_video_decoder_allocate_output_frame (vdec, frame);
  if (ret != GST_FLOW_OK)
    goto allocation_failed;

  pic = gst_vulkan_vp9_picture_new (self, frame->output_buffer);
  gst_vp9_picture_set_user_data (picture, pic, gst_vulkan_vp9_picture_free);

  return GST_FLOW_OK;

allocation_failed:
  {
    GST_WARNING_OBJECT (self, "Failed to allocated input or output buffer: %s",
        gst_flow_get_name (ret));
    return ret;
  }
}

static void
_fill_ref_slot (GstVulkanVp9Decoder * self, GstVp9Picture * picture,
    VkVideoReferenceSlotInfoKHR * slot, VkVideoPictureResourceInfoKHR * res,
    GstVulkanDecoderPicture ** ref)
{
  GstVulkanVp9Picture *pic =
      (GstVulkanVp9Picture *) gst_vp9_picture_get_user_data (picture);

  /* *INDENT-OFF* */
  *res = (VkVideoPictureResourceInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
    .codedExtent = {
      .width = picture->frame_hdr.width,
      .height = picture->frame_hdr.height,
    },
    .baseArrayLayer = (self->decoder->layered_dpb && self->decoder->dedicated_dpb) ? pic->slot_idx : 0,
    .imageViewBinding = pic->base.img_view_ref->view,
  };

  *slot = (VkVideoReferenceSlotInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR,
    .pNext = NULL,
    .slotIndex = pic->slot_idx,
    .pPictureResource = res,
  };
  /* *INDENT-ON* */

  if (ref)
    *ref = &pic->base;

  GST_TRACE_OBJECT (self, "0x%" G_GUINT64_FORMAT "x slotIndex: %d",
      res->imageViewBinding, slot->slotIndex);
}

/**
 * _find_next_slot_idx:
 * @self: The VP9 decoder instance
 *
 * Finds the next available slot index in the DPB.
 *
 * Returns: Valid slot index (0-31) or -1 if no slots available
 */
static gint32
_find_next_slot_idx (GstVulkanVp9Decoder * self)
{
  gint32 i;

  g_return_val_if_fail (self != NULL, -1);
  g_return_val_if_fail (self->dpb_size > 0, -1);
  g_return_val_if_fail (self->dpb_size <= GST_VULKAN_VP9_MAX_DPB_SLOTS, -1);

  for (i = 0; i < self->dpb_size; i++) {
    if (!(self->free_slot_mask & (1 << i))) {
      // Mark as used.
      self->free_slot_mask |= (1 << i);
      return i;
    }
  }

  GST_ERROR_OBJECT (self,
      "Failed to find free DPB slot (dpb_size=%d, free_mask=0x%08x)",
      self->dpb_size, self->free_slot_mask);
  return -1;
}

static GstFlowReturn
gst_vulkan_vp9_decoder_decode_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture, GstVp9Dpb * dpb)
{

  GstVulkanVp9Decoder *self = GST_VULKAN_VP9_DECODER (decoder);
  GstVp9FrameHeader *fh = &picture->frame_hdr;
  GstVp9QuantizationParams *qp = &fh->quantization_params;
  GstVp9LoopFilterParams *lf = &fh->loop_filter_params;
  GstVp9SegmentationParams *seg = &fh->segmentation_params;
  GstVulkanVp9Picture *pic;
  guint num_refs = 0;
  guint i, j;
  gboolean intra_only;

  GST_TRACE_OBJECT (self, "Start picture %p", picture);

  pic = gst_vp9_picture_get_user_data (picture);

  /* *INDENT-OFF* */
  pic->loop_filter = (StdVideoVP9LoopFilter) {
    .flags = {
      .loop_filter_delta_enabled = lf->loop_filter_delta_enabled,
      .loop_filter_delta_update = lf->loop_filter_delta_update,
    },
    .loop_filter_level = lf->loop_filter_level,
    .loop_filter_sharpness = lf->loop_filter_sharpness,
    .update_ref_delta = 0,
  };
  /* *INDENT-ON* */

  for (i = 0; i < STD_VIDEO_VP9_MAX_REF_FRAMES; i++) {
    pic->loop_filter.loop_filter_ref_deltas[i] = lf->loop_filter_ref_deltas[i];
    pic->loop_filter.update_ref_delta |= lf->update_ref_delta[i] << i;
  }

  for (i = 0; i < STD_VIDEO_VP9_LOOP_FILTER_ADJUSTMENTS; i++) {
    pic->loop_filter.loop_filter_mode_deltas[i] =
        lf->loop_filter_mode_deltas[i];
    pic->loop_filter.update_mode_delta |= lf->update_mode_delta[i] << i;
  }

  /* *INDENT-OFF* */
  pic->segmentation = (StdVideoVP9Segmentation) {
    .flags = (StdVideoVP9SegmentationFlags) {
      .segmentation_update_map = seg->segmentation_update_map,
      .segmentation_temporal_update = seg->segmentation_temporal_update,
      .segmentation_update_data = seg->segmentation_update_data,
      .segmentation_abs_or_delta_update = seg->segmentation_abs_or_delta_update,
    },
  };
  /* *INDENT-N* */

  for (i = 0; i < GST_VP9_MAX_SEGMENTS; i++) {
    pic->segmentation.FeatureEnabled[i] = 0;
    for (j = 0; j < GST_VP9_SEG_LVL_MAX; j++) {
      pic->segmentation.FeatureEnabled[i] |= seg->feature_enabled[i][j] << j;
      pic->segmentation.FeatureData[i][j] = seg->feature_data[i][j];
    }
  }
  memcpy (pic->segmentation.segmentation_tree_probs,
      seg->segmentation_tree_probs, sizeof (seg->segmentation_tree_probs));
  memcpy (pic->segmentation.segmentation_pred_prob, seg->segmentation_pred_prob,
      sizeof (seg->segmentation_pred_prob));

  intra_only = (fh->frame_type == STD_VIDEO_VP9_FRAME_TYPE_KEY
      || fh->intra_only);

  /* *INDENT-OFF* */
  pic->std_pic = (StdVideoDecodeVP9PictureInfo) {
    .flags = (StdVideoDecodeVP9PictureInfoFlags) {
      .error_resilient_mode = fh->error_resilient_mode,
      .intra_only = fh->intra_only,
      .allow_high_precision_mv = fh->allow_high_precision_mv,
      .refresh_frame_context = fh->refresh_frame_context,
      .frame_parallel_decoding_mode = fh->frame_parallel_decoding_mode,
      .segmentation_enabled = seg->segmentation_enabled,
      .show_frame = fh->show_frame,
      .UsePrevFrameMvs =
        (self->last_show_frame
         && !intra_only
         && fh->error_resilient_mode == 0
         && !self->resolution_changed),
    },
    .profile = (StdVideoVP9Profile) fh->profile,
    .frame_type = (StdVideoVP9FrameType) fh->frame_type,
    .frame_context_idx = fh->frame_context_idx,
    .reset_frame_context = fh->reset_frame_context,
    .refresh_frame_flags = fh->refresh_frame_flags,
    .ref_frame_sign_bias_mask = 0,
    .interpolation_filter =
      (StdVideoVP9InterpolationFilter) fh->interpolation_filter,
    .base_q_idx = qp->base_q_idx,
    .delta_q_y_dc = qp->delta_q_y_dc,
    .delta_q_uv_dc = qp->delta_q_uv_dc,
    .delta_q_uv_ac = qp->delta_q_uv_ac,
    .tile_cols_log2 = fh->tile_cols_log2,
    .tile_rows_log2 = fh->tile_rows_log2,
    .pColorConfig = &self->vk.color_config,
    .pLoopFilter = &pic->loop_filter,
    .pSegmentation = seg->segmentation_enabled ? &pic->segmentation : NULL,
  };
  /* *INDENT-ON* */
  self->resolution_changed = FALSE;
  self->last_show_frame = fh->show_frame;

  for (i = 0; i < GST_VP9_REF_FRAME_MAX; i++) {
    pic->std_pic.ref_frame_sign_bias_mask |= (fh->ref_frame_sign_bias[i] << i);
  }

  /* *INDENT-OFF* */
  pic->vk_pic = (VkVideoDecodeVP9PictureInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_VP9_PICTURE_INFO_KHR,
    .pStdPictureInfo = &pic->std_pic,
    .uncompressedHeaderOffset = 0,
    .compressedHeaderOffset = fh->frame_header_length_in_bytes,
    .tilesOffset = fh->frame_header_length_in_bytes + fh->header_size_in_bytes,
  };
  /* *INDENT-ON* */

  for (i = 0; i < VK_MAX_VIDEO_VP9_REFERENCES_PER_FRAME_KHR; i++) {
    GstVp9Picture *ref_pic = dpb->pic_list[fh->ref_frame_idx[i]];
    if (ref_pic) {
      GstVulkanVp9Picture *ref_vk_pic =
          (GstVulkanVp9Picture *) gst_vp9_picture_get_user_data (ref_pic);

      pic->vk_pic.referenceNameSlotIndices[i] = ref_vk_pic->slot_idx;
    } else {
      pic->vk_pic.referenceNameSlotIndices[i] = -1;
    }
  }

  pic->slot_idx = _find_next_slot_idx (self);
  if (pic->slot_idx < 0) {
    GST_ERROR_OBJECT (self, "No free DPB slots available");
    return GST_FLOW_ERROR;
  }

  /* fill main slot */
  _fill_ref_slot (self, picture, &pic->base.slot, &pic->base.pic_res, NULL);

  for (i = 0; i < GST_VP9_REF_FRAME_MAX; i++) {
    GstVp9Picture *ref_pic = dpb->pic_list[i];
    gboolean found = FALSE;
    GstVulkanVp9Picture *ref_vk_pic;

    if (!ref_pic)
      continue;

    ref_vk_pic =
        (GstVulkanVp9Picture *) gst_vp9_picture_get_user_data (ref_pic);

    for (j = 0; j < num_refs; j++) {
      if (pic->base.slots[j].slotIndex == ref_vk_pic->slot_idx) {
        found = TRUE;
        break;
      }
    }

    if (!found) {
      _fill_ref_slot (self, ref_pic, &pic->base.slots[num_refs],
          &pic->base.pics_res[num_refs], &pic->base.refs[num_refs]);
      num_refs++;
    }

  }

  /* *INDENT-OFF* */
  pic->base.decode_info = (VkVideoDecodeInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR,
    .pNext = &pic->vk_pic,
    .flags = 0x0,
    .dstPictureResource = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
      .codedExtent = { picture->frame_hdr.width, picture->frame_hdr.height },
      .baseArrayLayer = 0,
      .imageViewBinding = pic->base.img_view_out->view,
    },
    .pSetupReferenceSlot = &pic->base.slot,
    .referenceSlotCount = num_refs,
    .pReferenceSlots = (const VkVideoReferenceSlotInfoKHR *) &pic->base.slots,
  };
  /* *INDENT-ON* */

  /* only wait if there's a buffer processed */
  if (GST_CODEC_PICTURE_FRAME_NUMBER (picture) > 0) {
    if (!gst_vulkan_decoder_wait (self->decoder)) {
      GST_ERROR_OBJECT (self, "Error at waiting for decoding operation to end");
      return GST_FLOW_ERROR;
    }
  }

  if (!gst_vulkan_decoder_append_slice (self->decoder, &pic->base,
          picture->data, picture->size, FALSE))
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vulkan_vp9_decoder_end_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture)
{
  GstVulkanVp9Decoder *self = GST_VULKAN_VP9_DECODER (decoder);
  GstVulkanVp9Picture *pic;
  GError *error = NULL;

  GST_TRACE_OBJECT (self, "End picture %p", picture);

  pic = (GstVulkanVp9Picture *) gst_vp9_picture_get_user_data (picture);
  g_assert (pic);

  if (pic->base.slice_offs->len == 0)
    return GST_FLOW_OK;

  GST_TRACE_OBJECT (self, "Decoding frame, %p", picture);

  if (!gst_vulkan_decoder_decode (self->decoder, &pic->base, &error)) {
    GST_ERROR_OBJECT (self, "Couldn't decode frame: %s",
        error ? error->message : "");
    g_clear_error (&error);
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vulkan_vp9_decoder_output_picture (GstVp9Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp9Picture * picture)
{
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstVulkanVp9Decoder *self = GST_VULKAN_VP9_DECODER (decoder);
  GstVideoCodecState *discont_state =
      GST_CODEC_PICTURE (picture)->discont_state;

  GST_TRACE_OBJECT (self, "Output picture %p", picture);

  if (discont_state) {
    g_clear_pointer (&self->input_state, gst_video_codec_state_unref);
    self->input_state = gst_video_codec_state_ref (discont_state);

    self->need_negotiation = TRUE;
    if (!gst_video_decoder_negotiate (vdec)) {
      gst_vp9_picture_unref (picture);
      GST_ERROR_OBJECT (self, "Could not re-negotiate with updated state");
      return GST_FLOW_ERROR;
    }
  }

  gst_vp9_picture_unref (picture);

  return gst_video_decoder_finish_frame (vdec, frame);
}

static GstVp9Picture *
gst_vulkan_vp9_decoder_duplicate_picture (GstVp9Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp9Picture * picture)
{
  GstVulkanVp9Decoder *self = GST_VULKAN_VP9_DECODER (decoder);
  GstVulkanVp9Picture *pic, *new_pic;
  GstVp9Picture *new_picture;

  if (_check_resolution_change (self, picture) != GST_FLOW_OK) {
    return NULL;
  }

  pic = (GstVulkanVp9Picture *) gst_vp9_picture_get_user_data (picture);
  if (!pic) {
    GST_ERROR_OBJECT (self, "Parent picture does not have a vulkan picture");
    return NULL;
  }

  new_picture = gst_vp9_picture_new ();
  new_picture->frame_hdr = picture->frame_hdr;
  new_pic = gst_vulkan_vp9_picture_new (self, pic->base.out);

  frame->output_buffer = gst_buffer_ref (new_pic->base.out);

  GST_LOG_OBJECT (self, "Duplicate output with buffer %" GST_PTR_FORMAT, pic);

  gst_vp9_picture_set_user_data (new_picture, new_pic,
      gst_vulkan_vp9_picture_free);

  return new_picture;
}

static void
gst_vulkan_vp9_decoder_init (GTypeInstance * instance, gpointer g_class)
{
  gst_vulkan_buffer_memory_init_once ();
}

static void
gst_vulkan_vp9_decoder_class_init (gpointer g_klass, gpointer class_data)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (g_klass);
  GstVp9DecoderClass *vp9decoder_class = GST_VP9_DECODER_CLASS (g_klass);
  GstVulkanVp9DecoderClass *vk_vp9_class =
      GST_VULKAN_VP9_DECODER_CLASS (g_klass);
  struct CData *cdata = class_data;
  gchar *long_name;
  const gchar *name;

  name = "Vulkan VP9 decoder";
  if (cdata->description)
    long_name = g_strdup_printf ("%s on %s", name, cdata->description);
  else
    long_name = g_strdup (name);

  vk_vp9_class->device_index = cdata->device_index;

  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Decoder/Video/Hardware", "A VP9 video decoder based on Vulkan",
      "Stephane Cerveau <scerveau@igalia.com>");

  parent_class = g_type_class_peek_parent (g_klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_vulkan_vp9dec_sink_template);

  gst_element_class_add_static_pad_template (element_class,
      &gst_vulkan_vp9dec_src_template);

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_vulkan_vp9_decoder_set_context);

  decoder_class->src_query =
      GST_DEBUG_FUNCPTR (gst_vulkan_vp9_decoder_src_query);
  decoder_class->sink_query =
      GST_DEBUG_FUNCPTR (gst_vulkan_vp9_decoder_sink_query);
  decoder_class->open = GST_DEBUG_FUNCPTR (gst_vulkan_vp9_decoder_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_vulkan_vp9_decoder_close);
  decoder_class->stop = GST_DEBUG_FUNCPTR (gst_vulkan_vp9_decoder_stop);
  decoder_class->negotiate =
      GST_DEBUG_FUNCPTR (gst_vulkan_vp9_decoder_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_vulkan_vp9_decoder_decide_allocation);

  vp9decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_vulkan_vp9_decoder_new_sequence);
  vp9decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_vulkan_vp9_decoder_new_picture);
  vp9decoder_class->decode_picture =
      GST_DEBUG_FUNCPTR (gst_vulkan_vp9_decoder_decode_picture);
  vp9decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_vulkan_vp9_decoder_end_picture);
  vp9decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_vulkan_vp9_decoder_output_picture);
  vp9decoder_class->duplicate_picture =
      GST_DEBUG_FUNCPTR (gst_vulkan_vp9_decoder_duplicate_picture);

  g_free (long_name);
  g_free (cdata->description);
  g_free (cdata);
}

gboolean
gst_vulkan_vp9_decoder_register (GstPlugin * plugin, GstVulkanDevice * device,
    guint rank)
{
  static GOnce debug_once = G_ONCE_INIT;
  GType type;
  GTypeInfo type_info = {
    .class_size = sizeof (GstVulkanVp9DecoderClass),
    .class_init = gst_vulkan_vp9_decoder_class_init,
    .instance_size = sizeof (GstVulkanVp9Decoder),
    .instance_init = gst_vulkan_vp9_decoder_init,
  };
  struct CData *cdata;
  gboolean ret;
  gchar *type_name, *feature_name;

  cdata = g_new (struct CData, 1);
  cdata->description = NULL;
  cdata->device_index = device->physical_device->device_index;

  g_return_val_if_fail (GST_IS_PLUGIN (plugin), FALSE);

  gst_vulkan_create_feature_name (device, "GstVulkanVp9Decoder",
      "GstVulkanVp9Device%dDecoder", &type_name, "vulkanvp9dec",
      "vulkanVp9device%ddec", &feature_name, &cdata->description, &rank);

  type_info.class_data = cdata;

  g_once (&debug_once, _register_debug_category, NULL);
  type = g_type_register_static (GST_TYPE_VP9_DECODER,
      type_name, &type_info, 0);

  ret = gst_element_register (plugin, feature_name, rank, type);

  g_free (type_name);
  g_free (feature_name);

  return ret;
}
