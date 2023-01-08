/* VP9
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
 * Copyright (C) 2010 Entropy Wave Inc
 * Copyright (C) 2010-2013 Sebastian Dröge <slomo@circular-chaos.org>
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
 *
 */
/**
 * SECTION:element-vp9enc
 * @title: vp9enc
 * @see_also: vp9dec, webmmux, oggmux
 *
 * This element encodes raw video into a VP9 stream.
 * [VP9](http://www.webmproject.org) is a royalty-free video codec maintained by
 * [Google](http://www.google.com/). It's the successor of On2 VP3, which was
 * the base of the Theora video codec.
 *
 * To control the quality of the encoding, the #GstVPXEnc:target-bitrate,
 * #GstVPXEnc:min-quantizer, #GstVPXEnc:max-quantizer or #GstVPXEnc:cq-level
 * properties can be used. Which one is used depends on the mode selected by
 * the #GstVPXEnc:end-usage property.
 * See [Encoder Parameters](http://www.webmproject.org/docs/encoder-parameters/)
 * for explanation, examples for useful encoding parameters and more details
 * on the encoding parameters.
 *
 * ## Example pipeline
 * |[
 * gst-launch-1.0 -v videotestsrc num-buffers=1000 ! vp9enc ! webmmux ! filesink location=videotestsrc.webm
 * ]| This example pipeline will encode a test video source to VP9 muxed in an
 * WebM container.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_VP9_ENCODER

/* glib decided in 2.32 it would be a great idea to deprecated GValueArray without
 * providing an alternative
 *
 * See https://bugzilla.gnome.org/show_bug.cgi?id=667228
 * */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <gst/tag/tag.h>
#include <gst/video/video.h>
#include <string.h>

#include "gstvpxelements.h"
#include "gstvpxenums.h"
#include "gstvpx-enumtypes.h"
#include "gstvp8utils.h"
#include "gstvp9enc.h"

GST_DEBUG_CATEGORY_STATIC (gst_vp9enc_debug);
#define GST_CAT_DEFAULT gst_vp9enc_debug

#define DEFAULT_TILE_COLUMNS 6
#define DEFAULT_TILE_ROWS 0
#define DEFAULT_ROW_MT 0
#define DEFAULT_AQ_MODE GST_VPX_AQ_OFF
#define DEFAULT_FRAME_PARALLEL_DECODING TRUE

enum
{
  PROP_0,
  PROP_TILE_COLUMNS,
  PROP_TILE_ROWS,
  PROP_ROW_MT,
  PROP_AQ_MODE,
  PROP_FRAME_PARALLEL_DECODING,
};

#define GST_VP9_ENC_VIDEO_FORMATS_8BIT "I420, YV12, Y444"
#define GST_VP9_ENC_VIDEO_FORMATS_HIGHBIT \
    "I420_10LE, I420_12LE, I422_10LE, I422_12LE, Y444_10LE, Y444_12LE"

static GstStaticPadTemplate gst_vp9_enc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vp9, " "profile = (string) {0, 1, 2, 3}")
    );

#define parent_class gst_vp9_enc_parent_class
G_DEFINE_TYPE (GstVP9Enc, gst_vp9_enc, GST_TYPE_VPX_ENC);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (vp9enc, "vp9enc", GST_RANK_PRIMARY,
    gst_vp9_enc_get_type (), vpx_element_init (plugin));

static vpx_codec_iface_t *gst_vp9_enc_get_algo (GstVPXEnc * enc);
static gboolean gst_vp9_enc_enable_scaling (GstVPXEnc * enc);
static void gst_vp9_enc_set_image_format (GstVPXEnc * enc, vpx_image_t * image);
static GstCaps *gst_vp9_enc_get_new_simple_caps (GstVPXEnc * enc);
static void gst_vp9_enc_set_stream_info (GstVPXEnc * enc, GstCaps * caps,
    GstVideoInfo * info);
