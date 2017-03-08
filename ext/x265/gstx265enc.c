/* GStreamer H265 encoder plugin
 * Copyright (C) 2005 Michal Benes <michal.benes@itonis.tv>
 * Copyright (C) 2005 Josef Zlomek <josef.zlomek@itonis.tv>
 * Copyright (C) 2008 Mark Nauwelaerts <mnauw@users.sf.net>
 * Copyright (C) 2014 Thijs Vermeir <thijs.vermeir@barco.com>
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
 * SECTION:element-x265enc
 * @title: x265enc
 *
 * This element encodes raw video into H265 compressed data.
 *
 **/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstx265enc.h"

#include <gst/pbutils/pbutils.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include <string.h>
#include <stdlib.h>

GST_DEBUG_CATEGORY_STATIC (x265_enc_debug);
#define GST_CAT_DEFAULT x265_enc_debug

enum
{
  PROP_0,
  PROP_BITRATE,
  PROP_QP,
  PROP_OPTION_STRING,
  PROP_X265_LOG_LEVEL,
  PROP_SPEED_PRESET,
  PROP_TUNE
};

#define PROP_BITRATE_DEFAULT            (2 * 1024)
#define PROP_QP_DEFAULT                 -1
#define PROP_OPTION_STRING_DEFAULT      ""
#define PROP_LOG_LEVEL_DEFAULT           -1     // None
#define PROP_SPEED_PRESET_DEFAULT        6      // Medium
#define PROP_TUNE_DEFAULT                2      // SSIM

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define FORMATS "I420, Y444, I420_10LE, Y444_10LE"
#else
#define FORMATS "I420, Y444, I420_10BE, Y444_10BE"
#endif

#define GST_X265_ENC_LOG_LEVEL_TYPE (gst_x265_enc_log_level_get_type())
static GType
gst_x265_enc_log_level_get_type (void)
{
  static GType log_level = 0;

  static const GEnumValue log_levels[] = {
    {X265_LOG_NONE, "No logging", "none"},
    {X265_LOG_ERROR, "Error", "error"},
    {X265_LOG_WARNING, "Warning", "warning"},
    {X265_LOG_INFO, "Info", "info"},
    {X265_LOG_DEBUG, "Debug", "debug"},
    {X265_LOG_FULL, "Full", "full"},
    {0, NULL, NULL}
  };

  if (!log_level) {
    log_level = g_enum_register_static ("GstX265LogLevel", log_levels);
  }
  return log_level;
}

#define GST_X265_ENC_SPEED_PRESET_TYPE (gst_x265_enc_speed_preset_get_type())
static GType
gst_x265_enc_speed_preset_get_type (void)
{
  static GType speed_preset = 0;
  static GEnumValue *speed_presets;
  int n, i;

  if (speed_preset != 0)
    return speed_preset;

  n = 0;
  while (x265_preset_names[n] != NULL)
    n++;

  speed_presets = g_new0 (GEnumValue, n + 2);

  speed_presets[0].value = 0;
  speed_presets[0].value_name = "No preset";
  speed_presets[0].value_nick = "No preset";

  for (i = 0; i < n; i++) {
    speed_presets[i + 1].value = i + 1;
    speed_presets[i + 1].value_name = x265_preset_names[i];
    speed_presets[i + 1].value_nick = x265_preset_names[i];
  }

  speed_preset = g_enum_register_static ("GstX265SpeedPreset", speed_presets);

  return speed_preset;
}

#define GST_X265_ENC_TUNE_TYPE (gst_x265_enc_tune_get_type())
static GType
gst_x265_enc_tune_get_type (void)
{
  static GType tune = 0;
  static GEnumValue *tune_values;
  int n, i;

  if (tune != 0)
    return tune;

  n = 0;
  while (x265_tune_names[n] != NULL)
    n++;

  tune_values = g_new0 (GEnumValue, n + 2);

  tune_values[0].value = 0;
  tune_values[0].value_name = "No tunning";
  tune_values[0].value_nick = "No tunning";

  for (i = 0; i < n; i++) {
    tune_values[i + 1].value = i + 1;
    tune_values[i + 1].value_name = x265_tune_names[i];
    tune_values[i + 1].value_nick = x265_tune_names[i];
  }

  tune = g_enum_register_static ("GstX265Tune", tune_values);

  return tune;
}

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) { " FORMATS " }, "
        "framerate = (fraction) [0, MAX], "
        "width = (int) [ 4, MAX ], " "height = (int) [ 4, MAX ]")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 4, MAX ], " "height = (int) [ 4, MAX ], "
        "stream-format = (string) byte-stream, "
        "alignment = (string) au, " "profile = (string) { main }")
    );

static void gst_x265_enc_finalize (GObject * object);
static gboolean gst_x265_enc_start (GstVideoEncoder * encoder);
static gboolean gst_x265_enc_stop (GstVideoEncoder * encoder);
static gboolean gst_x265_enc_flush (GstVideoEncoder * encoder);

