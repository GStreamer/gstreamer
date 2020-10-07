/* GStreamer
 *  Copyright (C) 2020 Intel Corporation
 *     Author: He Junyan <junyan.he@intel.com>
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
 * License along with this library; if not, write to the0
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvavp8dec.h"

#include <gst/codecs/gstvp8decoder.h>

#include <va/va_drmcommon.h>

#include "gstvaallocator.h"
#include "gstvacaps.h"
#include "gstvadecoder.h"
#include "gstvadevice.h"
#include "gstvadisplay_drm.h"
#include "gstvapool.h"
#include "gstvaprofile.h"
#include "gstvautils.h"
#include "gstvavideoformat.h"

GST_DEBUG_CATEGORY_STATIC (gst_va_vp8dec_debug);
#define GST_CAT_DEFAULT gst_va_vp8dec_debug

#define GST_VA_VP8_DEC(obj)           ((GstVaVp8Dec *) obj)
#define GST_VA_VP8_DEC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_FROM_INSTANCE (obj), GstVaVp8DecClass))
#define GST_VA_VP8_DEC_CLASS(klass)   ((GstVaVp8DecClass *) klass)

typedef struct _GstVaVp8Dec GstVaVp8Dec;
typedef struct _GstVaVp8DecClass GstVaVp8DecClass;

struct _GstVaVp8DecClass
{
  GstVp8DecoderClass parent_class;

  gchar *render_device_path;
};

struct _GstVaVp8Dec
{
  GstVp8Decoder parent;

  GstVaDisplay *display;
  GstVaDecoder *decoder;

  GstBufferPool *other_pool;

  GstFlowReturn last_ret;
  GstVideoCodecState *output_state;

  VAProfile profile;
  gint width;
  gint height;

  gboolean need_negotiation;
  guint rt_format;
  gboolean has_videometa;
  gboolean copy_frames;
};

struct CData
{
  gchar *render_device_path;
  gchar *description;
  GstCaps *sink_caps;
  GstCaps *src_caps;
};

static GstElementClass *parent_class = NULL;

/* *INDENT-OFF* */
static const gchar *src_caps_str = GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("memory:VAMemory",
            "{ NV12, P010_10LE }") " ;" GST_VIDEO_CAPS_MAKE ("{ NV12, P010_10LE }");
/* *INDENT-ON* */

static const gchar *sink_caps_str = "video/x-vp8";

static gboolean
gst_va_vp8_dec_open (GstVideoDecoder * decoder)
{
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);
  GstVaVp8DecClass *klass = GST_VA_VP8_DEC_GET_CLASS (decoder);

  if (!gst_va_ensure_element_data (decoder, klass->render_device_path,
          &self->display))
    return FALSE;

  if (!self->decoder)
    self->decoder = gst_va_decoder_new (self->display, VP8);

  return (self->decoder != NULL);
}

static gboolean
gst_va_vp8_dec_close (GstVideoDecoder * decoder)
{
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);

  gst_clear_object (&self->decoder);
  gst_clear_object (&self->display);

  return TRUE;
}

static GstCaps *
gst_va_vp8_dec_sink_getcaps (GstVideoDecoder * decoder, GstCaps * filter)
{
  GstCaps *sinkcaps, *caps = NULL, *tmp;
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);

  if (self->decoder)
    caps = gst_va_decoder_get_sinkpad_caps (self->decoder);

  if (caps) {
    sinkcaps = gst_caps_copy (caps);
    gst_caps_unref (caps);
    if (filter) {
      tmp = gst_caps_intersect_full (filter, sinkcaps,
          GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref (sinkcaps);
      caps = tmp;
    } else {
      caps = sinkcaps;
    }
    GST_LOG_OBJECT (self, "Returning caps %" GST_PTR_FORMAT, caps);
  } else if (!caps) {
    caps = gst_video_decoder_proxy_getcaps (decoder, NULL, filter);
  }

  return caps;
}

static gboolean
gst_va_vp8_dec_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:{
      return gst_va_handle_context_query (GST_ELEMENT_CAST (self), query,
          self->display);
    }

    case GST_QUERY_CAPS:{
      GstCaps *caps = NULL, *tmp, *filter = NULL;
      gboolean fixed_caps;

      gst_query_parse_caps (query, &filter);

      fixed_caps = GST_PAD_IS_FIXED_CAPS (GST_VIDEO_DECODER_SRC_PAD (decoder));

      if (!fixed_caps && self->decoder)
        caps = gst_va_decoder_get_srcpad_caps (self->decoder);
      if (caps) {
        if (filter) {
          tmp =
              gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
          gst_caps_unref (caps);
          caps = tmp;
        }

        GST_LOG_OBJECT (self, "Returning caps %" GST_PTR_FORMAT, caps);
        gst_query_set_caps_result (query, caps);
        gst_caps_unref (caps);
        ret = TRUE;
        break;
      }
      /* else jump to default */
    }
    default:
      ret = GST_VIDEO_DECODER_CLASS (parent_class)->src_query (decoder, query);
      break;
  }

  return ret;
}

