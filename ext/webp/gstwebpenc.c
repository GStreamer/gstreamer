/* GStreamer
 * Copyright (C) <2014> Sreerenj Balachandran <sreerenjb@gnome.org>
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>             /* free */

#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include "gstwebpenc.h"

#define GST_CAT_DEFAULT webpenc_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  PROP_0,
  PROP_LOSSLESS,
  PROP_QUALITY,
  PROP_SPEED,
  PROP_PRESET
};

#define DEFAULT_LOSSLESS FALSE
#define DEFAULT_QUALITY 90
#define DEFAULT_SPEED 4
#define DEFAULT_PRESET WEBP_PRESET_PHOTO

static void gst_webp_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_webp_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_webp_enc_start (GstVideoEncoder * benc);
static gboolean gst_webp_enc_stop (GstVideoEncoder * benc);
static gboolean gst_webp_enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_webp_enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static gboolean gst_webp_enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);

static GstStaticPadTemplate webp_enc_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ I420, YV12, RGB, RGBA}"))
    );
static GstStaticPadTemplate webp_enc_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/webp, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 16, 16383 ], " "height = (int) [ 16, 16383 ]")
    );

enum
{
  GST_WEBP_PRESET_DEFAULT,
  GST_WEBP_PRESET_PICTURE,
  GST_WEBP_PRESET_PHOTO,
  GST_WEBP_PRESET_DRAWING,
  GST_WEBP_PRESET_ICON,
  GST_WEBP_PREET_TEXT
};

static const GEnumValue preset_values[] = {
  {GST_WEBP_PRESET_DEFAULT, "Default", "none"},
  {GST_WEBP_PRESET_PICTURE, "Digital picture,inner shot", "picture"},
  {GST_WEBP_PRESET_PHOTO, "Outdoor photo, natural lighting", "photo"},
  {GST_WEBP_PRESET_DRAWING, "Hand or Line drawing", "drawing"},
  {GST_WEBP_PRESET_ICON, "Small-sized colorful images", "icon"},
  {GST_WEBP_PREET_TEXT, "text-like", "text"},
  {0, NULL, NULL},
};

#define GST_WEBP_ENC_PRESET_TYPE (gst_webp_enc_preset_get_type())
static GType
gst_webp_enc_preset_get_type (void)
{
  static GType preset_type = 0;

  if (!preset_type) {
    preset_type = g_enum_register_static ("GstWebpEncPreset", preset_values);
  }
  return preset_type;
}

#define gst_webp_enc_parent_class parent_class
G_DEFINE_TYPE (GstWebpEnc, gst_webp_enc, GST_TYPE_VIDEO_ENCODER);