static gboolean gst_x265_enc_init_encoder (GstX265Enc * encoder);
static void gst_x265_enc_close_encoder (GstX265Enc * encoder);

static GstFlowReturn gst_x265_enc_finish (GstVideoEncoder * encoder);
static GstFlowReturn gst_x265_enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static void gst_x265_enc_flush_frames (GstX265Enc * encoder, gboolean send);
static GstFlowReturn gst_x265_enc_encode_frame (GstX265Enc * encoder,
    x265_picture * pic_in, GstVideoCodecFrame * input_frame, guint32 * i_nal,
    gboolean send);
static gboolean gst_x265_enc_set_format (GstVideoEncoder * video_enc,
    GstVideoCodecState * state);
static gboolean gst_x265_enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);

static void gst_x265_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_x265_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define gst_x265_enc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstX265Enc, gst_x265_enc, GST_TYPE_VIDEO_ENCODER,
    G_IMPLEMENT_INTERFACE (GST_TYPE_PRESET, NULL));

static void
set_value (GValue * val, gint count, ...)
{
  const gchar *fmt = NULL;
  GValue sval = G_VALUE_INIT;
  va_list ap;
  gint i;

  g_value_init (&sval, G_TYPE_STRING);

  if (count > 1)
    g_value_init (val, GST_TYPE_LIST);

  va_start (ap, count);
  for (i = 0; i < count; i++) {
    fmt = va_arg (ap, const gchar *);
    g_value_set_string (&sval, fmt);
    if (count > 1) {
      gst_value_list_append_value (val, &sval);
    }
  }
  va_end (ap);

  if (count == 1)
    *val = sval;
  else
    g_value_unset (&sval);
}

static void
gst_x265_enc_add_x265_chroma_format (GstStructure * s,
    int x265_chroma_format_local)
{
  GValue fmt = G_VALUE_INIT;

  if (x265_max_bit_depth >= 10) {
    GST_INFO ("This x265 build supports %d-bit depth", x265_max_bit_depth);
    if (x265_chroma_format_local == 0) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      set_value (&fmt, 4, "I420", "Y444", "I420_10LE", "Y444_10LE");
#else
      set_value (&fmt, 4, "I420", "Y444", "I420_10BE", "Y444_10BE");
#endif
    } else if (x265_chroma_format_local == X265_CSP_I444) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      set_value (&fmt, 2, "Y444", "Y444_10LE");
#else
      set_value (&fmt, 2, "Y444", "Y444_10BE");
#endif
    } else if (x265_chroma_format_local == X265_CSP_I420) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      set_value (&fmt, 2, "I420", "I420_10LE");
#else
      set_value (&fmt, 2, "I420", "I420_10BE");
#endif
    } else {
      GST_ERROR ("Unsupported chroma format %d", x265_chroma_format_local);
    }
  } else if (x265_max_bit_depth == 8) {
    GST_INFO ("This x265 build supports 8-bit depth");
    if (x265_chroma_format_local == 0) {
      set_value (&fmt, 2, "I420", "Y444");
    } else if (x265_chroma_format_local == X265_CSP_I444) {
      set_value (&fmt, 1, "Y444");
    } else if (x265_chroma_format_local == X265_CSP_I420) {
      set_value (&fmt, 1, "I420");
    } else {
      GST_ERROR ("Unsupported chroma format %d", x265_chroma_format_local);
    }
  }

  if (G_VALUE_TYPE (&fmt) != G_TYPE_INVALID)
    gst_structure_take_value (s, "format", &fmt);
}

static GstCaps *
gst_x265_enc_get_supported_input_caps (void)
{
  GstCaps *caps;
  int x265_chroma_format = 0;

  caps = gst_caps_new_simple ("video/x-raw",
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
      "width", GST_TYPE_INT_RANGE, 4, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 4, G_MAXINT, NULL);

  gst_x265_enc_add_x265_chroma_format (gst_caps_get_structure (caps, 0),
      x265_chroma_format);

  GST_DEBUG ("returning %" GST_PTR_FORMAT, caps);
  return caps;
}

static gboolean
gst_x265_enc_sink_query (GstVideoEncoder * enc, GstQuery * query)
{
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ACCEPT_CAPS:{
      GstCaps *acceptable, *caps;

      acceptable = gst_x265_enc_get_supported_input_caps ();
      gst_query_parse_accept_caps (query, &caps);

      gst_query_set_accept_caps_result (query,
          gst_caps_is_subset (caps, acceptable));
      gst_caps_unref (acceptable);
      res = TRUE;
    }
      break;
    default:
      res = GST_VIDEO_ENCODER_CLASS (parent_class)->sink_query (enc, query);
      break;
  }

  return res;
}