static gboolean
gst_va_vp8_dec_sink_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);

  if (GST_QUERY_TYPE (query) == GST_QUERY_CONTEXT) {
    return gst_va_handle_context_query (GST_ELEMENT_CAST (self), query,
        self->display);
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->sink_query (decoder, query);
}

static gboolean
gst_va_vp8_dec_stop (GstVideoDecoder * decoder)
{
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);

  if (!gst_va_decoder_close (self->decoder))
    return FALSE;

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);
  self->output_state = NULL;

  if (self->other_pool)
    gst_buffer_pool_set_active (self->other_pool, FALSE);
  gst_clear_object (&self->other_pool);

  return GST_VIDEO_DECODER_CLASS (parent_class)->stop (decoder);
}

static GstVideoFormat
_default_video_format_from_chroma (guint chroma_type)
{
  switch (chroma_type) {
    case VA_RT_FORMAT_YUV420:
    case VA_RT_FORMAT_YUV422:
    case VA_RT_FORMAT_YUV444:
      return GST_VIDEO_FORMAT_NV12;
    case VA_RT_FORMAT_YUV420_10:
    case VA_RT_FORMAT_YUV422_10:
    case VA_RT_FORMAT_YUV444_10:
      return GST_VIDEO_FORMAT_P010_10LE;
    default:
      return GST_VIDEO_FORMAT_UNKNOWN;
  }
}

static void
_get_preferred_format_and_caps_features (GstVaVp8Dec * self,
    GstVideoFormat * format, GstCapsFeatures ** capsfeatures)
{
  GstCaps *peer_caps, *preferred_caps = NULL;
  GstCapsFeatures *features;
  GstStructure *structure;
  const GValue *v_format;
  guint num_structures, i;

  peer_caps = gst_pad_get_allowed_caps (GST_VIDEO_DECODER_SRC_PAD (self));
  GST_DEBUG_OBJECT (self, "Allowed caps %" GST_PTR_FORMAT, peer_caps);

  /* prefer memory:VASurface over other caps features */
  num_structures = gst_caps_get_size (peer_caps);
  for (i = 0; i < num_structures; i++) {
    features = gst_caps_get_features (peer_caps, i);
    structure = gst_caps_get_structure (peer_caps, i);

    if (gst_caps_features_is_any (features))
      continue;

    if (gst_caps_features_contains (features, "memory:VAMemory")) {
      preferred_caps = gst_caps_new_full (gst_structure_copy (structure), NULL);
      gst_caps_set_features_simple (preferred_caps,
          gst_caps_features_copy (features));
      break;
    }
  }

  if (!preferred_caps)
    preferred_caps = peer_caps;
  else
    gst_clear_caps (&peer_caps);

  if (gst_caps_is_empty (preferred_caps)
      || gst_caps_is_any (preferred_caps)) {
    /* if any or not linked yet then system memory and nv12 */
    if (capsfeatures)
      *capsfeatures = NULL;
    if (format)
      *format = _default_video_format_from_chroma (self->rt_format);
    goto bail;
  }

  features = gst_caps_get_features (preferred_caps, 0);
  if (features && capsfeatures)
    *capsfeatures = gst_caps_features_copy (features);

  if (!format)
    goto bail;

  structure = gst_caps_get_structure (preferred_caps, 0);
  v_format = gst_structure_get_value (structure, "format");
  if (!v_format)
    *format = _default_video_format_from_chroma (self->rt_format);
  else if (G_VALUE_HOLDS_STRING (v_format))
    *format = gst_video_format_from_string (g_value_get_string (v_format));
  else if (GST_VALUE_HOLDS_LIST (v_format)) {
    guint num_values = gst_value_list_get_size (v_format);
    for (i = 0; i < num_values; i++) {
      GstVideoFormat fmt;
      const GValue *v_fmt = gst_value_list_get_value (v_format, i);
      if (!v_fmt)
        continue;
      fmt = gst_video_format_from_string (g_value_get_string (v_fmt));
      if (gst_va_chroma_from_video_format (fmt) == self->rt_format) {
        *format = fmt;
        break;
      }
    }
    if (i == num_values)
      *format = _default_video_format_from_chroma (self->rt_format);
  }

bail:
  gst_clear_caps (&preferred_caps);
}

