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

/**
 * SECTION:element-vavp8dec
 * @title: vavp8dec
 * @short_description: A VA-API based VP8 video decoder
 *
 * vavp8dec decodes VP8 bitstreams to VA surfaces using the
 * installed and chosen [VA-API](https://01.org/linuxmedia/vaapi)
 * driver.
 *
 * The decoding surfaces can be mapped onto main memory as video
 * frames.
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 filesrc location=sample.webm ! parsebin ! vavp8dec ! autovideosink
 * ```
 *
 * Since: 1.20
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvavp8dec.h"

#include "gstvabasedec.h"

GST_DEBUG_CATEGORY_STATIC (gst_va_vp8dec_debug);
#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT gst_va_vp8dec_debug
#else
#define GST_CAT_DEFAULT NULL
#endif

#define GST_VA_VP8_DEC(obj)           ((GstVaVp8Dec *) obj)
#define GST_VA_VP8_DEC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_FROM_INSTANCE (obj), GstVaVp8DecClass))
#define GST_VA_VP8_DEC_CLASS(klass)   ((GstVaVp8DecClass *) klass)

typedef struct _GstVaVp8Dec GstVaVp8Dec;
typedef struct _GstVaVp8DecClass GstVaVp8DecClass;

struct _GstVaVp8DecClass
{
  GstVaBaseDecClass parent_class;
};

struct _GstVaVp8Dec
{
  GstVaBaseDec parent;

  GstFlowReturn last_ret;
};

static GstElementClass *parent_class = NULL;

/* *INDENT-OFF* */
static const gchar *src_caps_str =
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_VA,
        "{ NV12 }") " ;"
    GST_VIDEO_CAPS_MAKE ("{ NV12 }");
/* *INDENT-ON* */

static const gchar *sink_caps_str = "video/x-vp8";

static gboolean
gst_va_vp8_dec_negotiate (GstVideoDecoder * decoder)
{
  GstCapsFeatures *capsfeatures = NULL;
  GstVaBaseDec *base = GST_VA_BASE_DEC (decoder);
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;
  GstVp8Decoder *vp8dec = GST_VP8_DECODER (decoder);

  /* Ignore downstream renegotiation request. */
  if (!base->need_negotiation)
    return TRUE;

  base->need_negotiation = FALSE;

  if (gst_va_decoder_is_open (base->decoder)
      && !gst_va_decoder_close (base->decoder))
    return FALSE;

  if (!gst_va_decoder_open (base->decoder, base->profile, base->rt_format))
    return FALSE;

  if (!gst_va_decoder_set_frame_size (base->decoder, base->width, base->height))
    return FALSE;

  if (base->output_state)
    gst_video_codec_state_unref (base->output_state);

  gst_va_base_dec_get_preferred_format_and_caps_features (base, &format,
      &capsfeatures);

  base->output_state =
      gst_video_decoder_set_output_state (decoder, format,
      base->width, base->height, vp8dec->input_state);

  base->output_state->caps = gst_video_info_to_caps (&base->output_state->info);
  if (capsfeatures)
    gst_caps_set_features_simple (base->output_state->caps, capsfeatures);

  GST_INFO_OBJECT (self, "Negotiated caps %" GST_PTR_FORMAT,
      base->output_state->caps);

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
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

static GstFlowReturn
gst_va_vp8_dec_new_sequence (GstVp8Decoder * decoder,
    const GstVp8FrameHdr * frame_hdr)
{
  GstVaBaseDec *base = GST_VA_BASE_DEC (decoder);
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);
  VAProfile profile;
  guint rt_format;
  gboolean negotiation_needed = FALSE;

  GST_LOG_OBJECT (self, "new sequence");

  profile = _get_profile (self, frame_hdr);
  if (profile == VAProfileNone)
    return GST_FLOW_NOT_NEGOTIATED;

  if (!gst_va_decoder_has_profile (base->decoder, profile)) {
    GST_ERROR_OBJECT (self, "Profile %s is not supported",
        gst_va_profile_name (profile));
    return GST_FLOW_NOT_NEGOTIATED;
  }

  /* VP8 always use 8 bits 4:2:0 */
  rt_format = VA_RT_FORMAT_YUV420;

  if (!gst_va_decoder_config_is_equal (base->decoder, profile,
          rt_format, frame_hdr->width, frame_hdr->height)) {
    base->profile = profile;
    base->width = frame_hdr->width;
    base->height = frame_hdr->height;
    base->rt_format = rt_format;
    negotiation_needed = TRUE;
  }

  base->min_buffers = 3 + 4;    /* max num pic references + scratch surfaces */

  base->need_negotiation = negotiation_needed;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_va_vp8_dec_new_picture (GstVp8Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp8Picture * picture)
{
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);
  GstVaDecodePicture *pic;
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstVaBaseDec *base = GST_VA_BASE_DEC (decoder);

  if (base->need_negotiation) {
    if (!gst_video_decoder_negotiate (vdec)) {
      GST_ERROR_OBJECT (self, "Failed to negotiate with downstream");
      return GST_FLOW_NOT_NEGOTIATED;
    }
  }

  self->last_ret = gst_video_decoder_allocate_output_frame (vdec, frame);
  if (self->last_ret != GST_FLOW_OK)
    goto error;

  pic = gst_va_decode_picture_new (base->decoder, frame->output_buffer);

  gst_vp8_picture_set_user_data (picture, pic,
      (GDestroyNotify) gst_va_decode_picture_free);

  GST_LOG_OBJECT (self, "New va decode picture %p - %#x", pic,
      gst_va_decode_picture_get_surface (pic));

  return GST_FLOW_OK;

error:
  {
    GST_WARNING_OBJECT (self,
        "Failed to allocated output buffer, return %s",
        gst_flow_get_name (self->last_ret));
    return self->last_ret;
  }
}