static void *gst_vp9_enc_process_frame_user_data (GstVPXEnc * enc,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_vp9_enc_handle_invisible_frame_buffer (GstVPXEnc * enc,
    void *user_data, GstBuffer * buffer);
static void gst_vp9_enc_set_frame_user_data (GstVPXEnc * enc,
    GstVideoCodecFrame * frame, vpx_image_t * image);
static void gst_vp9_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vp9_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_vp9_enc_configure_encoder (GstVPXEnc * encoder,
    GstVideoCodecState * state);

#define DEFAULT_BITS_PER_PIXEL 0.0289

static GstCaps *
gst_vp9_enc_get_sink_caps (void)
{
#define CAPS_8BIT GST_VIDEO_CAPS_MAKE ("{ " GST_VP9_ENC_VIDEO_FORMATS_8BIT " }")
#define CAPS_HIGHBIT GST_VIDEO_CAPS_MAKE ( "{ " GST_VP9_ENC_VIDEO_FORMATS_8BIT ", " \
    GST_VP9_ENC_VIDEO_FORMATS_HIGHBIT "}")

  return gst_caps_from_string ((vpx_codec_get_caps (gst_vp9_enc_get_algo (NULL))
          & VPX_CODEC_CAP_HIGHBITDEPTH) ? CAPS_HIGHBIT : CAPS_8BIT);
}