static gboolean
gst_va_vp8_dec_negotiate (GstVideoDecoder * decoder)
{
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;
  GstCapsFeatures *capsfeatures = NULL;
  GstVp8Decoder *vp8dec = GST_VP8_DECODER (decoder);

  /* Ignore downstream renegotiation request. */
  if (!self->need_negotiation)
    return TRUE;

  self->need_negotiation = FALSE;

  if (gst_va_decoder_is_open (self->decoder)
      && !gst_va_decoder_close (self->decoder))
    return FALSE;

  if (!gst_va_decoder_open (self->decoder, self->profile, self->rt_format))
    return FALSE;

  if (!gst_va_decoder_set_format (self->decoder, self->width, self->height,
          NULL))
    return FALSE;

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);

  _get_preferred_format_and_caps_features (self, &format, &capsfeatures);

  self->output_state =
      gst_video_decoder_set_output_state (decoder, format,
      self->width, self->height, vp8dec->input_state);

  self->output_state->caps = gst_video_info_to_caps (&self->output_state->info);
  if (capsfeatures)
    gst_caps_set_features_simple (self->output_state->caps, capsfeatures);

  GST_INFO_OBJECT (self, "Negotiated caps %" GST_PTR_FORMAT,
      self->output_state->caps);

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static GstAllocator *
_create_allocator (GstVaVp8Dec * self, GstCaps * caps)
{
  GstAllocator *allocator = NULL;

  if (gst_caps_is_dmabuf (caps)) {
    allocator = gst_va_dmabuf_allocator_new (self->display);
  } else {
    GArray *surface_formats =
        gst_va_decoder_get_surface_formats (self->decoder);
    allocator = gst_va_allocator_new (self->display, surface_formats);
  }

  return allocator;
}

/* 1. get allocator in query
 *    1.1 if allocator is not ours and downstream doesn't handle
 *        videometa, keep it for other_pool
 * 2. get pool in query
 *    2.1 if pool is not va, keep it as other_pool if downstream
 *        doesn't handle videometa or (it doesn't handle alignment and
 *        the stream needs cropping)
 *    2.2 if there's no pool in query and downstream doesn't handle
 *        videometa, create other_pool as GstVideoPool with the non-va
 *        from query and query's params
 * 3. create our allocator and pool if they aren't in query
 * 4. add or update pool and allocator in query
 * 5. set our custom pool configuration
 */
static gboolean
gst_va_vp8_dec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstAllocator *allocator = NULL, *other_allocator = NULL;
  GstAllocationParams other_params, params;
  GstBufferPool *pool = NULL;
  GstCaps *caps = NULL;
  GstStructure *config;
  GstVideoInfo info;
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);
  guint size = 0, min, max;
  gboolean update_pool = FALSE, update_allocator = FALSE;

  gst_query_parse_allocation (query, &caps, NULL);

  if (!(caps && gst_video_info_from_caps (&info, caps)))
    goto wrong_caps;

  self->has_videometa = gst_query_find_allocation_meta (query,
      GST_VIDEO_META_API_TYPE, NULL);

  if (gst_query_get_n_allocation_params (query) > 0) {
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &other_params);
    if (allocator && !(GST_IS_VA_DMABUF_ALLOCATOR (allocator)
            || GST_IS_VA_ALLOCATOR (allocator))) {
      /* save the allocator for the other pool */
      other_allocator = allocator;
      allocator = NULL;
    }
    update_allocator = TRUE;
  } else {
    gst_allocation_params_init (&other_params);
  }

  gst_allocation_params_init (&params);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool) {
      if (!GST_IS_VA_POOL (pool)) {
        if (!self->has_videometa) {
          GST_DEBUG_OBJECT (self,
              "keeping other pool for copy %" GST_PTR_FORMAT, pool);
          gst_object_replace ((GstObject **) & self->other_pool,
              (GstObject *) pool);
          gst_object_unref (pool);      /* decrease previous increase */
        }
        gst_clear_object (&pool);
      }
    }

    min = MAX (3 + 4, min);     /* max num pic references + scratch surfaces */
    size = MAX (size, GST_VIDEO_INFO_SIZE (&info));

    update_pool = TRUE;
  } else {
    size = GST_VIDEO_INFO_SIZE (&info);

    if (!self->has_videometa && !gst_caps_is_vamemory (caps)) {
      GST_DEBUG_OBJECT (self, "making new other pool for copy");
      self->other_pool = gst_video_buffer_pool_new ();
      config = gst_buffer_pool_get_config (self->other_pool);
      gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
      gst_buffer_pool_config_set_allocator (config, other_allocator,
          &other_params);
      if (!gst_buffer_pool_set_config (self->other_pool, config)) {
        GST_ERROR_OBJECT (self, "couldn't configure other pool for copy");
        gst_clear_object (&self->other_pool);
      }
    } else {
      gst_clear_object (&other_allocator);
    }

    min = 3 + 4;                /* max num pic references + scratch surfaces */
    max = 0;
  }

  if (!allocator) {
    if (!(allocator = _create_allocator (self, caps)))
      return FALSE;
  }

  if (!pool)
    pool = gst_va_pool_new ();

  {
    GstStructure *config = gst_buffer_pool_get_config (pool);

    gst_buffer_pool_config_set_params (config, caps, size, min, max);
    gst_buffer_pool_config_set_allocator (config, allocator, &params);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    gst_buffer_pool_config_set_va_allocation_params (config,
        VA_SURFACE_ATTRIB_USAGE_HINT_DECODER);

    if (!gst_buffer_pool_set_config (pool, config))
      return FALSE;
  }

  if (update_allocator)
    gst_query_set_nth_allocation_param (query, 0, allocator, &params);
  else
    gst_query_add_allocation_param (query, allocator, &params);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (allocator);
  gst_object_unref (pool);

  return GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation (decoder,
      query);