static GstCaps *
gst_x265_enc_sink_getcaps (GstVideoEncoder * enc, GstCaps * filter)
{
  GstCaps *supported_incaps;
  GstCaps *ret;

  supported_incaps = gst_x265_enc_get_supported_input_caps ();

  ret = gst_video_encoder_proxy_getcaps (enc, supported_incaps, filter);
  if (supported_incaps)
    gst_caps_unref (supported_incaps);
  return ret;
}

static void
gst_x265_enc_class_init (GstX265EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstVideoEncoderClass *gstencoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  gstencoder_class = GST_VIDEO_ENCODER_CLASS (klass);

  gobject_class->set_property = gst_x265_enc_set_property;
  gobject_class->get_property = gst_x265_enc_get_property;
  gobject_class->finalize = gst_x265_enc_finalize;

  gstencoder_class->set_format = GST_DEBUG_FUNCPTR (gst_x265_enc_set_format);
  gstencoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_x265_enc_handle_frame);
  gstencoder_class->start = GST_DEBUG_FUNCPTR (gst_x265_enc_start);
  gstencoder_class->stop = GST_DEBUG_FUNCPTR (gst_x265_enc_stop);
  gstencoder_class->flush = GST_DEBUG_FUNCPTR (gst_x265_enc_flush);
  gstencoder_class->finish = GST_DEBUG_FUNCPTR (gst_x265_enc_finish);
  gstencoder_class->getcaps = GST_DEBUG_FUNCPTR (gst_x265_enc_sink_getcaps);
  gstencoder_class->sink_query = GST_DEBUG_FUNCPTR (gst_x265_enc_sink_query);
  gstencoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_x265_enc_propose_allocation);

  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate", "Bitrate in kbit/sec", 1,
          100 * 1024, PROP_BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));

  g_object_class_install_property (gobject_class, PROP_QP,
      g_param_spec_int ("qp", "Quantization parameter",
          "QP for P slices in (implied) CQP mode (-1 = disabled)", -1,
          51, PROP_QP_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_OPTION_STRING,
      g_param_spec_string ("option-string", "Option string",
          "String of x264 options (overridden by element properties)",
          PROP_OPTION_STRING_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_X265_LOG_LEVEL,
      g_param_spec_enum ("log-level", "(internal) x265 log level",
          "x265 log level", GST_X265_ENC_LOG_LEVEL_TYPE,
          PROP_LOG_LEVEL_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SPEED_PRESET,
      g_param_spec_enum ("speed-preset", "Speed preset",
          "Preset name for speed/quality tradeoff options",
          GST_X265_ENC_SPEED_PRESET_TYPE, PROP_SPEED_PRESET_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TUNE,
      g_param_spec_enum ("tune", "Tune options",
          "Preset name for tuning options", GST_X265_ENC_TUNE_TYPE,
          PROP_TUNE_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "x265enc", "Codec/Encoder/Video", "H265 Encoder",
      "Thijs Vermeir <thijs.vermeir@barco.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class, &src_factory);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_x265_enc_init (GstX265Enc * encoder)
{
  x265_param_default (&encoder->x265param);

  encoder->push_header = TRUE;

  encoder->bitrate = PROP_BITRATE_DEFAULT;
  encoder->qp = PROP_QP_DEFAULT;
  encoder->option_string_prop = g_string_new (PROP_OPTION_STRING_DEFAULT);
  encoder->log_level = PROP_LOG_LEVEL_DEFAULT;
  encoder->speed_preset = PROP_SPEED_PRESET_DEFAULT;
  encoder->tune = PROP_TUNE_DEFAULT;
}

typedef struct
{
  GstVideoCodecFrame *frame;
  GstVideoFrame vframe;
} FrameData;

static FrameData *
gst_x265_enc_queue_frame (GstX265Enc * enc, GstVideoCodecFrame * frame,
    GstVideoInfo * info)
{
  GstVideoFrame vframe;
  FrameData *fdata;

  if (!gst_video_frame_map (&vframe, info, frame->input_buffer, GST_MAP_READ))
    return NULL;

  fdata = g_slice_new (FrameData);
  fdata->frame = gst_video_codec_frame_ref (frame);
  fdata->vframe = vframe;

  enc->pending_frames = g_list_prepend (enc->pending_frames, fdata);

  return fdata;
}

static void
gst_x265_enc_dequeue_frame (GstX265Enc * enc, GstVideoCodecFrame * frame)
{
  GList *l;

  for (l = enc->pending_frames; l; l = l->next) {
    FrameData *fdata = l->data;

    if (fdata->frame != frame)
      continue;

    gst_video_frame_unmap (&fdata->vframe);
    gst_video_codec_frame_unref (fdata->frame);
    g_slice_free (FrameData, fdata);

    enc->pending_frames = g_list_delete_link (enc->pending_frames, l);
    return;
  }
}

static void
gst_x265_enc_dequeue_all_frames (GstX265Enc * enc)
{
  GList *l;

  for (l = enc->pending_frames; l; l = l->next) {
    FrameData *fdata = l->data;

    gst_video_frame_unmap (&fdata->vframe);
    gst_video_codec_frame_unref (fdata->frame);
    g_slice_free (FrameData, fdata);
  }
  g_list_free (enc->pending_frames);
  enc->pending_frames = NULL;
}

static gboolean
gst_x265_enc_start (GstVideoEncoder * encoder)
{
  //GstX265Enc *x265enc = GST_X265_ENC (encoder);

  return TRUE;
}

static gboolean
gst_x265_enc_stop (GstVideoEncoder * encoder)
{
  GstX265Enc *x265enc = GST_X265_ENC (encoder);

  GST_DEBUG_OBJECT (encoder, "stop encoder");

  gst_x265_enc_flush_frames (x265enc, FALSE);
  gst_x265_enc_close_encoder (x265enc);
  gst_x265_enc_dequeue_all_frames (x265enc);

  if (x265enc->input_state)
    gst_video_codec_state_unref (x265enc->input_state);
  x265enc->input_state = NULL;

  return TRUE;
}


static gboolean
gst_x265_enc_flush (GstVideoEncoder * encoder)
{
  GstX265Enc *x265enc = GST_X265_ENC (encoder);

  GST_DEBUG_OBJECT (encoder, "flushing encoder");

  gst_x265_enc_flush_frames (x265enc, FALSE);
  gst_x265_enc_close_encoder (x265enc);
  gst_x265_enc_dequeue_all_frames (x265enc);

  gst_x265_enc_init_encoder (x265enc);

  return TRUE;
}

static void
gst_x265_enc_finalize (GObject * object)
{
  GstX265Enc *encoder = GST_X265_ENC (object);

  if (encoder->input_state)
    gst_video_codec_state_unref (encoder->input_state);
  encoder->input_state = NULL;

  gst_x265_enc_close_encoder (encoder);

  g_string_free (encoder->option_string_prop, TRUE);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gint
gst_x265_enc_gst_to_x265_video_format (GstVideoFormat format, gint * nplanes)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_10BE:
      if (nplanes)
        *nplanes = 3;
      return X265_CSP_I420;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_Y444_10BE:
      if (nplanes)
        *nplanes = 3;
      return X265_CSP_I444;
    default:
      g_return_val_if_reached (GST_VIDEO_FORMAT_UNKNOWN);
  }
}

/*
 * gst_x265_enc_parse_options
 * @encoder: Encoder to which options are assigned
 * @str: Option string
 *
 * Parse option string and assign to x265 parameters
 *
 */
static gboolean
gst_x265_enc_parse_options (GstX265Enc * encoder, const gchar * str)
{
  GStrv kvpairs;
  guint npairs, i;
  gint parse_result = 0, ret = 0;
  gchar *options = (gchar *) str;

  while (*options == ':')
    options++;

  kvpairs = g_strsplit (options, ":", 0);
  npairs = g_strv_length (kvpairs);

  for (i = 0; i < npairs; i++) {
    GStrv key_val = g_strsplit (kvpairs[i], "=", 2);

    parse_result =
        x265_param_parse (&encoder->x265param, key_val[0], key_val[1]);

    if (parse_result == X265_PARAM_BAD_NAME) {
      GST_ERROR_OBJECT (encoder, "Bad name for option %s=%s",
          key_val[0] ? key_val[0] : "", key_val[1] ? key_val[1] : "");
    }
    if (parse_result == X265_PARAM_BAD_VALUE) {
      GST_ERROR_OBJECT (encoder,
          "Bad value for option %s=%s (Note: a NULL value for a non-boolean triggers this)",
          key_val[0] ? key_val[0] : "", key_val[1] ? key_val[1] : "");
    }

    g_strfreev (key_val);

    if (parse_result)
      ret++;
  }

  g_strfreev (kvpairs);
  return !ret;
}

/*
 * gst_x265_enc_init_encoder
 * @encoder:  Encoder which should be initialized.
 *
 * Initialize x265 encoder.
 *
 */
static gboolean
gst_x265_enc_init_encoder (GstX265Enc * encoder)
{
  GstVideoInfo *info;

  if (!encoder->input_state) {
    GST_DEBUG_OBJECT (encoder, "Have no input state yet");
    return FALSE;
  }

  info = &encoder->input_state->info;

  /* make sure that the encoder is closed */
  gst_x265_enc_close_encoder (encoder);

  GST_OBJECT_LOCK (encoder);

  if (x265_param_default_preset (&encoder->x265param,
          x265_preset_names[encoder->speed_preset - 1],
          x265_tune_names[encoder->tune - 1]) < 0) {
    GST_DEBUG_OBJECT (encoder, "preset or tune unrecognized");
    GST_OBJECT_UNLOCK (encoder);
    return FALSE;
  }

  /* set up encoder parameters */
  encoder->x265param.logLevel = encoder->log_level;
  encoder->x265param.internalCsp =
      gst_x265_enc_gst_to_x265_video_format (info->finfo->format, NULL);
  if (info->fps_d == 0 || info->fps_n == 0) {
  } else {
    encoder->x265param.fpsNum = info->fps_n;
    encoder->x265param.fpsDenom = info->fps_d;
  }
  encoder->x265param.sourceWidth = info->width;
  encoder->x265param.sourceHeight = info->height;
  if (info->par_d > 0) {
    encoder->x265param.vui.aspectRatioIdc = X265_EXTENDED_SAR;
    encoder->x265param.vui.sarWidth = info->par_n;
    encoder->x265param.vui.sarHeight = info->par_d;
  }

  if (encoder->qp != -1) {
    /* CQP */
    encoder->x265param.rc.qp = encoder->qp;
    encoder->x265param.rc.rateControlMode = X265_RC_CQP;
  } else {
    /* ABR */
    encoder->x265param.rc.bitrate = encoder->bitrate;
    encoder->x265param.rc.rateControlMode = X265_RC_ABR;
  }

  /* apply option-string property */
  if (encoder->option_string_prop && encoder->option_string_prop->len) {
    GST_DEBUG_OBJECT (encoder, "Applying option-string: %s",
        encoder->option_string_prop->str);
    if (gst_x265_enc_parse_options (encoder,
            encoder->option_string_prop->str) == FALSE) {
      GST_DEBUG_OBJECT (encoder, "Your option-string contains errors.");
      GST_OBJECT_UNLOCK (encoder);
      return FALSE;
    }
  }

  encoder->reconfig = FALSE;

  /* good start, will be corrected if needed */
  encoder->dts_offset = 0;

  GST_OBJECT_UNLOCK (encoder);

  encoder->x265enc = x265_encoder_open (&encoder->x265param);
  if (!encoder->x265enc) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE,
        ("Can not initialize x265 encoder."), (NULL));
    return FALSE;
  }

  encoder->push_header = TRUE;

  return TRUE;
}

/* gst_x265_enc_close_encoder
 * @encoder:  Encoder which should close.
 *
 * Close x265 encoder.
 */
static void
gst_x265_enc_close_encoder (GstX265Enc * encoder)
{
  if (encoder->x265enc != NULL) {
    x265_encoder_close (encoder->x265enc);
    encoder->x265enc = NULL;
  }
}

static x265_nal *
gst_x265_enc_bytestream_to_nal (x265_nal * input)
{
  x265_nal *output;
  int i, j, zeros;

  output = g_malloc (sizeof (x265_nal));
  output->payload = g_malloc (input->sizeBytes - 4);
  output->sizeBytes = input->sizeBytes - 4;
  output->type = input->type;

  zeros = 0;
  for (i = 4, j = 0; i < input->sizeBytes; (i++, j++)) {
    if (input->payload[i] == 0x00) {
      zeros++;
    } else if (input->payload[i] == 0x03 && zeros == 2) {
      zeros = 0;
      j--;
      output->sizeBytes--;
      continue;
    } else {
      zeros = 0;
    }
    output->payload[j] = input->payload[i];
  }

  return output;
}

static void
x265_nal_free (x265_nal * nal)
{
  g_free (nal->payload);
  g_free (nal);
}

static gboolean
gst_x265_enc_set_level_tier_and_profile (GstX265Enc * encoder, GstCaps * caps)
{
  x265_nal *nal, *vps_nal;
  guint32 i_nal;
  int header_return;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (encoder, "set profile, level and tier");

  header_return = x265_encoder_headers (encoder->x265enc, &nal, &i_nal);
  if (header_return < 0) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE, ("Encode x265 header failed."),
        ("x265_encoder_headers return code=%d", header_return));
    return FALSE;
  }

  GST_DEBUG_OBJECT (encoder, "%d nal units in header", i_nal);

  g_assert (nal[0].type == NAL_UNIT_VPS);
  vps_nal = gst_x265_enc_bytestream_to_nal (&nal[0]);

  GST_MEMDUMP ("VPS", vps_nal->payload, vps_nal->sizeBytes);

  if (!gst_codec_utils_h265_caps_set_level_tier_and_profile (caps,
          vps_nal->payload + 6, vps_nal->sizeBytes - 6)) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE, ("Encode x265 failed."),
        ("Failed to find correct level, tier or profile in VPS"));
    ret = FALSE;
  }

  x265_nal_free (vps_nal);

  return ret;
}