static void
gst_webp_enc_class_init (GstWebpEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstVideoEncoderClass *venc_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  venc_class = (GstVideoEncoderClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_webp_enc_set_property;
  gobject_class->get_property = gst_webp_enc_get_property;
  gst_element_class_add_static_pad_template (element_class,
      &webp_enc_sink_factory);
  gst_element_class_add_static_pad_template (element_class,
      &webp_enc_src_factory);
  gst_element_class_set_static_metadata (element_class, "WEBP image encoder",
      "Codec/Encoder/Image", "Encode images in WEBP format",
      "Sreerenj Balachandran <sreerenjb@gnome.org>");

  venc_class->start = gst_webp_enc_start;
  venc_class->stop = gst_webp_enc_stop;
  venc_class->set_format = gst_webp_enc_set_format;
  venc_class->handle_frame = gst_webp_enc_handle_frame;
  venc_class->propose_allocation = gst_webp_enc_propose_allocation;

  g_object_class_install_property (gobject_class, PROP_LOSSLESS,
      g_param_spec_boolean ("lossless", "Lossless",
          "Enable lossless encoding",
          DEFAULT_LOSSLESS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_QUALITY,
      g_param_spec_float ("quality", "quality-level",
          "quality level, between 0 (smallest file) and 100 (biggest)",
          0, 100, DEFAULT_QUALITY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SPEED,
      g_param_spec_uint ("speed", "Compression Method",
          "quality/speed trade-off (0=fast, 6=slower-better)",
          0, 6, DEFAULT_SPEED, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PRESET,
      g_param_spec_enum ("preset", "preset tuning",
          "Preset name for visual tuning",
          GST_WEBP_ENC_PRESET_TYPE, DEFAULT_PRESET,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (webpenc_debug, "webpenc", 0,
      "WEBP encoding element");
}

static void
gst_webp_enc_init (GstWebpEnc * webpenc)
{
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_ENCODER_SINK_PAD (webpenc));

  webpenc->lossless = DEFAULT_LOSSLESS;
  webpenc->quality = DEFAULT_QUALITY;
  webpenc->speed = DEFAULT_SPEED;
  webpenc->preset = DEFAULT_PRESET;

  webpenc->use_argb = FALSE;
  webpenc->rgb_format = GST_VIDEO_FORMAT_UNKNOWN;
}

static gboolean
gst_webp_enc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  GstWebpEnc *enc = GST_WEBP_ENC (encoder);
  GstVideoCodecState *output_state;
  GstVideoInfo *info;
  GstVideoFormat format;

  info = &state->info;
  format = GST_VIDEO_INFO_FORMAT (info);

  if (GST_VIDEO_INFO_IS_YUV (info)) {
    switch (format) {
      case GST_VIDEO_FORMAT_I420:
      case GST_VIDEO_FORMAT_YV12:
        enc->webp_color_space = WEBP_YUV420;
        break;
      default:
        break;
    }
  } else {
    if (GST_VIDEO_INFO_IS_RGB (info)) {
      enc->rgb_format = format;
      enc->use_argb = 1;
    }
  }

  if (enc->input_state)
    gst_video_codec_state_unref (enc->input_state);
  enc->input_state = gst_video_codec_state_ref (state);

  output_state =
      gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (enc),
      gst_caps_new_empty_simple ("image/webp"), enc->input_state);
  gst_video_codec_state_unref (output_state);

  return TRUE;
}

static gboolean
gst_webp_set_picture_params (GstWebpEnc * enc)
{
  GstVideoInfo *info;
  gboolean ret = TRUE;

  info = &enc->input_state->info;

  if (!WebPPictureInit (&enc->webp_picture)) {
    ret = FALSE;
    goto failed_pic_init;
  }

  enc->webp_picture.use_argb = enc->use_argb;
  if (!enc->use_argb)
    enc->webp_picture.colorspace = enc->webp_color_space;

  enc->webp_picture.width = GST_VIDEO_INFO_WIDTH (info);
  enc->webp_picture.height = GST_VIDEO_INFO_HEIGHT (info);

  WebPMemoryWriterInit (&enc->webp_writer);
  enc->webp_picture.writer = WebPMemoryWrite;
  enc->webp_picture.custom_ptr = &enc->webp_writer;

  return ret;

failed_pic_init:
  {
    GST_ERROR_OBJECT (enc, "Failed to Initialize WebPPicture !");
    return ret;
  }
}

static GstFlowReturn
gst_webp_enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstWebpEnc *enc = GST_WEBP_ENC (encoder);
  GstBuffer *out_buffer = NULL;
  GstVideoFrame vframe;

  GST_LOG_OBJECT (enc, "got new frame");

  gst_webp_set_picture_params (enc);

  if (!gst_video_frame_map (&vframe, &enc->input_state->info,
          frame->input_buffer, GST_MAP_READ))
    return GST_FLOW_ERROR;

  if (!enc->use_argb) {
    enc->webp_picture.y = GST_VIDEO_FRAME_COMP_DATA (&vframe, 0);
    enc->webp_picture.u = GST_VIDEO_FRAME_COMP_DATA (&vframe, 1);
    enc->webp_picture.v = GST_VIDEO_FRAME_COMP_DATA (&vframe, 2);

    enc->webp_picture.y_stride = GST_VIDEO_FRAME_COMP_STRIDE (&vframe, 0);
    enc->webp_picture.uv_stride = GST_VIDEO_FRAME_COMP_STRIDE (&vframe, 1);

  } else {
    switch (enc->rgb_format) {
      case GST_VIDEO_FORMAT_RGB:
        WebPPictureImportRGB (&enc->webp_picture,
            GST_VIDEO_FRAME_COMP_DATA (&vframe, 0),
            GST_VIDEO_FRAME_COMP_STRIDE (&vframe, 0));
        break;
      case GST_VIDEO_FORMAT_RGBA:
        WebPPictureImportRGBA (&enc->webp_picture,
            GST_VIDEO_FRAME_COMP_DATA (&vframe, 0),
            GST_VIDEO_FRAME_COMP_STRIDE (&vframe, 0));
        break;
      default:
        break;
    }
  }

  if (WebPEncode (&enc->webp_config, &enc->webp_picture)) {
    WebPPictureFree (&enc->webp_picture);

    out_buffer = gst_buffer_new_allocate (NULL, enc->webp_writer.size, NULL);
    if (!out_buffer) {
      GST_ERROR_OBJECT (enc, "Failed to create output buffer");
      return GST_FLOW_ERROR;
    }
    gst_buffer_fill (out_buffer, 0, enc->webp_writer.mem,
        enc->webp_writer.size);
    free (enc->webp_writer.mem);
  } else {
    GST_ERROR_OBJECT (enc, "Failed to encode WebPPicture");
    return GST_FLOW_ERROR;
  }

  gst_video_frame_unmap (&vframe);
  frame->output_buffer = out_buffer;
  return gst_video_encoder_finish_frame (encoder, frame);
}

static gboolean
gst_webp_enc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  return
      GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
}

static void
gst_webp_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWebpEnc *webpenc = GST_WEBP_ENC (object);

  switch (prop_id) {
    case PROP_LOSSLESS:
      webpenc->lossless = g_value_get_boolean (value);
      break;
    case PROP_QUALITY:
      webpenc->quality = g_value_get_float (value);
      break;
    case PROP_SPEED:
      webpenc->speed = g_value_get_uint (value);
      break;
    case PROP_PRESET:
      webpenc->preset = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static void
gst_webp_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstWebpEnc *webpenc = GST_WEBP_ENC (object);

  switch (prop_id) {
    case PROP_LOSSLESS:
      g_value_set_boolean (value, webpenc->lossless);
      break;
    case PROP_QUALITY:
      g_value_set_float (value, webpenc->quality);
      break;
    case PROP_SPEED:
      g_value_set_uint (value, webpenc->speed);
      break;
    case PROP_PRESET:
      g_value_set_enum (value, webpenc->preset);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_webp_enc_start (GstVideoEncoder * benc)
{
  GstWebpEnc *enc = (GstWebpEnc *) benc;

  if (!WebPConfigPreset (&enc->webp_config, enc->preset, enc->quality)) {
    GST_ERROR_OBJECT (enc, "Failed to Initialize WebPConfig ");
    return FALSE;
  }

  enc->webp_config.lossless = enc->lossless;
  enc->webp_config.method = enc->speed;
  if (!WebPValidateConfig (&enc->webp_config)) {
    GST_ERROR_OBJECT (enc, "Failed to Validate the WebPConfig");
    return FALSE;
  }
  return TRUE;
}

static gboolean
gst_webp_enc_stop (GstVideoEncoder * benc)
{
  GstWebpEnc *enc = GST_WEBP_ENC (benc);
  if (enc->input_state)
    gst_video_codec_state_unref (enc->input_state);
  return TRUE;
}

gboolean
gst_webp_enc_register (GstPlugin * plugin)
{
  return gst_element_register (plugin, "webpenc",
      GST_RANK_PRIMARY, GST_TYPE_WEBP_ENC);
}