wrong_caps:
  {
    GST_WARNING_OBJECT (self, "No valid caps");
    return FALSE;
  }
}

static void
gst_va_vp8_dec_set_context (GstElement * element, GstContext * context)
{
  GstVaDisplay *old_display, *new_display;
  GstVaVp8Dec *self = GST_VA_VP8_DEC (element);
  GstVaVp8DecClass *klass = GST_VA_VP8_DEC_GET_CLASS (self);
  gboolean ret;

  old_display = self->display ? gst_object_ref (self->display) : NULL;
  ret = gst_va_handle_set_context (element, context, klass->render_device_path,
      &self->display);
  new_display = self->display ? gst_object_ref (self->display) : NULL;

  if (!ret
      || (old_display && new_display && old_display != new_display
          && self->decoder)) {
    GST_ELEMENT_WARNING (element, RESOURCE, BUSY,
        ("Can't replace VA display while operating"), (NULL));
  }

  gst_clear_object (&old_display);
  gst_clear_object (&new_display);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static VAProfile
_get_profile (GstVaVp8Dec * self, const GstVp8FrameHdr * frame_hdr)
{

  if (frame_hdr->version > 3) {
    GST_ERROR_OBJECT (self, "Unsupported vp8 version: %d", frame_hdr->version);
    return VAProfileNone;
  }

  return VAProfileVP8Version0_3;
}

static gboolean
gst_va_vp8_dec_new_sequence (GstVp8Decoder * decoder,
    const GstVp8FrameHdr * frame_hdr)
{
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);
  VAProfile profile;
  guint rt_format;
  gboolean negotiation_needed = FALSE;

  GST_LOG_OBJECT (self, "new sequence");

  profile = _get_profile (self, frame_hdr);
  if (profile == VAProfileNone)
    return FALSE;

  if (!gst_va_decoder_has_profile (self->decoder, profile)) {
    GST_ERROR_OBJECT (self, "Profile %s is not supported",
        gst_va_profile_name (profile));
    return FALSE;
  }

  /* VP8 always use 8 bits 4:2:0 */
  rt_format = VA_RT_FORMAT_YUV420;

  if (gst_va_decoder_format_changed (self->decoder, profile,
          rt_format, frame_hdr->width, frame_hdr->height)) {
    self->profile = profile;
    self->width = frame_hdr->width;
    self->height = frame_hdr->height;
    self->rt_format = rt_format;
    negotiation_needed = TRUE;
  }

  if (negotiation_needed) {
    self->need_negotiation = TRUE;
    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
      GST_ERROR_OBJECT (self, "Failed to negotiate with downstream");
      return FALSE;
    }
  }

  if (!self->has_videometa) {
    GstBufferPool *pool;

    pool = gst_video_decoder_get_buffer_pool (GST_VIDEO_DECODER (self));
    self->copy_frames = gst_va_pool_requires_video_meta (pool);
    gst_object_unref (pool);
  }

  return TRUE;
}

static gboolean
gst_va_vp8_dec_new_picture (GstVp8Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp8Picture * picture)
{
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);
  GstVaDecodePicture *pic;
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);

  self->last_ret = gst_video_decoder_allocate_output_frame (vdec, frame);
  if (self->last_ret != GST_FLOW_OK)
    goto error;

  pic = gst_va_decode_picture_new (frame->output_buffer);

  gst_vp8_picture_set_user_data (picture, pic,
      (GDestroyNotify) gst_va_decode_picture_free);

  GST_LOG_OBJECT (self, "New va decode picture %p - %#x", pic,
      gst_va_decode_picture_get_surface (pic));
  return TRUE;

error:
  {
    GST_WARNING_OBJECT (self,
        "Failed to allocated output buffer, return %s",
        gst_flow_get_name (self->last_ret));
    return FALSE;
  }
}