static GstBuffer *
gst_x265_enc_get_header_buffer (GstX265Enc * encoder)
{
  x265_nal *nal;
  guint32 i_nal, i, offset;
  gint32 vps_idx, sps_idx, pps_idx;
  int header_return;
  GstBuffer *buf;

  header_return = x265_encoder_headers (encoder->x265enc, &nal, &i_nal);
  if (header_return < 0) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE, ("Encode x265 header failed."),
        ("x265_encoder_headers return code=%d", header_return));
    return FALSE;
  }

  GST_DEBUG_OBJECT (encoder, "%d nal units in header", i_nal);

  /* x265 returns also non header nal units with the call x265_encoder_headers.
   * The usefull headers are sequential (VPS, SPS and PPS), so we look for this
   * nal units and only copy these tree nal units as the header */

  vps_idx = sps_idx = pps_idx = -1;
  for (i = 0; i < i_nal; i++) {
    if (nal[i].type == 32) {
      vps_idx = i;
    } else if (nal[i].type == 33) {
      sps_idx = i;
    } else if (nal[i].type == 34) {
      pps_idx = i;
    }
  }

  if (vps_idx == -1 || sps_idx == -1 || pps_idx == -1) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE, ("Encode x265 header failed."),
        ("x265_encoder_headers did not return VPS, SPS and PPS"));
    return FALSE;
  }

  offset = 0;
  buf =
      gst_buffer_new_allocate (NULL,
      nal[vps_idx].sizeBytes + nal[sps_idx].sizeBytes + nal[pps_idx].sizeBytes,
      NULL);
  gst_buffer_fill (buf, offset, nal[vps_idx].payload, nal[vps_idx].sizeBytes);
  offset += nal[vps_idx].sizeBytes;
  gst_buffer_fill (buf, offset, nal[sps_idx].payload, nal[sps_idx].sizeBytes);
  offset += nal[sps_idx].sizeBytes;
  gst_buffer_fill (buf, offset, nal[pps_idx].payload, nal[pps_idx].sizeBytes);

  return buf;
}