static void
gst_vp9_enc_class_init (GstVP9EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstVPXEncClass *vpx_encoder_class;
  GstCaps *caps;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  vpx_encoder_class = GST_VPX_ENC_CLASS (klass);

  gobject_class->set_property = gst_vp9_enc_set_property;
  gobject_class->get_property = gst_vp9_enc_get_property;

  /**
   * GstVP9Enc:tile-columns:
   *
   * Number of tile columns, log2
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_TILE_COLUMNS,
      g_param_spec_int ("tile-columns", "Tile Columns",
          "Number of tile columns, log2",
          0, 6, DEFAULT_TILE_COLUMNS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstVP9Enc:tile-rows:
   *
   * Number of tile rows, log2
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_TILE_ROWS,
      g_param_spec_int ("tile-rows", "Tile Rows",
          "Number of tile rows, log2",
          0, 2, DEFAULT_TILE_ROWS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstVP9Enc:row-mt:
   *
   * Whether each row should be encoded using multiple threads
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_ROW_MT,
      g_param_spec_boolean ("row-mt", "Row Multithreading",
          "Whether each row should be encoded using multiple threads",
          DEFAULT_ROW_MT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstVP9Enc:aq-mode:
   *
   * Adaptive Quantization Mode
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_AQ_MODE,
      g_param_spec_enum ("aq-mode", "Adaptive Quantization Mode",
          "Which adaptive quantization mode should be used",
          GST_TYPE_VPXAQ, DEFAULT_AQ_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  gst_type_mark_as_plugin_api (GST_TYPE_VPXAQ, 0);

  /**
   * GstVP9Enc:frame-parallel-decoding:
   *
   * Whether encoded bitstream should allow parallel processing of video frames in the decoder
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_FRAME_PARALLEL_DECODING,
      g_param_spec_boolean ("frame-parallel-decoding",
          "Frame Parallel Decoding",
          "Whether encoded bitstream should allow parallel processing of video frames in the decoder "
          "(default is on)", DEFAULT_FRAME_PARALLEL_DECODING,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_add_static_pad_template (element_class,
      &gst_vp9_enc_src_template);

  caps = gst_vp9_enc_get_sink_caps ();
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
  gst_clear_caps (&caps);

  gst_element_class_set_static_metadata (element_class,
      "On2 VP9 Encoder",
      "Codec/Encoder/Video",
      "Encode VP9 video streams", "David Schleef <ds@entropywave.com>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  vpx_encoder_class->get_algo = gst_vp9_enc_get_algo;
  vpx_encoder_class->enable_scaling = gst_vp9_enc_enable_scaling;
  vpx_encoder_class->set_image_format = gst_vp9_enc_set_image_format;
  vpx_encoder_class->get_new_vpx_caps = gst_vp9_enc_get_new_simple_caps;
  vpx_encoder_class->set_stream_info = gst_vp9_enc_set_stream_info;
  vpx_encoder_class->process_frame_user_data =
      gst_vp9_enc_process_frame_user_data;
  vpx_encoder_class->handle_invisible_frame_buffer =
      gst_vp9_enc_handle_invisible_frame_buffer;
  vpx_encoder_class->set_frame_user_data = gst_vp9_enc_set_frame_user_data;
  vpx_encoder_class->configure_encoder = gst_vp9_enc_configure_encoder;

  GST_DEBUG_CATEGORY_INIT (gst_vp9enc_debug, "vp9enc", 0, "VP9 Encoder");
}

static void
gst_vp9_enc_init (GstVP9Enc * gst_vp9_enc)
{
  vpx_codec_err_t status;
  GstVPXEnc *gst_vpx_enc = GST_VPX_ENC (gst_vp9_enc);
  GST_DEBUG_OBJECT (gst_vp9_enc, "gst_vp9_enc_init");
  status =
      vpx_codec_enc_config_default (gst_vp9_enc_get_algo (gst_vpx_enc),
      &gst_vpx_enc->cfg, 0);
  if (status != VPX_CODEC_OK) {
    GST_ERROR_OBJECT (gst_vpx_enc,
        "Failed to get default encoder configuration: %s",
        gst_vpx_error_name (status));
    gst_vpx_enc->have_default_config = FALSE;
  } else {
    gst_vpx_enc->have_default_config = TRUE;
  }
  gst_vpx_enc->bits_per_pixel = DEFAULT_BITS_PER_PIXEL;

  gst_vp9_enc->tile_columns = DEFAULT_TILE_COLUMNS;
  gst_vp9_enc->tile_rows = DEFAULT_TILE_ROWS;
  gst_vp9_enc->row_mt = DEFAULT_ROW_MT;
  gst_vp9_enc->aq_mode = DEFAULT_AQ_MODE;
  gst_vp9_enc->frame_parallel_decoding = DEFAULT_FRAME_PARALLEL_DECODING;
}

static void
gst_vp9_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVPXEnc *gst_vpx_enc = GST_VPX_ENC (object);
  GstVP9Enc *gst_vp9_enc = GST_VP9_ENC (object);
  vpx_codec_err_t status;

  g_mutex_lock (&gst_vpx_enc->encoder_lock);

  switch (prop_id) {
    case PROP_TILE_COLUMNS:
      gst_vp9_enc->tile_columns = g_value_get_int (value);
      if (gst_vpx_enc->inited) {
        status =
            vpx_codec_control (&gst_vpx_enc->encoder, VP9E_SET_TILE_COLUMNS,
            gst_vp9_enc->tile_columns);
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vpx_enc,
              "Failed to set VP9E_SET_TILE_COLUMNS: %s",
              gst_vpx_error_name (status));
        }
      }
      break;
    case PROP_TILE_ROWS:
      gst_vp9_enc->tile_rows = g_value_get_int (value);
      if (gst_vpx_enc->inited) {
        status =
            vpx_codec_control (&gst_vpx_enc->encoder, VP9E_SET_TILE_ROWS,
            gst_vp9_enc->tile_rows);
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vpx_enc,
              "Failed to set VP9E_SET_TILE_ROWS: %s",
              gst_vpx_error_name (status));
        }
      }
      break;
    case PROP_ROW_MT:
      gst_vp9_enc->row_mt = g_value_get_boolean (value);
      if (gst_vpx_enc->inited) {
        status =
            vpx_codec_control (&gst_vpx_enc->encoder, VP9E_SET_ROW_MT,
            gst_vp9_enc->row_mt ? 1 : 0);
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vpx_enc,
              "Failed to set VP9E_SET_ROW_MT: %s", gst_vpx_error_name (status));
        }
      }
      break;
    case PROP_AQ_MODE:
      gst_vp9_enc->aq_mode = g_value_get_enum (value);
      if (gst_vpx_enc->inited) {
        status = vpx_codec_control (&gst_vpx_enc->encoder, VP9E_SET_AQ_MODE,
            gst_vp9_enc->aq_mode);
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vpx_enc,
              "Failed to set VP9E_SET_AQ_MODE: %s",
              gst_vpx_error_name (status));
        }
      }
      break;
    case PROP_FRAME_PARALLEL_DECODING:
      gst_vp9_enc->frame_parallel_decoding = g_value_get_boolean (value);
      if (gst_vpx_enc->inited) {
        status = vpx_codec_control (&gst_vpx_enc->encoder,
            VP9E_SET_FRAME_PARALLEL_DECODING,
            gst_vp9_enc->frame_parallel_decoding ? 1 : 0);
        if (status != VPX_CODEC_OK) {
          GST_WARNING_OBJECT (gst_vpx_enc,
              "Failed to set VP9E_SET_FRAME_PARALLEL_DECODING: %s",
              gst_vpx_error_name (status));
        }
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  g_mutex_unlock (&gst_vpx_enc->encoder_lock);
}

static void
gst_vp9_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVPXEnc *gst_vpx_enc = GST_VPX_ENC (object);
  GstVP9Enc *gst_vp9_enc = GST_VP9_ENC (object);

  g_mutex_lock (&gst_vpx_enc->encoder_lock);

  switch (prop_id) {
    case PROP_TILE_COLUMNS:
      g_value_set_int (value, gst_vp9_enc->tile_columns);
      break;
    case PROP_TILE_ROWS:
      g_value_set_int (value, gst_vp9_enc->tile_rows);
      break;
    case PROP_ROW_MT:
      g_value_set_boolean (value, gst_vp9_enc->row_mt);
      break;
    case PROP_AQ_MODE:
      g_value_set_enum (value, gst_vp9_enc->aq_mode);
      break;
    case PROP_FRAME_PARALLEL_DECODING:
      g_value_set_boolean (value, gst_vp9_enc->frame_parallel_decoding);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  g_mutex_unlock (&gst_vpx_enc->encoder_lock);
}

static vpx_color_space_t
gst_vp9_get_vpx_colorspace (GstVPXEnc * encoder, GstVideoColorimetry * in_cinfo,
    GstVideoFormat format)
{
  vpx_color_space_t colorspace = VPX_CS_UNKNOWN;
  GstVideoColorimetry cinfo = *in_cinfo;
  gchar *colorimetry_str;
  guint i;

  /* *INDENT-OFF* */
  static const struct
  {
    const gchar *str;
    vpx_color_space_t vpx_color_space;
  } colorimetry_map[] = {
    {
    GST_VIDEO_COLORIMETRY_BT601, VPX_CS_BT_601}, {
    GST_VIDEO_COLORIMETRY_BT709, VPX_CS_BT_709}, {
    GST_VIDEO_COLORIMETRY_SMPTE240M, VPX_CS_SMPTE_240}, {
    GST_VIDEO_COLORIMETRY_BT2020, VPX_CS_BT_2020}
  };
  /* *INDENT-ON* */

  /* We support any range, all mapped CSC are by default reduced range. */
  cinfo.range = GST_VIDEO_COLOR_RANGE_16_235;
  colorimetry_str = gst_video_colorimetry_to_string (&cinfo);

  if (colorimetry_str != NULL) {
    for (i = 0; i < G_N_ELEMENTS (colorimetry_map); ++i) {
      if (g_strcmp0 (colorimetry_map[i].str, colorimetry_str) == 0) {
        colorspace = colorimetry_map[i].vpx_color_space;
        break;
      }
    }
  }

  if (colorspace == VPX_CS_UNKNOWN) {
    if (format == GST_VIDEO_FORMAT_GBR
        || format == GST_VIDEO_FORMAT_GBR_10BE
        || format == GST_VIDEO_FORMAT_GBR_10LE
        || format == GST_VIDEO_FORMAT_GBR_12BE
        || format == GST_VIDEO_FORMAT_GBR_12LE) {
      /* Currently has no effect because vp*enc elements only accept YUV video
       * formats.
       *
       * FIXME: Support encoding GST_VIDEO_FORMAT_GBR and its high bits variants.
       */
      colorspace = VPX_CS_SRGB;
    } else {
      GST_WARNING_OBJECT (encoder, "Unsupported colorspace \"%s\"",
          GST_STR_NULL (colorimetry_str));
    }
  }

  g_free (colorimetry_str);

  return colorspace;
}