static gboolean
_fill_quant_matrix (GstVp8Decoder * decoder, GstVp8Picture * picture,
    GstVp8Parser * parser)
{
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);
  GstVp8FrameHdr const *frame_hdr = &picture->frame_hdr;
  GstVp8Segmentation *const seg = &parser->segmentation;
  VAIQMatrixBufferVP8 iq_matrix = { };
  const gint8 QI_MAX = 127;
  gint8 qi, qi_base;
  gint i;

  /* Fill in VAIQMatrixBufferVP8 */
  for (i = 0; i < 4; i++) {
    if (seg->segmentation_enabled) {
      qi_base = seg->quantizer_update_value[i];
      if (!seg->segment_feature_mode)   // 0 means delta update
        qi_base += frame_hdr->quant_indices.y_ac_qi;
    } else
      qi_base = frame_hdr->quant_indices.y_ac_qi;

    qi = qi_base;
    iq_matrix.quantization_index[i][0] = CLAMP (qi, 0, QI_MAX);
    qi = qi_base + frame_hdr->quant_indices.y_dc_delta;
    iq_matrix.quantization_index[i][1] = CLAMP (qi, 0, QI_MAX);
    qi = qi_base + frame_hdr->quant_indices.y2_dc_delta;
    iq_matrix.quantization_index[i][2] = CLAMP (qi, 0, QI_MAX);
    qi = qi_base + frame_hdr->quant_indices.y2_ac_delta;
    iq_matrix.quantization_index[i][3] = CLAMP (qi, 0, QI_MAX);
    qi = qi_base + frame_hdr->quant_indices.uv_dc_delta;
    iq_matrix.quantization_index[i][4] = CLAMP (qi, 0, QI_MAX);
    qi = qi_base + frame_hdr->quant_indices.uv_ac_delta;
    iq_matrix.quantization_index[i][5] = CLAMP (qi, 0, QI_MAX);
  }

  if (!gst_va_decoder_add_param_buffer (self->decoder,
          gst_vp8_picture_get_user_data (picture),
          VAIQMatrixBufferType, &iq_matrix, sizeof (iq_matrix))) {
    GST_WARNING ("fill Inverse Quantization Matrix Buffer error");
    return FALSE;
  }

  return TRUE;
}

static gboolean
_fill_probability_table (GstVp8Decoder * decoder, GstVp8Picture * picture)
{
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);
  GstVp8FrameHdr const *frame_hdr = &picture->frame_hdr;
  VAProbabilityDataBufferVP8 prob_table = { };

  /* Fill in VAProbabilityDataBufferVP8 */
  memcpy (prob_table.dct_coeff_probs, frame_hdr->token_probs.prob,
      sizeof (frame_hdr->token_probs.prob));

  if (!gst_va_decoder_add_param_buffer (self->decoder,
          gst_vp8_picture_get_user_data (picture),
          VAProbabilityBufferType, &prob_table, sizeof (prob_table))) {
    GST_WARNING ("fill Coefficient Probability Data Buffer error");
    return FALSE;
  }

  return TRUE;
}