/* gst_x265_enc_set_src_caps
 * Returns: TRUE on success.
 */
static gboolean
gst_x265_enc_set_src_caps (GstX265Enc * encoder, GstCaps * caps)
{
  GstCaps *outcaps;
  GstStructure *structure;
  GstVideoCodecState *state;
  GstTagList *tags;

  outcaps = gst_caps_new_empty_simple ("video/x-h265");
  structure = gst_caps_get_structure (outcaps, 0);

  gst_structure_set (structure, "stream-format", G_TYPE_STRING, "byte-stream",
      NULL);
  gst_structure_set (structure, "alignment", G_TYPE_STRING, "au", NULL);

  if (!gst_x265_enc_set_level_tier_and_profile (encoder, outcaps)) {
    gst_caps_unref (outcaps);
    return FALSE;
  }

  state = gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (encoder),
      outcaps, encoder->input_state);
  GST_DEBUG_OBJECT (encoder, "output caps: %" GST_PTR_FORMAT, state->caps);
  gst_video_codec_state_unref (state);

  tags = gst_tag_list_new_empty ();
  gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_ENCODER, "x265",
      GST_TAG_ENCODER_VERSION, x265_version_str, NULL);
  gst_video_encoder_merge_tags (GST_VIDEO_ENCODER (encoder), tags,
      GST_TAG_MERGE_REPLACE);
  gst_tag_list_unref (tags);

  return TRUE;
}