static gint
gst_vp9_get_vpx_color_range (GstVideoColorimetry * colorimetry)
{
  if (colorimetry->range == GST_VIDEO_COLOR_RANGE_0_255)
    /* Full range (0..255 or HBD equivalent) */
    return 1;

  /* Limited range (16..235 or HBD equivalent) */
  return 0;
}

static gboolean
gst_vp9_enc_configure_encoder (GstVPXEnc * encoder, GstVideoCodecState * state)
{
  GstVP9Enc *vp9enc = GST_VP9_ENC (encoder);
  GstVideoInfo *info = &state->info;
  vpx_codec_err_t status;

  status = vpx_codec_control (&encoder->encoder, VP9E_SET_COLOR_SPACE,
      gst_vp9_get_vpx_colorspace (encoder, &GST_VIDEO_INFO_COLORIMETRY (info),
          GST_VIDEO_INFO_FORMAT (info)));
  if (status != VPX_CODEC_OK) {
    GST_WARNING_OBJECT (encoder,
        "Failed to set VP9E_SET_COLOR_SPACE: %s", gst_vpx_error_name (status));
  }

  status = vpx_codec_control (&encoder->encoder, VP9E_SET_COLOR_RANGE,
      gst_vp9_get_vpx_color_range (&GST_VIDEO_INFO_COLORIMETRY (info)));
  if (status != VPX_CODEC_OK) {
    GST_WARNING_OBJECT (encoder,
        "Failed to set VP9E_SET_COLOR_RANGE: %s", gst_vpx_error_name (status));
  }

  status =
      vpx_codec_control (&encoder->encoder, VP9E_SET_TILE_COLUMNS,
      vp9enc->tile_columns);
  if (status != VPX_CODEC_OK) {
    GST_DEBUG_OBJECT (encoder, "Failed to set VP9E_SET_TILE_COLUMNS: %s",
        gst_vpx_error_name (status));
  }

  status =
      vpx_codec_control (&encoder->encoder, VP9E_SET_TILE_ROWS,
      vp9enc->tile_rows);
  if (status != VPX_CODEC_OK) {
    GST_DEBUG_OBJECT (encoder, "Failed to set VP9E_SET_TILE_ROWS: %s",
        gst_vpx_error_name (status));
  }
  status =
      vpx_codec_control (&encoder->encoder, VP9E_SET_ROW_MT,
      vp9enc->row_mt ? 1 : 0);
  if (status != VPX_CODEC_OK) {
    GST_DEBUG_OBJECT (encoder,
        "Failed to set VP9E_SET_ROW_MT: %s", gst_vpx_error_name (status));
  }
  status =
      vpx_codec_control (&encoder->encoder, VP9E_SET_AQ_MODE, vp9enc->aq_mode);
  if (status != VPX_CODEC_OK) {
    GST_WARNING_OBJECT (encoder,
        "Failed to set VP9E_SET_AQ_MODE: %s", gst_vpx_error_name (status));
  }
  status =
      vpx_codec_control (&encoder->encoder, VP9E_SET_FRAME_PARALLEL_DECODING,
      vp9enc->frame_parallel_decoding ? 1 : 0);
  if (status != VPX_CODEC_OK) {
    GST_WARNING_OBJECT (encoder,
        "Failed to set VP9E_SET_FRAME_PARALLEL_DECODING: %s",
        gst_vpx_error_name (status));
  }

  return TRUE;
}