static gboolean
_fill_quant_matrix (GstVp8Decoder * decoder, GstVp8Picture * picture,
    GstVp8Parser * parser)
{
  GstVaBaseDec *base = GST_VA_BASE_DEC (decoder);
  GstVp8FrameHdr const *frame_hdr = &picture->frame_hdr;
  GstVp8Segmentation *const seg = &parser->segmentation;
  VAIQMatrixBufferVP8 iq_matrix = { };
  const gint8 QI_MAX = 127;
  gint16 qi, qi_base;
  gint i;

  /* Fill in VAIQMatrixBufferVP8 */
  for (i = 0; i < 4; i++) {
    if (seg->segmentation_enabled) {
      qi_base = seg->quantizer_update_value[i];
      if (!seg->segment_feature_mode)   /* 0 means delta update */
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

  return gst_va_decoder_add_param_buffer (base->decoder,
      gst_vp8_picture_get_user_data (picture), VAIQMatrixBufferType, &iq_matrix,
      sizeof (iq_matrix));
}

static gboolean
_fill_probability_table (GstVp8Decoder * decoder, GstVp8Picture * picture)
{
  GstVaBaseDec *base = GST_VA_BASE_DEC (decoder);
  GstVp8FrameHdr const *frame_hdr = &picture->frame_hdr;
  VAProbabilityDataBufferVP8 prob_table = { };

  /* Fill in VAProbabilityDataBufferVP8 */
  memcpy (prob_table.dct_coeff_probs, frame_hdr->token_probs.prob,
      sizeof (frame_hdr->token_probs.prob));

  return gst_va_decoder_add_param_buffer (base->decoder,
      gst_vp8_picture_get_user_data (picture), VAProbabilityBufferType,
      &prob_table, sizeof (prob_table));
}

static gboolean
_fill_picture (GstVp8Decoder * decoder, GstVp8Picture * picture,
    GstVp8Parser * parser)
{
  GstVaBaseDec *base = GST_VA_BASE_DEC (decoder);
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
    .frame_width = base->width,
    .frame_height = base->height,
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
  return gst_va_decoder_add_param_buffer (base->decoder, va_pic,
      VAPictureParameterBufferType, &pic_param, sizeof (pic_param));
}

static gboolean
_add_slice (GstVp8Decoder * decoder, GstVp8Picture * picture,
    GstVp8Parser * parser)
{
  GstVaBaseDec *base = GST_VA_BASE_DEC (decoder);
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
  return gst_va_decoder_add_slice_buffer (base->decoder, va_pic, &slice_param,
      sizeof (slice_param), (gpointer) picture->data, picture->size);
}

static gboolean
gst_va_vp8_dec_decode_picture (GstVp8Decoder * decoder, GstVp8Picture * picture,
    GstVp8Parser * parser)
{
  if (_fill_picture (decoder, picture, parser) &&
      _add_slice (decoder, picture, parser))
    return GST_FLOW_OK;

  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_va_vp8_dec_end_picture (GstVp8Decoder * decoder, GstVp8Picture * picture)
{
  GstVaBaseDec *base = GST_VA_BASE_DEC (decoder);
  GstVaDecodePicture *va_pic;

  GST_LOG_OBJECT (base, "end picture %p, (system_frame_number %d)",
      picture, picture->system_frame_number);

  va_pic = gst_vp8_picture_get_user_data (picture);

  if (!gst_va_decoder_decode (base->decoder, va_pic))
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_va_vp8_dec_output_picture (GstVp8Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp8Picture * picture)
{
  GstVaBaseDec *base = GST_VA_BASE_DEC (decoder);
  GstVaVp8Dec *self = GST_VA_VP8_DEC (decoder);

  GST_LOG_OBJECT (self,
      "Outputting picture %p (system_frame_number %d)",
      picture, picture->system_frame_number);

  if (self->last_ret != GST_FLOW_OK) {
    gst_vp8_picture_unref (picture);
    gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
    return self->last_ret;
  }

  if (base->copy_frames)
    gst_va_base_dec_copy_output_buffer (base, frame);

  gst_vp8_picture_unref (picture);

  return gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
}

static void
gst_va_vp8_dec_init (GTypeInstance * instance, gpointer g_class)
{
  gst_va_base_dec_init (GST_VA_BASE_DEC (instance), GST_CAT_DEFAULT);
}

static void
gst_va_vp8_dec_dispose (GObject * object)
{
  gst_va_base_dec_close (GST_VIDEO_DECODER (object));
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_va_vp8_dec_class_init (gpointer g_class, gpointer class_data)
{
  GstCaps *src_doc_caps, *sink_doc_caps;
  GObjectClass *gobject_class = G_OBJECT_CLASS (g_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstVp8DecoderClass *vp8decoder_class = GST_VP8_DECODER_CLASS (g_class);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (g_class);
  struct CData *cdata = class_data;
  gchar *long_name;

  if (cdata->description) {
    long_name = g_strdup_printf ("VA-API VP8 Decoder in %s",
        cdata->description);
  } else {
    long_name = g_strdup ("VA-API VP8 Decoder");
  }

  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Decoder/Video/Hardware",
      "VA-API based VP8 video decoder", "He Junyan <junyan.he@intel.com>");

  sink_doc_caps = gst_caps_from_string (sink_caps_str);
  src_doc_caps = gst_caps_from_string (src_caps_str);

  parent_class = g_type_class_peek_parent (g_class);

  gst_va_base_dec_class_init (GST_VA_BASE_DEC_CLASS (g_class), VP8,
      cdata->render_device_path, cdata->sink_caps, cdata->src_caps,
      src_doc_caps, sink_doc_caps);

  gobject_class->dispose = gst_va_vp8_dec_dispose;

  decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_va_vp8_dec_negotiate);

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