static void
gst_x265_enc_set_latency (GstX265Enc * encoder)
{
  GstVideoInfo *info = &encoder->input_state->info;
  gint max_delayed_frames;
  GstClockTime latency;

  /* FIXME get a real value from the encoder, this is currently not exposed */
  if (encoder->tune > 0 && encoder->tune <= G_N_ELEMENTS (x265_tune_names) &&
      strcmp (x265_tune_names[encoder->tune - 1], "zerolatency") == 0)
    max_delayed_frames = 0;
  else
    max_delayed_frames = 5;

  if (info->fps_n) {
    latency = gst_util_uint64_scale_ceil (GST_SECOND * info->fps_d,
        max_delayed_frames, info->fps_n);
  } else {
    /* FIXME: Assume 25fps. This is better than reporting no latency at
     * all and then later failing in live pipelines
     */
    latency = gst_util_uint64_scale_ceil (GST_SECOND * 1,
        max_delayed_frames, 25);
  }

  GST_INFO_OBJECT (encoder,
      "Updating latency to %" GST_TIME_FORMAT " (%d frames)",
      GST_TIME_ARGS (latency), max_delayed_frames);

  gst_video_encoder_set_latency (GST_VIDEO_ENCODER (encoder), latency, latency);
}

static gboolean
gst_x265_enc_set_format (GstVideoEncoder * video_enc,
    GstVideoCodecState * state)
{
  GstX265Enc *encoder = GST_X265_ENC (video_enc);
  GstVideoInfo *info = &state->info;

  /* If the encoder is initialized, do not reinitialize it again if not
   * necessary */
  if (encoder->x265enc) {
    GstVideoInfo *old = &encoder->input_state->info;

    if (info->finfo->format == old->finfo->format
        && info->width == old->width && info->height == old->height
        && info->fps_n == old->fps_n && info->fps_d == old->fps_d
        && info->par_n == old->par_n && info->par_d == old->par_d) {
      gst_video_codec_state_unref (encoder->input_state);
      encoder->input_state = gst_video_codec_state_ref (state);
      return TRUE;
    }

    /* clear out pending frames */
    gst_x265_enc_flush_frames (encoder, TRUE);
  }

  if (encoder->input_state)
    gst_video_codec_state_unref (encoder->input_state);
  encoder->input_state = gst_video_codec_state_ref (state);

  if (!gst_x265_enc_init_encoder (encoder))
    return FALSE;

  if (!gst_x265_enc_set_src_caps (encoder, state->caps)) {
    gst_x265_enc_close_encoder (encoder);
    return FALSE;
  }

  gst_x265_enc_set_latency (encoder);

  return TRUE;
}