static vpx_codec_iface_t *
gst_vp9_enc_get_algo (GstVPXEnc * enc)
{
  return &vpx_codec_vp9_cx_algo;
}

static gboolean
gst_vp9_enc_enable_scaling (GstVPXEnc * enc)
{
  return FALSE;
}

static void
gst_vp9_enc_set_image_format (GstVPXEnc * enc, vpx_image_t * image)
{
  switch (enc->input_state->info.finfo->format) {
    case GST_VIDEO_FORMAT_I420:
      image->fmt = (vpx_img_fmt_t) GST_VPX_IMG_FMT_I420;
      image->bps = 12;
      image->bit_depth = 8;
      image->x_chroma_shift = image->y_chroma_shift = 1;
      break;
    case GST_VIDEO_FORMAT_YV12:
      image->fmt = (vpx_img_fmt_t) GST_VPX_IMG_FMT_YV12;
      image->bps = 12;
      image->bit_depth = 8;
      image->x_chroma_shift = image->y_chroma_shift = 1;
      break;
    case GST_VIDEO_FORMAT_Y42B:
      image->fmt = (vpx_img_fmt_t) GST_VPX_IMG_FMT_I422;
      image->bps = 16;
      image->bit_depth = 8;
      image->x_chroma_shift = 1;
      image->y_chroma_shift = 0;
      break;
    case GST_VIDEO_FORMAT_Y444:
      image->fmt = (vpx_img_fmt_t) GST_VPX_IMG_FMT_I444;
      image->bps = 24;
      image->bit_depth = 8;
      image->x_chroma_shift = image->y_chroma_shift = 0;
      break;
    case GST_VIDEO_FORMAT_I420_10LE:
      image->fmt = (vpx_img_fmt_t) GST_VPX_IMG_FMT_I42016;
      image->bps = 15;
      image->bit_depth = 10;
      image->x_chroma_shift = image->y_chroma_shift = 1;
      break;
    case GST_VIDEO_FORMAT_I420_12LE:
      image->fmt = (vpx_img_fmt_t) GST_VPX_IMG_FMT_I42016;
      image->bps = 18;
      image->bit_depth = 12;
      image->x_chroma_shift = image->y_chroma_shift = 1;
      break;
    case GST_VIDEO_FORMAT_I422_10LE:
      image->fmt = (vpx_img_fmt_t) GST_VPX_IMG_FMT_I42216;
      image->bps = 20;
      image->bit_depth = 10;
      image->x_chroma_shift = 1;
      image->y_chroma_shift = 0;
      break;
    case GST_VIDEO_FORMAT_I422_12LE:
      image->fmt = (vpx_img_fmt_t) GST_VPX_IMG_FMT_I42216;
      image->bps = 24;
      image->bit_depth = 12;
      image->x_chroma_shift = 1;
      image->y_chroma_shift = 0;
      break;
    case GST_VIDEO_FORMAT_Y444_10LE:
      image->fmt = (vpx_img_fmt_t) GST_VPX_IMG_FMT_I44416;
      image->bps = 30;
      image->bit_depth = 10;
      image->x_chroma_shift = image->y_chroma_shift = 0;
      break;
    case GST_VIDEO_FORMAT_Y444_12LE:
      image->fmt = (vpx_img_fmt_t) GST_VPX_IMG_FMT_I44416;
      image->bps = 36;
      image->bit_depth = 12;
      image->x_chroma_shift = image->y_chroma_shift = 0;
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

static GstCaps *
gst_vp9_enc_get_new_simple_caps (GstVPXEnc * enc)
{
  GstCaps *caps;
  gchar *profile_str = g_strdup_printf ("%d", enc->cfg.g_profile);
  caps = gst_caps_new_simple ("video/x-vp9",
      "profile", G_TYPE_STRING, profile_str, NULL);
  g_free (profile_str);
  return caps;
}

static void
gst_vp9_enc_set_stream_info (GstVPXEnc * enc, GstCaps * caps,
    GstVideoInfo * info)
{
  return;
}

static void *
gst_vp9_enc_process_frame_user_data (GstVPXEnc * enc,
    GstVideoCodecFrame * frame)
{
  return NULL;
}

static GstFlowReturn
gst_vp9_enc_handle_invisible_frame_buffer (GstVPXEnc * enc, void *user_data,
    GstBuffer * buffer)
{
  GstFlowReturn ret;
  g_mutex_unlock (&enc->encoder_lock);
  ret = gst_pad_push (GST_VIDEO_ENCODER_SRC_PAD (enc), buffer);
  g_mutex_lock (&enc->encoder_lock);
  return ret;
}

static void
gst_vp9_enc_user_data_free (vpx_image_t * image)
{
  g_free (image);
}

static void
gst_vp9_enc_set_frame_user_data (GstVPXEnc * enc, GstVideoCodecFrame * frame,
    vpx_image_t * image)
{
  gst_video_codec_frame_set_user_data (frame, image,
      (GDestroyNotify) gst_vp9_enc_user_data_free);
  return;
}

#endif /* HAVE_VP9_ENCODER */