static gboolean
_fill_picture (GstVp8Decoder * decoder, GstVp8Picture * picture,
    GstVp8Parser * parser)
{
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);
  GstVaDecodePicture *va_pic;
  VAPictureParameterBufferVP8 pic_param;
  GstVp8FrameHdr const *frame_hdr = &picture->frame_hdr;
  GstVp8Segmentation *const seg = &parser->segmentation;
  guint i;

  if (!_fill_quant_matrix (decoder, picture, parser))
    return FALSE;

  if (!_fill_probability_table (decoder, picture))
    return FALSE;

  /* *INDENT-OFF* */
  pic_param = (VAPictureParameterBufferVP8) {
    .frame_width = self->width,
    .frame_height = self->height,
    .last_ref_frame = VA_INVALID_SURFACE,
    .golden_ref_frame = VA_INVALID_SURFACE,
    .alt_ref_frame = VA_INVALID_SURFACE,
    .out_of_loop_frame = VA_INVALID_SURFACE, // not used currently
    .pic_fields.bits.key_frame = !frame_hdr->key_frame,
    .pic_fields.bits.version = frame_hdr->version,
    .pic_fields.bits.segmentation_enabled = seg->segmentation_enabled,
    .pic_fields.bits.update_mb_segmentation_map =
        seg->update_mb_segmentation_map,
    .pic_fields.bits.update_segment_feature_data =
        seg->update_segment_feature_data,
    .pic_fields.bits.filter_type = frame_hdr->filter_type,
    .pic_fields.bits.sharpness_level = frame_hdr->sharpness_level,
    .pic_fields.bits.loop_filter_adj_enable =
        parser->mb_lf_adjust.loop_filter_adj_enable,
    .pic_fields.bits.mode_ref_lf_delta_update =
        parser->mb_lf_adjust.mode_ref_lf_delta_update,
    .pic_fields.bits.sign_bias_golden = frame_hdr->sign_bias_golden,
    .pic_fields.bits.sign_bias_alternate = frame_hdr->sign_bias_alternate,
    .pic_fields.bits.mb_no_coeff_skip = frame_hdr->mb_no_skip_coeff,
    /* In decoding, the only loop filter settings that matter are those
       in the frame header (9.1) */
    .pic_fields.bits.loop_filter_disable = frame_hdr->loop_filter_level == 0,
    .prob_skip_false = frame_hdr->prob_skip_false,
    .prob_intra = frame_hdr->prob_intra,
    .prob_last = frame_hdr->prob_last,
    .prob_gf = frame_hdr->prob_gf,
    .bool_coder_ctx.range = frame_hdr->rd_range,
    .bool_coder_ctx.value = frame_hdr->rd_value,
    .bool_coder_ctx.count = frame_hdr->rd_count,
  };
  /* *INDENT-ON* */

  if (!frame_hdr->key_frame) {
    if (decoder->last_picture) {
      va_pic = gst_vp8_picture_get_user_data (decoder->last_picture);
      pic_param.last_ref_frame = gst_va_decode_picture_get_surface (va_pic);
    }
    if (decoder->golden_ref_picture) {
      va_pic = gst_vp8_picture_get_user_data (decoder->golden_ref_picture);
      pic_param.golden_ref_frame = gst_va_decode_picture_get_surface (va_pic);
    }
    if (decoder->alt_ref_picture) {
      va_pic = gst_vp8_picture_get_user_data (decoder->alt_ref_picture);
      pic_param.alt_ref_frame = gst_va_decode_picture_get_surface (va_pic);
    }
  }

  for (i = 0; i < 3; i++)
    pic_param.mb_segment_tree_probs[i] = seg->segment_prob[i];

  for (i = 0; i < 4; i++) {
    gint8 level;
    if (seg->segmentation_enabled) {
      level = seg->lf_update_value[i];
      /* 0 means delta update */
      if (!seg->segment_feature_mode)
        level += frame_hdr->loop_filter_level;
    } else
      level = frame_hdr->loop_filter_level;
    pic_param.loop_filter_level[i] = CLAMP (level, 0, 63);

    pic_param.loop_filter_deltas_ref_frame[i] =
        parser->mb_lf_adjust.ref_frame_delta[i];
    pic_param.loop_filter_deltas_mode[i] =
        parser->mb_lf_adjust.mb_mode_delta[i];
  }

  memcpy (pic_param.y_mode_probs, frame_hdr->mode_probs.y_prob,
      sizeof (frame_hdr->mode_probs.y_prob));
  memcpy (pic_param.uv_mode_probs, frame_hdr->mode_probs.uv_prob,
      sizeof (frame_hdr->mode_probs.uv_prob));
  memcpy (pic_param.mv_probs, frame_hdr->mv_probs.prob,
      sizeof (frame_hdr->mv_probs));

  va_pic = gst_vp8_picture_get_user_data (picture);
  if (!gst_va_decoder_add_param_buffer (self->decoder, va_pic,
          VAPictureParameterBufferType, &pic_param, sizeof (pic_param)))
    return FALSE;

  return TRUE;
}

static gboolean
_add_slice (GstVp8Decoder * decoder, GstVp8Picture * picture,
    GstVp8Parser * parser)
{
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);
  GstVp8FrameHdr const *frame_hdr = &picture->frame_hdr;
  VASliceParameterBufferVP8 slice_param;
  GstVaDecodePicture *va_pic;
  gint i;

  /* *INDENT-OFF* */
  slice_param = (VASliceParameterBufferVP8) {
    .slice_data_size = picture->size,
    .slice_data_offset = frame_hdr->data_chunk_size,
    .macroblock_offset = frame_hdr->header_size,
    .num_of_partitions = (1 << frame_hdr->log2_nbr_of_dct_partitions) + 1,
  };
  /* *INDENT-ON* */

  slice_param.partition_size[0] =
      frame_hdr->first_part_size - ((slice_param.macroblock_offset + 7) >> 3);
  for (i = 1; i < slice_param.num_of_partitions; i++)
    slice_param.partition_size[i] = frame_hdr->partition_size[i - 1];
  for (; i < G_N_ELEMENTS (slice_param.partition_size); i++)
    slice_param.partition_size[i] = 0;

  va_pic = gst_vp8_picture_get_user_data (picture);
  return gst_va_decoder_add_slice_buffer (self->decoder, va_pic, &slice_param,
      sizeof (slice_param), (gpointer) picture->data, picture->size);
}