static GstFlowReturn
gst_x265_enc_finish (GstVideoEncoder * encoder)
{
  GST_DEBUG_OBJECT (encoder, "finish encoder");

  gst_x265_enc_flush_frames (GST_X265_ENC (encoder), TRUE);
  gst_x265_enc_flush_frames (GST_X265_ENC (encoder), TRUE);
  return GST_FLOW_OK;
}

static gboolean
gst_x265_enc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_x265_enc_handle_frame (GstVideoEncoder * video_enc,
    GstVideoCodecFrame * frame)
{
  GstX265Enc *encoder = GST_X265_ENC (video_enc);
  GstVideoInfo *info = &encoder->input_state->info;
  GstFlowReturn ret;
  x265_picture pic_in;
  guint32 i_nal, i;
  FrameData *fdata;
  gint nplanes = 0;

  if (G_UNLIKELY (encoder->x265enc == NULL))
    goto not_inited;

  /* set up input picture */
  x265_picture_init (&encoder->x265param, &pic_in);

  fdata = gst_x265_enc_queue_frame (encoder, frame, info);
  if (!fdata)
    goto invalid_frame;

  pic_in.colorSpace =
      gst_x265_enc_gst_to_x265_video_format (info->finfo->format, &nplanes);
  for (i = 0; i < nplanes; i++) {
    pic_in.planes[i] = GST_VIDEO_FRAME_PLANE_DATA (&fdata->vframe, i);
    pic_in.stride[i] = GST_VIDEO_FRAME_COMP_STRIDE (&fdata->vframe, i);
  }

  pic_in.sliceType = X265_TYPE_AUTO;
  pic_in.pts = frame->pts;
  pic_in.dts = frame->dts;
  pic_in.bitDepth = info->finfo->depth[0];
  pic_in.userData = GINT_TO_POINTER (frame->system_frame_number);

  ret = gst_x265_enc_encode_frame (encoder, &pic_in, frame, &i_nal, TRUE);

  /* input buffer is released later on */
  return ret;

/* ERRORS */
not_inited:
  {
    GST_WARNING_OBJECT (encoder, "Got buffer before set_caps was called");
    return GST_FLOW_NOT_NEGOTIATED;
  }
invalid_frame:
  {
    GST_ERROR_OBJECT (encoder, "Failed to map frame");
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_x265_enc_encode_frame (GstX265Enc * encoder, x265_picture * pic_in,
    GstVideoCodecFrame * input_frame, guint32 * i_nal, gboolean send)
{
  GstVideoCodecFrame *frame = NULL;
  GstBuffer *out_buf = NULL;
  x265_picture pic_out;
  x265_nal *nal;
  int i_size, i, offset;
  int encoder_return;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean update_latency = FALSE;

  if (G_UNLIKELY (encoder->x265enc == NULL)) {
    if (input_frame)
      gst_video_codec_frame_unref (input_frame);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  GST_OBJECT_LOCK (encoder);
  if (encoder->reconfig) {
    // x265_encoder_reconfig is not yet implemented thus we shut down and re-create encoder
    gst_x265_enc_init_encoder (encoder);
    update_latency = TRUE;
  }

  if (pic_in && input_frame) {
    if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (input_frame)) {
      GST_INFO_OBJECT (encoder, "Forcing key frame");
      pic_in->sliceType = X265_TYPE_IDR;
    }
  }
  GST_OBJECT_UNLOCK (encoder);

  if (G_UNLIKELY (update_latency))
    gst_x265_enc_set_latency (encoder);

  encoder_return = x265_encoder_encode (encoder->x265enc,
      &nal, i_nal, pic_in, &pic_out);

  GST_DEBUG_OBJECT (encoder, "encoder result (%d) with %u nal units",
      encoder_return, *i_nal);

  if (encoder_return < 0) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE, ("Encode x265 frame failed."),
        ("x265_encoder_encode return code=%d", encoder_return));
    ret = GST_FLOW_ERROR;
    /* Make sure we finish this frame */
    frame = input_frame;
    goto out;
  }

  /* Input frame is now queued */
  if (input_frame)
    gst_video_codec_frame_unref (input_frame);

  if (!*i_nal) {
    ret = GST_FLOW_OK;
    GST_LOG_OBJECT (encoder, "no output yet");
    goto out;
  }

  frame = gst_video_encoder_get_frame (GST_VIDEO_ENCODER (encoder),
      GPOINTER_TO_INT (pic_out.userData));
  g_assert (frame || !send);

  GST_DEBUG_OBJECT (encoder,
      "output picture ready POC=%d system=%d frame found %d", pic_out.poc,
      GPOINTER_TO_INT (pic_out.userData), frame != NULL);

  if (!send || !frame) {
    GST_LOG_OBJECT (encoder, "not sending (%d) or frame not found (%d)", send,
        frame != NULL);
    ret = GST_FLOW_OK;
    goto out;
  }

  i_size = 0;
  offset = 0;
  for (i = 0; i < *i_nal; i++)
    i_size += nal[i].sizeBytes;
  out_buf = gst_buffer_new_allocate (NULL, i_size, NULL);
  for (i = 0; i < *i_nal; i++) {
    gst_buffer_fill (out_buf, offset, nal[i].payload, nal[i].sizeBytes);
    offset += nal[i].sizeBytes;
  }

  frame->output_buffer = out_buf;

  if (encoder->push_header) {
    GstBuffer *header;

    header = gst_x265_enc_get_header_buffer (encoder);
    frame->output_buffer = gst_buffer_append (header, frame->output_buffer);
    encoder->push_header = FALSE;
  }

  GST_LOG_OBJECT (encoder,
      "output: dts %" G_GINT64_FORMAT " pts %" G_GINT64_FORMAT,
      (gint64) pic_out.dts, (gint64) pic_out.pts);

  frame->dts = pic_out.dts + encoder->dts_offset;

out:
  if (frame) {
    gst_x265_enc_dequeue_frame (encoder, frame);
    ret = gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (encoder), frame);
  }

  return ret;
}