static gboolean
gst_va_vp8_dec_decode_picture (GstVp8Decoder * decoder, GstVp8Picture * picture,
    GstVp8Parser * parser)
{
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);
  GstVaDecodePicture *va_pic;

  GST_TRACE_OBJECT (self, "-");

  if (!_fill_picture (decoder, picture, parser))
    goto error;

  if (!_add_slice (decoder, picture, parser))
    goto error;

  return TRUE;

error:
  {
    GST_WARNING_OBJECT (self, "Decode the picture error");
    va_pic = gst_vp8_picture_get_user_data (picture);
    gst_va_decoder_destroy_buffers (self->decoder, va_pic);
    return FALSE;
  }
}

static gboolean
gst_va_vp8_dec_end_picture (GstVp8Decoder * decoder, GstVp8Picture * picture)
{
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);
  GstVaDecodePicture *va_pic;

  GST_LOG_OBJECT (self, "end picture %p, (system_frame_number %d)",
      picture, picture->system_frame_number);

  va_pic = gst_vp8_picture_get_user_data (picture);

  return gst_va_decoder_decode (self->decoder, va_pic);
}

static gboolean
_copy_output_buffer (GstVaVp8Dec * self, GstVideoCodecFrame * codec_frame)
{
  GstVideoFrame src_frame;
  GstVideoFrame dest_frame;
  GstVideoInfo dest_vinfo;
  GstBuffer *buffer;
  GstFlowReturn ret;

  if (!self->other_pool)
    return FALSE;

  if (!gst_buffer_pool_set_active (self->other_pool, TRUE))
    return FALSE;

  gst_video_info_set_format (&dest_vinfo,
      GST_VIDEO_INFO_FORMAT (&self->output_state->info), self->width,
      self->height);

  ret = gst_buffer_pool_acquire_buffer (self->other_pool, &buffer, NULL);
  if (ret != GST_FLOW_OK)
    goto fail;

  if (!gst_video_frame_map (&src_frame, &self->output_state->info,
          codec_frame->output_buffer, GST_MAP_READ))
    goto fail;

  if (!gst_video_frame_map (&dest_frame, &dest_vinfo, buffer, GST_MAP_WRITE)) {
    gst_video_frame_unmap (&dest_frame);
    goto fail;
  }

  /* gst_video_frame_copy can crop this, but does not know, so let
   * make it think it's all right */
  GST_VIDEO_INFO_WIDTH (&src_frame.info) = self->width;
  GST_VIDEO_INFO_HEIGHT (&src_frame.info) = self->height;

  if (!gst_video_frame_copy (&dest_frame, &src_frame)) {
    gst_video_frame_unmap (&src_frame);
    gst_video_frame_unmap (&dest_frame);
    goto fail;
  }

  gst_video_frame_unmap (&src_frame);
  gst_video_frame_unmap (&dest_frame);
  gst_buffer_replace (&codec_frame->output_buffer, buffer);
  gst_buffer_unref (buffer);

  return TRUE;

fail:
  GST_ERROR_OBJECT (self, "Failed copy output buffer.");
  return FALSE;
}

static GstFlowReturn
gst_va_vp8_dec_output_picture (GstVp8Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp8Picture * picture)
{
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);

  GST_LOG_OBJECT (self,
      "Outputting picture %p (system_frame_number %d)",
      picture, picture->system_frame_number);

  if (self->last_ret != GST_FLOW_OK) {
    gst_vp8_picture_unref (picture);
    gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
    return self->last_ret;
  }

  if (self->copy_frames)
    _copy_output_buffer (self, frame);

  gst_vp8_picture_unref (picture);

  return gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
}

static void
gst_va_vp8_dec_init (GTypeInstance * instance, gpointer g_class)
{
}