static void
gst_x265_enc_flush_frames (GstX265Enc * encoder, gboolean send)
{
  GstFlowReturn flow_ret;
  guint32 i_nal;

  /* first send the remaining frames */
  if (encoder->x265enc)
    do {
      flow_ret = gst_x265_enc_encode_frame (encoder, NULL, NULL, &i_nal, send);
    } while (flow_ret == GST_FLOW_OK && i_nal > 0);
}

static void
gst_x265_enc_reconfig (GstX265Enc * encoder)
{
  encoder->x265param.rc.bitrate = encoder->bitrate;
  encoder->reconfig = TRUE;
}

static void
gst_x265_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstX265Enc *encoder;
  GstState state;

  encoder = GST_X265_ENC (object);

  GST_OBJECT_LOCK (encoder);

  state = GST_STATE (encoder);
  if ((state != GST_STATE_READY && state != GST_STATE_NULL) &&
      !(pspec->flags & GST_PARAM_MUTABLE_PLAYING))
    goto wrong_state;

  switch (prop_id) {
    case PROP_BITRATE:
      encoder->bitrate = g_value_get_uint (value);
      break;
    case PROP_QP:
      encoder->qp = g_value_get_int (value);
      break;
    case PROP_OPTION_STRING:
      g_string_assign (encoder->option_string_prop, g_value_get_string (value));
      break;
    case PROP_X265_LOG_LEVEL:
      encoder->log_level = g_value_get_enum (value);
      break;
    case PROP_SPEED_PRESET:
      encoder->speed_preset = g_value_get_enum (value);
      break;
    case PROP_TUNE:
      encoder->tune = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  gst_x265_enc_reconfig (encoder);
  GST_OBJECT_UNLOCK (encoder);
  return;

wrong_state:
  {
    GST_WARNING_OBJECT (encoder, "setting property in wrong state");
    GST_OBJECT_UNLOCK (encoder);
  }
}

static void
gst_x265_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstX265Enc *encoder;

  encoder = GST_X265_ENC (object);

  GST_OBJECT_LOCK (encoder);
  switch (prop_id) {
    case PROP_BITRATE:
      g_value_set_uint (value, encoder->bitrate);
      break;
    case PROP_QP:
      g_value_set_int (value, encoder->qp);
      break;
    case PROP_OPTION_STRING:
      g_value_set_string (value, encoder->option_string_prop->str);
      break;
    case PROP_X265_LOG_LEVEL:
      g_value_set_enum (value, encoder->log_level);
      break;
    case PROP_SPEED_PRESET:
      g_value_set_enum (value, encoder->speed_preset);
      break;
    case PROP_TUNE:
      g_value_set_enum (value, encoder->tune);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (encoder);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (x265_enc_debug, "x265enc", 0,
      "h265 encoding element");

  GST_INFO ("x265 build: %u", X265_BUILD);

  return gst_element_register (plugin, "x265enc",
      GST_RANK_PRIMARY, GST_TYPE_X265_ENC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    x265,
    "x265-based H265 plugins",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