static void
gst_va_vp8_dec_dispose (GObject * object)
{
  gst_va_vp8_dec_close (GST_VIDEO_DECODER (object));
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_va_vp8_dec_class_init (gpointer g_class, gpointer class_data)
{
  GstCaps *src_doc_caps, *sink_doc_caps;
  GstPadTemplate *sink_pad_templ, *src_pad_templ;
  GObjectClass *gobject_class = G_OBJECT_CLASS (g_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstVp8DecoderClass *vp8decoder_class = GST_VP8_DECODER_CLASS (g_class);
  GstVaVp8DecClass *klass = GST_VA_VP8_DEC_CLASS (g_class);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (g_class);
  struct CData *cdata = class_data;
  gchar *long_name;

  parent_class = g_type_class_peek_parent (g_class);

  klass->render_device_path = g_strdup (cdata->render_device_path);

  if (cdata->description) {
    long_name = g_strdup_printf ("VA-API VP8 Decoder in %s",
        cdata->description);
  } else {
    long_name = g_strdup ("VA-API VP8 Decoder");
  }

  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Decoder/Video/Hardware",
      "VA-API based VP8 video decoder", "He Junyan <junyan.he@intel.com>");

  sink_pad_templ = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      cdata->sink_caps);
  gst_element_class_add_pad_template (element_class, sink_pad_templ);
  sink_doc_caps = gst_caps_from_string (sink_caps_str);
  gst_pad_template_set_documentation_caps (sink_pad_templ, sink_doc_caps);
  gst_caps_unref (sink_doc_caps);

  src_pad_templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      cdata->src_caps);
  gst_element_class_add_pad_template (element_class, src_pad_templ);
  src_doc_caps = gst_caps_from_string (src_caps_str);
  gst_pad_template_set_documentation_caps (src_pad_templ, src_doc_caps);
  gst_caps_unref (src_doc_caps);

  gobject_class->dispose = gst_va_vp8_dec_dispose;

  element_class->set_context = GST_DEBUG_FUNCPTR (gst_va_vp8_dec_set_context);

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_va_vp8_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_va_vp8_dec_close);
  decoder_class->stop = GST_DEBUG_FUNCPTR (gst_va_vp8_dec_stop);
  decoder_class->src_query = GST_DEBUG_FUNCPTR (gst_va_vp8_dec_src_query);
  decoder_class->sink_query = GST_DEBUG_FUNCPTR (gst_va_vp8_dec_sink_query);
  decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_va_vp8_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_va_vp8_dec_decide_allocation);
  decoder_class->getcaps = GST_DEBUG_FUNCPTR (gst_va_vp8_dec_sink_getcaps);

  vp8decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_va_vp8_dec_new_sequence);
  vp8decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_va_vp8_dec_new_picture);
  vp8decoder_class->decode_picture =
      GST_DEBUG_FUNCPTR (gst_va_vp8_dec_decode_picture);
  vp8decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_va_vp8_dec_end_picture);
  vp8decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_va_vp8_dec_output_picture);

  g_free (long_name);
  g_free (cdata->description);
  g_free (cdata->render_device_path);
  gst_caps_unref (cdata->src_caps);
  gst_caps_unref (cdata->sink_caps);
  g_free (cdata);
}

static gpointer
_register_debug_category (gpointer data)
{
  GST_DEBUG_CATEGORY_INIT (gst_va_vp8dec_debug, "vavp8dec", 0,
      "VA VP8 decoder");

  return NULL;
}

gboolean
gst_va_vp8_dec_register (GstPlugin * plugin, GstVaDevice * device,
    GstCaps * sink_caps, GstCaps * src_caps, guint rank)
{
  static GOnce debug_once = G_ONCE_INIT;
  GType type;
  GTypeInfo type_info = {
    .class_size = sizeof (GstVaVp8DecClass),
    .class_init = gst_va_vp8_dec_class_init,
    .instance_size = sizeof (GstVaVp8Dec),
    .instance_init = gst_va_vp8_dec_init,
  };
  struct CData *cdata;
  gboolean ret;
  gchar *type_name, *feature_name;

  g_return_val_if_fail (GST_IS_PLUGIN (plugin), FALSE);
  g_return_val_if_fail (GST_IS_VA_DEVICE (device), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (sink_caps), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (src_caps), FALSE);

  cdata = g_new (struct CData, 1);
  cdata->description = NULL;
  cdata->render_device_path = g_strdup (device->render_device_path);
  cdata->sink_caps = gst_caps_ref (sink_caps);
  cdata->src_caps = gst_caps_ref (src_caps);

  /* class data will be leaked if the element never gets instantiated */
  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (src_caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  type_info.class_data = cdata;

  type_name = g_strdup ("GstVaVp8dec");
  feature_name = g_strdup ("vavp8dec");

  /* The first decoder to be registered should use a constant name,
   * like vavp8dec, for any additional decoders, we create unique
   * names, using inserting the render device name. */
  if (g_type_from_name (type_name)) {
    gchar *basename = g_path_get_basename (device->render_device_path);
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstVa%sVP8Dec", basename);
    feature_name = g_strdup_printf ("va%svp8dec", basename);
    cdata->description = basename;

    /* lower rank for non-first device */
    if (rank > 0)
      rank--;
  }

  g_once (&debug_once, _register_debug_category, NULL);

  type = g_type_register_static (GST_TYPE_VP8_DECODER,
      type_name, &type_info, 0);

  ret = gst_element_register (plugin, feature_name, rank, type);

  g_free (type_name);
  g_free (feature_name);

  return ret;
}
