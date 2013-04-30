/* GStreamer H264 encoder plugin
 * Copyright (C) 2005 Michal Benes <michal.benes@itonis.tv>
 * Copyright (C) 2005 Josef Zlomek <josef.zlomek@itonis.tv>
 * Copyright (C) 2008 Mark Nauwelaerts <mnauw@users.sf.net>
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
 * SECTION:element-x264enc
 * @see_also: faac
 *
 * This element encodes raw video into H264 compressed data,
 * also otherwise known as MPEG-4 AVC (Advanced Video Codec).
 *
 * The #GstX264Enc:pass property controls the type of encoding.  In case of Constant
 * Bitrate Encoding (actually ABR), the #GstX264Enc:bitrate will determine the quality
 * of the encoding.  This will similarly be the case if this target bitrate
 * is to obtained in multiple (2 or 3) pass encoding.
 * Alternatively, one may choose to perform Constant Quantizer or Quality encoding,
 * in which case the #GstX264Enc:quantizer property controls much of the outcome, in that case #GstX264Enc:bitrate is the maximum bitrate.
 *
 * The H264 profile that is eventually used depends on a few settings.
 * If #GstX264Enc:dct8x8 is enabled, then High profile is used.
 * Otherwise, if #GstX264Enc:cabac entropy coding is enabled or #GstX264Enc:bframes
 * are allowed, then Main Profile is in effect, and otherwise Baseline profile
 * applies.  The main profile is imposed by default,
 * which is fine for most software players and settings,
 * but in some cases (e.g. hardware platforms) a more restricted profile/level
 * may be necessary. The recommended way to set a profile is to set it in the
 * downstream caps.
 *
 * If a preset/tuning are specified then these will define the default values and
 * the property defaults will be ignored. After this the option-string property is
 * applied, followed by the user-set properties, fast first pass restrictions and
 * finally the profile restrictions.
 *
 * <note>Some settings, including the default settings, may lead to quite
 * some latency (i.e. frame buffering) in the encoder. This may cause problems
 * with pipeline stalling in non-trivial pipelines, because the encoder latency
 * is often considerably higher than the default size of a simple queue
 * element. Such problems are caused by one of the queues in the other
 * non-x264enc streams/branches filling up and blocking upstream. They can
 * be fixed by relaxing the default time/size/buffer limits on the queue
 * elements in the non-x264 branches, or using a (single) multiqueue element
 * for all branches. Also see the last example below.
 * </note>
 *
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch -v videotestsrc num-buffers=1000 ! x264enc qp-min=18 ! \
 *   avimux ! filesink location=videotestsrc.avi
 * ]| This example pipeline will encode a test video source to H264 muxed in an
 * AVI container, while ensuring a sane minimum quantization factor to avoid
 * some (excessive) waste.
 * |[
 * gst-launch -v videotestsrc num-buffers=1000 ! x264enc pass=quant ! \
 *   matroskamux ! filesink location=videotestsrc.avi
 * ]| This example pipeline will encode a test video source to H264 using fixed
 * quantization, and muxes it in a Matroska container.
 * |[
 * gst-launch -v videotestsrc num-buffers=1000 ! x264enc pass=5 quantizer=25 speed-preset=6 ! video/x-h264, profile=baseline ! \
 *   qtmux ! filesink location=videotestsrc.mov
 * ]| This example pipeline will encode a test video source to H264 using
 * constant quality at around Q25 using the 'medium' speed/quality preset and
 * restricting the options used so that the output is H.264 Baseline Profile
 * compliant and finally multiplexing the output in Quicktime mov format.
 * |[
 * gst-launch -v videotestsrc num-buffers=1000 ! tee name=t ! queue ! xvimagesink \
 *   t. ! queue ! x264enc rc-lookahead=5 ! fakesink
 * ]| This example pipeline will encode a test video source to H264 while
 * displaying the input material at the same time.  As mentioned above,
 * specific settings are needed in this case to avoid pipeline stalling.
 * Depending on goals and context, other approaches are possible, e.g.
 * tune=zerolatency might be configured, or queue sizes increased.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstx264enc.h"

#include <gst/pbutils/pbutils.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include <string.h>
#include <stdlib.h>

GST_DEBUG_CATEGORY_STATIC (x264_enc_debug);
#define GST_CAT_DEFAULT x264_enc_debug

enum
{
  ARG_0,
  ARG_THREADS,
  ARG_SLICED_THREADS,
  ARG_SYNC_LOOKAHEAD,
  ARG_PASS,
  ARG_QUANTIZER,
  ARG_MULTIPASS_CACHE_FILE,
  ARG_BYTE_STREAM,
  ARG_BITRATE,
  ARG_INTRA_REFRESH,
  ARG_VBV_BUF_CAPACITY,
  ARG_ME,
  ARG_SUBME,
  ARG_ANALYSE,
  ARG_DCT8x8,
  ARG_REF,
  ARG_BFRAMES,
  ARG_B_ADAPT,
  ARG_B_PYRAMID,
  ARG_WEIGHTB,
  ARG_SPS_ID,
  ARG_AU_NALU,
  ARG_TRELLIS,
  ARG_KEYINT_MAX,
  ARG_CABAC,
  ARG_QP_MIN,
  ARG_QP_MAX,
  ARG_QP_STEP,
  ARG_IP_FACTOR,
  ARG_PB_FACTOR,
  ARG_RC_MB_TREE,
  ARG_RC_LOOKAHEAD,
  ARG_NR,
  ARG_INTERLACED,
  ARG_OPTION_STRING,
  ARG_SPEED_PRESET,
  ARG_PSY_TUNE,
  ARG_TUNE,
};

#define ARG_THREADS_DEFAULT            0        /* 0 means 'auto' which is 1.5x number of CPU cores */
#define ARG_PASS_DEFAULT               0
#define ARG_QUANTIZER_DEFAULT          21
#define ARG_MULTIPASS_CACHE_FILE_DEFAULT "x264.log"
#define ARG_BYTE_STREAM_DEFAULT        FALSE
#define ARG_BITRATE_DEFAULT            (2 * 1024)
#define ARG_VBV_BUF_CAPACITY_DEFAULT   600
#define ARG_ME_DEFAULT                 X264_ME_HEX
#define ARG_SUBME_DEFAULT              1
#define ARG_ANALYSE_DEFAULT            0
#define ARG_DCT8x8_DEFAULT             FALSE
#define ARG_REF_DEFAULT                1
#define ARG_BFRAMES_DEFAULT            0
#define ARG_B_ADAPT_DEFAULT            TRUE
#define ARG_B_PYRAMID_DEFAULT          FALSE
#define ARG_WEIGHTB_DEFAULT            FALSE
#define ARG_SPS_ID_DEFAULT             0
#define ARG_AU_NALU_DEFAULT            TRUE
#define ARG_TRELLIS_DEFAULT            TRUE
#define ARG_KEYINT_MAX_DEFAULT         0
#define ARG_CABAC_DEFAULT              TRUE
#define ARG_QP_MIN_DEFAULT             10
#define ARG_QP_MAX_DEFAULT             51
#define ARG_QP_STEP_DEFAULT            4
#define ARG_IP_FACTOR_DEFAULT          1.4
#define ARG_PB_FACTOR_DEFAULT          1.3
#define ARG_NR_DEFAULT                 0
#define ARG_INTERLACED_DEFAULT         FALSE
#define ARG_SLICED_THREADS_DEFAULT     FALSE
#define ARG_SYNC_LOOKAHEAD_DEFAULT     -1
#define ARG_RC_MB_TREE_DEFAULT         TRUE
#define ARG_RC_LOOKAHEAD_DEFAULT       40
#define ARG_INTRA_REFRESH_DEFAULT      FALSE
#define ARG_OPTION_STRING_DEFAULT      ""
static GString *x264enc_defaults;
#define ARG_SPEED_PRESET_DEFAULT       6        /* 'medium' preset - matches x264 CLI default */
#define ARG_PSY_TUNE_DEFAULT           0        /* no psy tuning */
#define ARG_TUNE_DEFAULT               0        /* no tuning */

enum
{
  GST_X264_ENC_STREAM_FORMAT_FROM_PROPERTY,
  GST_X264_ENC_STREAM_FORMAT_AVC,
  GST_X264_ENC_STREAM_FORMAT_BYTE_STREAM
};

enum
{
  GST_X264_ENC_PASS_CBR = 0,
  GST_X264_ENC_PASS_QUANT = 0x04,
  GST_X264_ENC_PASS_QUAL,
  GST_X264_ENC_PASS_PASS1 = 0x11,
  GST_X264_ENC_PASS_PASS2,
  GST_X264_ENC_PASS_PASS3
};

#define GST_X264_ENC_PASS_TYPE (gst_x264_enc_pass_get_type())
static GType
gst_x264_enc_pass_get_type (void)
{
  static GType pass_type = 0;

  static const GEnumValue pass_types[] = {
    {GST_X264_ENC_PASS_CBR, "Constant Bitrate Encoding", "cbr"},
    {GST_X264_ENC_PASS_QUANT, "Constant Quantizer (debugging only)", "quant"},
    {GST_X264_ENC_PASS_QUAL, "Constant Quality", "qual"},
    {GST_X264_ENC_PASS_PASS1, "VBR Encoding - Pass 1", "pass1"},
    {GST_X264_ENC_PASS_PASS2, "VBR Encoding - Pass 2", "pass2"},
    {GST_X264_ENC_PASS_PASS3, "VBR Encoding - Pass 3", "pass3"},
    {0, NULL, NULL}
  };

  if (!pass_type) {
    pass_type = g_enum_register_static ("GstX264EncPass", pass_types);
  }
  return pass_type;
}

#define GST_X264_ENC_ME_TYPE (gst_x264_enc_me_get_type())
static GType
gst_x264_enc_me_get_type (void)
{
  static GType me_type = 0;
  static GEnumValue *me_types;
  int n, i;

  if (me_type != 0)
    return me_type;

  n = 0;
  while (x264_motion_est_names[n] != NULL)
    n++;

  me_types = g_new0 (GEnumValue, n + 1);

  for (i = 0; i < n; i++) {
    me_types[i].value = i;
    me_types[i].value_name = x264_motion_est_names[i];
    me_types[i].value_nick = x264_motion_est_names[i];
  }

  me_type = g_enum_register_static ("GstX264EncMe", me_types);

  return me_type;
}

#define GST_X264_ENC_ANALYSE_TYPE (gst_x264_enc_analyse_get_type())
static GType
gst_x264_enc_analyse_get_type (void)
{
  static GType analyse_type = 0;
  static const GFlagsValue analyse_types[] = {
    {X264_ANALYSE_I4x4, "i4x4", "i4x4"},
    {X264_ANALYSE_I8x8, "i8x8", "i8x8"},
    {X264_ANALYSE_PSUB16x16, "p8x8", "p8x8"},
    {X264_ANALYSE_PSUB8x8, "p4x4", "p4x4"},
    {X264_ANALYSE_BSUB16x16, "b8x8", "b8x8"},
    {0, NULL, NULL},
  };

  if (!analyse_type) {
    analyse_type = g_flags_register_static ("GstX264EncAnalyse", analyse_types);
  }
  return analyse_type;
}

#define GST_X264_ENC_SPEED_PRESET_TYPE (gst_x264_enc_speed_preset_get_type())
static GType
gst_x264_enc_speed_preset_get_type (void)
{
  static GType speed_preset_type = 0;
  static GEnumValue *speed_preset_types;
  int n, i;

  if (speed_preset_type != 0)
    return speed_preset_type;

  n = 0;
  while (x264_preset_names[n] != NULL)
    n++;

  speed_preset_types = g_new0 (GEnumValue, n + 2);

  speed_preset_types[0].value = 0;
  speed_preset_types[0].value_name = "No preset";
  speed_preset_types[0].value_nick = "None";

  for (i = 1; i <= n; i++) {
    speed_preset_types[i].value = i;
    speed_preset_types[i].value_name = x264_preset_names[i - 1];
    speed_preset_types[i].value_nick = x264_preset_names[i - 1];
  }

  speed_preset_type =
      g_enum_register_static ("GstX264EncPreset", speed_preset_types);

  return speed_preset_type;
}

static const GFlagsValue tune_types[] = {
  {0x0, "No tuning", "none"},
  {0x1, "Still image", "stillimage"},
  {0x2, "Fast decode", "fastdecode"},
  {0x4, "Zero latency", "zerolatency"},
  {0, NULL, NULL},
};

#define GST_X264_ENC_TUNE_TYPE (gst_x264_enc_tune_get_type())
static GType
gst_x264_enc_tune_get_type (void)
{
  static GType tune_type = 0;

  if (!tune_type) {
    tune_type = g_flags_register_static ("GstX264EncTune", tune_types + 1);
  }
  return tune_type;
}

enum
{
  GST_X264_ENC_TUNE_NONE,
  GST_X264_ENC_TUNE_FILM,
  GST_X264_ENC_TUNE_ANIMATION,
  GST_X264_ENC_TUNE_GRAIN,
  GST_X264_ENC_TUNE_PSNR,
  GST_X264_ENC_TUNE_SSIM,
  GST_X264_ENC_TUNE_LAST
};

static const GEnumValue psy_tune_types[] = {
  {GST_X264_ENC_TUNE_NONE, "No tuning", "none"},
  {GST_X264_ENC_TUNE_FILM, "Film", "film"},
  {GST_X264_ENC_TUNE_ANIMATION, "Animation", "animation"},
  {GST_X264_ENC_TUNE_GRAIN, "Grain", "grain"},
  {GST_X264_ENC_TUNE_PSNR, "PSNR", "psnr"},
  {GST_X264_ENC_TUNE_SSIM, "SSIM", "ssim"},
  {0, NULL, NULL},
};

#define GST_X264_ENC_PSY_TUNE_TYPE (gst_x264_enc_psy_tune_get_type())
static GType
gst_x264_enc_psy_tune_get_type (void)
{
  static GType psy_tune_type = 0;

  if (!psy_tune_type) {
    psy_tune_type =
        g_enum_register_static ("GstX264EncPsyTune", psy_tune_types);
  }
  return psy_tune_type;
}

static void
gst_x264_enc_build_tunings_string (GstX264Enc * x264enc)
{
  int i = 1;

  if (x264enc->tunings)
    g_string_free (x264enc->tunings, TRUE);

  if (x264enc->psy_tune) {
    x264enc->tunings =
        g_string_new (psy_tune_types[x264enc->psy_tune].value_nick);
  } else {
    x264enc->tunings = g_string_new (NULL);
  }

  while (tune_types[i].value_name) {
    if (x264enc->tune & (1 << (i - 1)))
      g_string_append_printf (x264enc->tunings, "%s%s",
          x264enc->tunings->len ? "," : "", tune_types[i].value_nick);
    i++;
  }

  if (x264enc->tunings->len)
    GST_DEBUG_OBJECT (x264enc, "Constructed tunings string: %s",
        x264enc->tunings->str);
}

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define FORMATS "I420, YV12, Y42B, Y444, NV12, I420_10LE, I422_10LE, Y444_10LE"
#else
#define FORMATS "I420, YV12, Y42B, Y444, NV12, I420_10BE, I422_10BE, Y444_10BE"
#endif

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) { " FORMATS " }, "
        "framerate = (fraction) [0, MAX], "
        "width = (int) [ 16, MAX ], " "height = (int) [ 16, MAX ]")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ], "
        "stream-format = (string) { avc, byte-stream }, "
        "alignment = (string) au, "
        "profile = (string) { high-10, high, main, baseline, "
        "constrained-baseline, high-10-intra }")
    );

static void gst_x264_enc_finalize (GObject * object);
static gboolean gst_x264_enc_reset (GstVideoEncoder * encoder, gboolean hard);

static gboolean gst_x264_enc_init_encoder (GstX264Enc * encoder);
static void gst_x264_enc_close_encoder (GstX264Enc * encoder);

static GstFlowReturn gst_x264_enc_finish (GstVideoEncoder * encoder);
static GstFlowReturn gst_x264_enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static void gst_x264_enc_flush_frames (GstX264Enc * encoder, gboolean send);
static GstFlowReturn gst_x264_enc_encode_frame (GstX264Enc * encoder,
    x264_picture_t * pic_in, GstVideoCodecFrame * input_frame, int *i_nal,
    gboolean send);
static gboolean gst_x264_enc_stop (GstVideoEncoder * encoder);
static gboolean gst_x264_enc_set_format (GstVideoEncoder * video_enc,
    GstVideoCodecState * state);
static gboolean gst_x264_enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);

static void gst_x264_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_x264_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define gst_x264_enc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstX264Enc, gst_x264_enc, GST_TYPE_VIDEO_ENCODER,
    G_IMPLEMENT_INTERFACE (GST_TYPE_PRESET, NULL));

/* don't forget to free the string after use */
static const gchar *
gst_x264_enc_build_partitions (gint analyse)
{
  GString *string;

  if (!analyse)
    return NULL;

  string = g_string_new (NULL);
  if (analyse & X264_ANALYSE_I4x4)
    g_string_append (string, "i4x4");
  if (analyse & X264_ANALYSE_I8x8)
    g_string_append (string, ",i8x8");
  if (analyse & X264_ANALYSE_PSUB16x16)
    g_string_append (string, ",p8x8");
  if (analyse & X264_ANALYSE_PSUB8x8)
    g_string_append (string, ",p4x4");
  if (analyse & X264_ANALYSE_BSUB16x16)
    g_string_append (string, ",b8x8");

  return (const gchar *) g_string_free (string, FALSE);
}

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

static GstCaps *
gst_x264_enc_get_supported_input_caps (void)
{
  GValue fmt = G_VALUE_INIT;
  GstCaps *caps;

  caps = gst_caps_new_empty_simple ("video/x-raw");

  if (x264_bit_depth == 8) {
    GST_INFO ("This x264 build supports 8-bit depth");
    if (x264_chroma_format == 0) {
      set_value (&fmt, 5, "I420", "YV12", "Y42B", "Y444", "NV12");
    } else if (x264_chroma_format == X264_CSP_I420) {
      set_value (&fmt, 3, "I420", "YV12", "NV12");
    } else if (x264_chroma_format == X264_CSP_I422) {
      set_value (&fmt, 1, "Y42B");
    } else if (x264_chroma_format == X264_CSP_I444) {
      set_value (&fmt, 1, "Y444");
    } else {
      GST_ERROR ("Unsupported chroma format %d", x264_chroma_format);
    }
  } else if (x264_bit_depth == 10) {
    GST_INFO ("This x264 build supports 10-bit depth");

    if (G_BYTE_ORDER == G_LITTLE_ENDIAN) {
      if (x264_chroma_format == 0) {
        set_value (&fmt, 3, "I420_10LE", "I422_10LE", "Y444_10LE");
      } else if (x264_chroma_format == X264_CSP_I420) {
        set_value (&fmt, 1, "I420_10LE");
      } else if (x264_chroma_format == X264_CSP_I422) {
        set_value (&fmt, 1, "Y422_10LE");
      } else if (x264_chroma_format == X264_CSP_I444) {
        set_value (&fmt, 1, "Y444_10LE");
      } else {
        GST_ERROR ("Unsupported chroma format %d", x264_chroma_format);
      }
    } else {
      if (x264_chroma_format == 0) {
        set_value (&fmt, 3, "I420_10BE", "I422_10BE", "Y444_10BE");
      } else if (x264_chroma_format == X264_CSP_I420) {
        set_value (&fmt, 1, "I420_10BE");
      } else if (x264_chroma_format == X264_CSP_I422) {
        set_value (&fmt, 1, "Y422_10BE");
      } else if (x264_chroma_format == X264_CSP_I444) {
        set_value (&fmt, 1, "Y444_10BE");
      } else {
        GST_ERROR ("Unsupported chroma format %d", x264_chroma_format);
      }
    }
  } else {
    GST_ERROR ("Unsupported bit depth %d, we only support 8-bit and 10-bit",
        x264_bit_depth);
  }

  if (G_VALUE_TYPE (&fmt) != G_TYPE_INVALID)
    gst_structure_take_value (gst_caps_get_structure (caps, 0), "format", &fmt);

  gst_caps_set_simple (caps,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
      "width", GST_TYPE_INT_RANGE, 16, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 16, G_MAXINT, NULL);

  GST_DEBUG ("returning %" GST_PTR_FORMAT, caps);
  return caps;
}

/* allowed input caps depending on whether libx264 was built for 8 or 10 bits */
static GstCaps *
gst_x264_enc_sink_getcaps (GstVideoEncoder * enc, GstCaps * filter)
{
  GstCaps *supported_incaps, *caps;

  supported_incaps = gst_x264_enc_get_supported_input_caps ();
  caps = gst_video_encoder_proxy_getcaps (enc, supported_incaps, filter);
  gst_caps_unref (supported_incaps);

  return caps;
}

static void
gst_x264_enc_class_init (GstX264EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstVideoEncoderClass *gstencoder_class;
  const gchar *partitions = NULL;

  x264enc_defaults = g_string_new ("");

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  gstencoder_class = GST_VIDEO_ENCODER_CLASS (klass);

  gobject_class->set_property = gst_x264_enc_set_property;
  gobject_class->get_property = gst_x264_enc_get_property;
  gobject_class->finalize = gst_x264_enc_finalize;

  gstencoder_class->stop = GST_DEBUG_FUNCPTR (gst_x264_enc_stop);
  gstencoder_class->set_format = GST_DEBUG_FUNCPTR (gst_x264_enc_set_format);
  gstencoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_x264_enc_handle_frame);
  gstencoder_class->reset = GST_DEBUG_FUNCPTR (gst_x264_enc_reset);
  gstencoder_class->finish = GST_DEBUG_FUNCPTR (gst_x264_enc_finish);
  gstencoder_class->getcaps = GST_DEBUG_FUNCPTR (gst_x264_enc_sink_getcaps);
  gstencoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_x264_enc_propose_allocation);

  /* options for which we don't use string equivalents */
  g_object_class_install_property (gobject_class, ARG_PASS,
      g_param_spec_enum ("pass", "Encoding pass/type",
          "Encoding pass/type", GST_X264_ENC_PASS_TYPE,
          ARG_PASS_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_QUANTIZER,
      g_param_spec_uint ("quantizer", "Constant Quantizer",
          "Constant quantizer or quality to apply",
          1, 50, ARG_QUANTIZER_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate", "Bitrate in kbit/sec", 1,
          100 * 1024, ARG_BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject_class, ARG_VBV_BUF_CAPACITY,
      g_param_spec_uint ("vbv-buf-capacity", "VBV buffer capacity",
          "Size of the VBV buffer in milliseconds",
          0, 10000, ARG_VBV_BUF_CAPACITY_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject_class, ARG_SPEED_PRESET,
      g_param_spec_enum ("speed-preset", "Speed/quality preset",
          "Preset name for speed/quality tradeoff options (can affect decode "
          "compatibility - impose restrictions separately for your target decoder)",
          GST_X264_ENC_SPEED_PRESET_TYPE, ARG_SPEED_PRESET_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_PSY_TUNE,
      g_param_spec_enum ("psy-tune", "Psychovisual tuning preset",
          "Preset name for psychovisual tuning options",
          GST_X264_ENC_PSY_TUNE_TYPE, ARG_PSY_TUNE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_TUNE,
      g_param_spec_flags ("tune", "Content tuning preset",
          "Preset name for non-psychovisual tuning options",
          GST_X264_ENC_TUNE_TYPE, ARG_TUNE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_OPTION_STRING,
      g_param_spec_string ("option-string", "Option string",
          "String of x264 options (overridden by element properties)",
          ARG_OPTION_STRING_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* options for which we _do_ use string equivalents */
  g_object_class_install_property (gobject_class, ARG_THREADS,
      g_param_spec_uint ("threads", "Threads",
          "Number of threads used by the codec (0 for automatic)",
          0, 4, ARG_THREADS_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /* NOTE: this first string append doesn't require the ':' delimiter but the
   * rest do */
  g_string_append_printf (x264enc_defaults, "threads=%d", ARG_THREADS_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_SLICED_THREADS,
      g_param_spec_boolean ("sliced-threads", "Sliced Threads",
          "Low latency but lower efficiency threading",
          ARG_SLICED_THREADS_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":sliced-threads=%d",
      ARG_SLICED_THREADS_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_SYNC_LOOKAHEAD,
      g_param_spec_int ("sync-lookahead", "Sync Lookahead",
          "Number of buffer frames for threaded lookahead (-1 for automatic)",
          -1, 250, ARG_SYNC_LOOKAHEAD_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":sync-lookahead=%d",
      ARG_SYNC_LOOKAHEAD_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_MULTIPASS_CACHE_FILE,
      g_param_spec_string ("multipass-cache-file", "Multipass Cache File",
          "Filename for multipass cache file",
          ARG_MULTIPASS_CACHE_FILE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":stats=%s",
      ARG_MULTIPASS_CACHE_FILE_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_BYTE_STREAM,
      g_param_spec_boolean ("byte-stream", "Byte Stream",
          "Generate byte stream format of NALU", ARG_BYTE_STREAM_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":annexb=%d",
      ARG_BYTE_STREAM_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_INTRA_REFRESH,
      g_param_spec_boolean ("intra-refresh", "Intra Refresh",
          "Use Periodic Intra Refresh instead of IDR frames",
          ARG_INTRA_REFRESH_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":intra-refresh=%d",
      ARG_INTRA_REFRESH_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_ME,
      g_param_spec_enum ("me", "Motion Estimation",
          "Integer pixel motion estimation method", GST_X264_ENC_ME_TYPE,
          ARG_ME_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":me=%s",
      x264_motion_est_names[ARG_ME_DEFAULT]);
  g_object_class_install_property (gobject_class, ARG_SUBME,
      g_param_spec_uint ("subme", "Subpixel Motion Estimation",
          "Subpixel motion estimation and partition decision quality: 1=fast, 10=best",
          1, 10, ARG_SUBME_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":subme=%d", ARG_SUBME_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_ANALYSE,
      g_param_spec_flags ("analyse", "Analyse", "Partitions to consider",
          GST_X264_ENC_ANALYSE_TYPE, ARG_ANALYSE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  partitions = gst_x264_enc_build_partitions (ARG_ANALYSE_DEFAULT);
  if (partitions) {
    g_string_append_printf (x264enc_defaults, ":partitions=%s", partitions);
    g_free ((gpointer) partitions);
  }
  g_object_class_install_property (gobject_class, ARG_DCT8x8,
      g_param_spec_boolean ("dct8x8", "DCT8x8",
          "Adaptive spatial transform size", ARG_DCT8x8_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":8x8dct=%d", ARG_DCT8x8_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_REF,
      g_param_spec_uint ("ref", "Reference Frames",
          "Number of reference frames",
          1, 12, ARG_REF_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":ref=%d", ARG_REF_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_BFRAMES,
      g_param_spec_uint ("bframes", "B-Frames",
          "Number of B-frames between I and P",
          0, 4, ARG_BFRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":bframes=%d", ARG_BFRAMES_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_B_ADAPT,
      g_param_spec_boolean ("b-adapt", "B-Adapt",
          "Automatically decide how many B-frames to use",
          ARG_B_ADAPT_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":b-adapt=%d", ARG_B_ADAPT_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_B_PYRAMID,
      g_param_spec_boolean ("b-pyramid", "B-Pyramid",
          "Keep some B-frames as references", ARG_B_PYRAMID_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":b-pyramid=%s",
      x264_b_pyramid_names[ARG_B_PYRAMID_DEFAULT]);
  g_object_class_install_property (gobject_class, ARG_WEIGHTB,
      g_param_spec_boolean ("weightb", "Weighted B-Frames",
          "Weighted prediction for B-frames", ARG_WEIGHTB_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":weightb=%d", ARG_WEIGHTB_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_SPS_ID,
      g_param_spec_uint ("sps-id", "SPS ID",
          "SPS and PPS ID number",
          0, 31, ARG_SPS_ID_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":sps-id=%d", ARG_SPS_ID_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_AU_NALU,
      g_param_spec_boolean ("aud", "AUD",
          "Use AU (Access Unit) delimiter", ARG_AU_NALU_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":aud=%d", ARG_AU_NALU_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_TRELLIS,
      g_param_spec_boolean ("trellis", "Trellis quantization",
          "Enable trellis searched quantization", ARG_TRELLIS_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":trellis=%d", ARG_TRELLIS_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_KEYINT_MAX,
      g_param_spec_uint ("key-int-max", "Key-frame maximal interval",
          "Maximal distance between two key-frames (0 for automatic)",
          0, G_MAXINT, ARG_KEYINT_MAX_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":keyint=%d",
      ARG_KEYINT_MAX_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_CABAC,
      g_param_spec_boolean ("cabac", "Use CABAC", "Enable CABAC entropy coding",
          ARG_CABAC_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":cabac=%d", ARG_CABAC_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_QP_MIN,
      g_param_spec_uint ("qp-min", "Minimum Quantizer",
          "Minimum quantizer", 1, 51, ARG_QP_MIN_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":qpmin=%d", ARG_QP_MIN_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_QP_MAX,
      g_param_spec_uint ("qp-max", "Maximum Quantizer",
          "Maximum quantizer", 1, 51, ARG_QP_MAX_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":qpmax=%d", ARG_QP_MAX_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_QP_STEP,
      g_param_spec_uint ("qp-step", "Maximum Quantizer Difference",
          "Maximum quantizer difference between frames",
          1, 50, ARG_QP_STEP_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":qpstep=%d", ARG_QP_STEP_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_IP_FACTOR,
      g_param_spec_float ("ip-factor", "IP-Factor",
          "Quantizer factor between I- and P-frames",
          0, 2, ARG_IP_FACTOR_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":ip-factor=%f",
      ARG_IP_FACTOR_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_PB_FACTOR,
      g_param_spec_float ("pb-factor", "PB-Factor",
          "Quantizer factor between P- and B-frames", 0, 2,
          ARG_PB_FACTOR_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":pb-factor=%f",
      ARG_PB_FACTOR_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_RC_MB_TREE,
      g_param_spec_boolean ("mb-tree", "Macroblock Tree",
          "Macroblock-Tree ratecontrol",
          ARG_RC_MB_TREE_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":mbtree=%d",
      ARG_RC_MB_TREE_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_RC_LOOKAHEAD,
      g_param_spec_int ("rc-lookahead", "Rate Control Lookahead",
          "Number of frames for frametype lookahead", 0, 250,
          ARG_RC_LOOKAHEAD_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":rc-lookahead=%d",
      ARG_RC_LOOKAHEAD_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_NR,
      g_param_spec_uint ("noise-reduction", "Noise Reduction",
          "Noise reduction strength",
          0, 100000, ARG_NR_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":nr=%d", ARG_NR_DEFAULT);
  g_object_class_install_property (gobject_class, ARG_INTERLACED,
      g_param_spec_boolean ("interlaced", "Interlaced",
          "Interlaced material", ARG_INTERLACED_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_string_append_printf (x264enc_defaults, ":interlaced=%d",
      ARG_INTERLACED_DEFAULT);

  /* append deblock parameters */
  g_string_append_printf (x264enc_defaults, ":deblock=0,0");
  /* append weighted prediction parameter */
  g_string_append_printf (x264enc_defaults, ":weightp=0");

  gst_element_class_set_static_metadata (element_class,
      "x264enc", "Codec/Encoder/Video", "H264 Encoder",
      "Josef Zlomek <josef.zlomek@itonis.tv>, "
      "Mark Nauwelaerts <mnauw@users.sf.net>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
}

static void
gst_x264_enc_log_callback (gpointer private, gint level, const char *format,
    va_list args)
{
#ifndef GST_DISABLE_GST_DEBUG
  GstDebugLevel gst_level;
  GObject *object = (GObject *) private;

  switch (level) {
    case X264_LOG_NONE:
      gst_level = GST_LEVEL_NONE;
      break;
    case X264_LOG_ERROR:
      gst_level = GST_LEVEL_ERROR;
      break;
    case X264_LOG_WARNING:
      gst_level = GST_LEVEL_WARNING;
      break;
    case X264_LOG_INFO:
      gst_level = GST_LEVEL_INFO;
      break;
    default:
      /* push x264enc debug down to our lower levels to avoid some clutter */
      gst_level = GST_LEVEL_LOG;
      break;
  }

  gst_debug_log_valist (x264_enc_debug, gst_level, "", "", 0, object, format,
      args);
#endif /* GST_DISABLE_GST_DEBUG */
}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_x264_enc_init (GstX264Enc * encoder)
{
  /* properties */
  encoder->threads = ARG_THREADS_DEFAULT;
  encoder->sliced_threads = ARG_SLICED_THREADS_DEFAULT;
  encoder->sync_lookahead = ARG_SYNC_LOOKAHEAD_DEFAULT;
  encoder->pass = ARG_PASS_DEFAULT;
  encoder->quantizer = ARG_QUANTIZER_DEFAULT;
  encoder->mp_cache_file = g_strdup (ARG_MULTIPASS_CACHE_FILE_DEFAULT);
  encoder->byte_stream = ARG_BYTE_STREAM_DEFAULT;
  encoder->bitrate = ARG_BITRATE_DEFAULT;
  encoder->intra_refresh = ARG_INTRA_REFRESH_DEFAULT;
  encoder->vbv_buf_capacity = ARG_VBV_BUF_CAPACITY_DEFAULT;
  encoder->me = ARG_ME_DEFAULT;
  encoder->subme = ARG_SUBME_DEFAULT;
  encoder->analyse = ARG_ANALYSE_DEFAULT;
  encoder->dct8x8 = ARG_DCT8x8_DEFAULT;
  encoder->ref = ARG_REF_DEFAULT;
  encoder->bframes = ARG_BFRAMES_DEFAULT;
  encoder->b_adapt = ARG_B_ADAPT_DEFAULT;
  encoder->b_pyramid = ARG_B_PYRAMID_DEFAULT;
  encoder->weightb = ARG_WEIGHTB_DEFAULT;
  encoder->sps_id = ARG_SPS_ID_DEFAULT;
  encoder->au_nalu = ARG_AU_NALU_DEFAULT;
  encoder->trellis = ARG_TRELLIS_DEFAULT;
  encoder->keyint_max = ARG_KEYINT_MAX_DEFAULT;
  encoder->cabac = ARG_CABAC_DEFAULT;
  encoder->qp_min = ARG_QP_MIN_DEFAULT;
  encoder->qp_max = ARG_QP_MAX_DEFAULT;
  encoder->qp_step = ARG_QP_STEP_DEFAULT;
  encoder->ip_factor = ARG_IP_FACTOR_DEFAULT;
  encoder->pb_factor = ARG_PB_FACTOR_DEFAULT;
  encoder->mb_tree = ARG_RC_MB_TREE_DEFAULT;
  encoder->rc_lookahead = ARG_RC_LOOKAHEAD_DEFAULT;
  encoder->noise_reduction = ARG_NR_DEFAULT;
  encoder->interlaced = ARG_INTERLACED_DEFAULT;
  encoder->option_string = g_string_new (NULL);
  encoder->option_string_prop = g_string_new (ARG_OPTION_STRING_DEFAULT);
  encoder->speed_preset = ARG_SPEED_PRESET_DEFAULT;
  encoder->psy_tune = ARG_PSY_TUNE_DEFAULT;
  encoder->tune = ARG_TUNE_DEFAULT;

  x264_param_default (&encoder->x264param);

  /* log callback setup; part of parameters */
  encoder->x264param.pf_log = gst_x264_enc_log_callback;
  encoder->x264param.p_log_private = encoder;
  encoder->x264param.i_log_level = X264_LOG_DEBUG;

  gst_x264_enc_reset (GST_VIDEO_ENCODER (encoder), FALSE);
}

typedef struct
{
  GstVideoCodecFrame *frame;
  GstVideoFrame vframe;
} FrameData;

static FrameData *
gst_x264_enc_queue_frame (GstX264Enc * enc, GstVideoCodecFrame * frame,
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
gst_x264_enc_dequeue_frame (GstX264Enc * enc, GstVideoCodecFrame * frame)
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
gst_x264_enc_dequeue_all_frames (GstX264Enc * enc)
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
gst_x264_enc_reset (GstVideoEncoder * encoder, gboolean hard)
{
  GstX264Enc *x264enc = GST_X264_ENC (encoder);

  if (hard) {
    gst_x264_enc_flush_frames (x264enc, FALSE);
    gst_x264_enc_close_encoder (x264enc);
  }

  if (x264enc->input_state)
    gst_video_codec_state_unref (x264enc->input_state);
  x264enc->input_state = NULL;
  x264enc->current_byte_stream = GST_X264_ENC_STREAM_FORMAT_FROM_PROPERTY;

  gst_x264_enc_dequeue_all_frames (x264enc);

  return TRUE;
}

static void
gst_x264_enc_finalize (GObject * object)
{
  GstX264Enc *encoder = GST_X264_ENC (object);

  if (encoder->input_state)
    gst_video_codec_state_unref (encoder->input_state);
  encoder->input_state = NULL;

#define FREE_STRING(ptr) \
  if (ptr) \
    g_string_free (ptr, TRUE);

  FREE_STRING (encoder->tunings);
  FREE_STRING (encoder->option_string);
  FREE_STRING (encoder->option_string_prop);

#undef FREE_STRING

  g_free (encoder->mp_cache_file);
  encoder->mp_cache_file = NULL;

  gst_x264_enc_close_encoder (encoder);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/*
 * gst_x264_enc_parse_options
 * @encoder: Encoder to which options are assigned
 * @str: Option string
 *
 * Parse option string and assign to x264 parameters
 *
 */
static gboolean
gst_x264_enc_parse_options (GstX264Enc * encoder, const gchar * str)
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
        x264_param_parse (&encoder->x264param, key_val[0], key_val[1]);

    if (parse_result == X264_PARAM_BAD_NAME) {
      GST_ERROR_OBJECT (encoder, "Bad name for option %s=%s",
          key_val[0] ? key_val[0] : "", key_val[1] ? key_val[1] : "");
    }
    if (parse_result == X264_PARAM_BAD_VALUE) {
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

static gint
gst_x264_enc_gst_to_x264_video_format (GstVideoFormat format, gint * nplanes)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      if (nplanes)
        *nplanes = 3;
      return X264_CSP_I420;
      break;
    case GST_VIDEO_FORMAT_I420_10BE:
    case GST_VIDEO_FORMAT_I420_10LE:
      if (nplanes)
        *nplanes = 3;
      return X264_CSP_I420 | X264_CSP_HIGH_DEPTH;
      break;
    case GST_VIDEO_FORMAT_Y42B:
      if (nplanes)
        *nplanes = 3;
      return X264_CSP_I422;
      break;
    case GST_VIDEO_FORMAT_I422_10BE:
    case GST_VIDEO_FORMAT_I422_10LE:
      if (nplanes)
        *nplanes = 3;
      return X264_CSP_I422 | X264_CSP_HIGH_DEPTH;
      break;
    case GST_VIDEO_FORMAT_Y444:
      if (nplanes)
        *nplanes = 3;
      return X264_CSP_I444;
      break;
    case GST_VIDEO_FORMAT_Y444_10BE:
    case GST_VIDEO_FORMAT_Y444_10LE:
      if (nplanes)
        *nplanes = 3;
      return X264_CSP_I444 | X264_CSP_HIGH_DEPTH;
      break;
    case GST_VIDEO_FORMAT_NV12:
      if (nplanes)
        *nplanes = 2;
      return X264_CSP_NV12;
      break;
    default:
      g_assert_not_reached ();
      return GST_VIDEO_FORMAT_UNKNOWN;
      break;
  }
}

/*
 * gst_x264_enc_init_encoder
 * @encoder:  Encoder which should be initialized.
 *
 * Initialize x264 encoder.
 *
 */
static gboolean
gst_x264_enc_init_encoder (GstX264Enc * encoder)
{
  guint pass = 0;
  GstVideoInfo *info = &encoder->input_state->info;

  /* make sure that the encoder is closed */
  gst_x264_enc_close_encoder (encoder);

  GST_OBJECT_LOCK (encoder);

  gst_x264_enc_build_tunings_string (encoder);

  /* set x264 parameters and use preset/tuning if present */
  GST_DEBUG_OBJECT (encoder, "Applying defaults with preset %s, tunings %s",
      encoder->speed_preset ? x264_preset_names[encoder->speed_preset - 1] : "",
      encoder->tunings && encoder->tunings->len ? encoder->tunings->str : "");
  x264_param_default_preset (&encoder->x264param,
      encoder->speed_preset ? x264_preset_names[encoder->speed_preset -
          1] : NULL, encoder->tunings
      && encoder->tunings->len ? encoder->tunings->str : NULL);

  /* log callback setup; part of parameters
   * this needs to be done again after every *param_default* () call */
  encoder->x264param.pf_log = gst_x264_enc_log_callback;
  encoder->x264param.p_log_private = encoder;
  encoder->x264param.i_log_level = X264_LOG_DEBUG;

  /* if no preset nor tuning, use property defaults */
  if (!encoder->speed_preset && !encoder->tunings->len) {
    GST_DEBUG_OBJECT (encoder, "Applying x264enc_defaults");
    if (x264enc_defaults->len
        && gst_x264_enc_parse_options (encoder,
            x264enc_defaults->str) == FALSE) {
      GST_DEBUG_OBJECT (encoder,
          "x264enc_defaults string contains errors. This is a bug.");
      goto unlock_and_return;
    }
  } else {
    /* When using presets we need to respect the default output format */
    encoder->x264param.b_aud = encoder->au_nalu;
    encoder->x264param.b_annexb = encoder->byte_stream;
  }

  /* setup appropriate timebase for gstreamer */
  encoder->x264param.i_timebase_num = 1;
  encoder->x264param.i_timebase_den = 1000000000;

  /* apply option-string property */
  if (encoder->option_string_prop && encoder->option_string_prop->len) {
    GST_DEBUG_OBJECT (encoder, "Applying option-string: %s",
        encoder->option_string_prop->str);
    if (gst_x264_enc_parse_options (encoder,
            encoder->option_string_prop->str) == FALSE) {
      GST_DEBUG_OBJECT (encoder, "Your option-string contains errors.");
      goto unlock_and_return;
    }
  }
  /* apply user-set options */
  if (encoder->option_string && encoder->option_string->len) {
    GST_DEBUG_OBJECT (encoder, "Applying user-set options: %s",
        encoder->option_string->str);
    if (gst_x264_enc_parse_options (encoder,
            encoder->option_string->str) == FALSE) {
      GST_DEBUG_OBJECT (encoder, "Failed to parse internal option string. "
          "This could be due to use of an old libx264 version. Option string "
          "was: %s", encoder->option_string->str);
    }
  }

  /* set up encoder parameters */
  encoder->x264param.i_csp =
      gst_x264_enc_gst_to_x264_video_format (info->finfo->format, NULL);
  if (info->fps_d == 0 || info->fps_n == 0) {
    /* No FPS so must use VFR
     * This raises latency apparently see http://mewiki.project357.com/wiki/X264_Encoding_Suggestions */
    encoder->x264param.b_vfr_input = TRUE;
    if (encoder->keyint_max) {  /* NB the default is 250 setup by x264 itself */
      encoder->x264param.i_keyint_max = encoder->keyint_max;
    }
  } else {
    /* FPS available so set it up */
    encoder->x264param.i_fps_num = info->fps_n;
    encoder->x264param.i_fps_den = info->fps_d;
    encoder->x264param.i_keyint_max =
        encoder->keyint_max ? encoder->keyint_max : (10 * info->fps_n /
        info->fps_d);
  }
  encoder->x264param.i_width = info->width;
  encoder->x264param.i_height = info->height;
  if (info->par_d > 0) {
    encoder->x264param.vui.i_sar_width = info->par_n;
    encoder->x264param.vui.i_sar_height = info->par_d;
  }

  if ((((info->height == 576) && ((info->width == 720)
                  || (info->width == 704) || (info->width == 352)))
          || ((info->height == 288) && (info->width == 352)))
      && (info->fps_d == 1) && (info->fps_n == 25)) {
    encoder->x264param.vui.i_vidformat = 1;     /* PAL */
  } else if ((((info->height == 480) && ((info->width == 720)
                  || (info->width == 704) || (info->width == 352)))
          || ((info->height == 240) && (info->width == 352)))
      && (info->fps_d == 1001) && ((info->fps_n == 30000)
          || (info->fps_n == 24000))) {
    encoder->x264param.vui.i_vidformat = 2;     /* NTSC */
  } else
    encoder->x264param.vui.i_vidformat = 5;     /* unspecified */

  encoder->x264param.analyse.b_psnr = 0;

  switch (encoder->pass) {
    case GST_X264_ENC_PASS_QUANT:
      encoder->x264param.rc.i_rc_method = X264_RC_CQP;
      encoder->x264param.rc.i_qp_constant = encoder->quantizer;
      break;
    case GST_X264_ENC_PASS_QUAL:
      encoder->x264param.rc.i_rc_method = X264_RC_CRF;
      encoder->x264param.rc.f_rf_constant = encoder->quantizer;
      encoder->x264param.rc.i_vbv_max_bitrate = encoder->bitrate;
      encoder->x264param.rc.i_vbv_buffer_size
          = encoder->x264param.rc.i_vbv_max_bitrate
          * encoder->vbv_buf_capacity / 1000;
      break;
    case GST_X264_ENC_PASS_CBR:
    case GST_X264_ENC_PASS_PASS1:
    case GST_X264_ENC_PASS_PASS2:
    case GST_X264_ENC_PASS_PASS3:
    default:
      encoder->x264param.rc.i_rc_method = X264_RC_ABR;
      encoder->x264param.rc.i_bitrate = encoder->bitrate;
      encoder->x264param.rc.i_vbv_max_bitrate = encoder->bitrate;
      encoder->x264param.rc.i_vbv_buffer_size =
          encoder->x264param.rc.i_vbv_max_bitrate
          * encoder->vbv_buf_capacity / 1000;
      pass = encoder->pass & 0xF;
      break;
  }

  switch (pass) {
    case 0:
      encoder->x264param.rc.b_stat_read = 0;
      encoder->x264param.rc.b_stat_write = 0;
      break;
    case 1:
      encoder->x264param.rc.b_stat_read = 0;
      encoder->x264param.rc.b_stat_write = 1;
      x264_param_apply_fastfirstpass (&encoder->x264param);
      encoder->x264param.i_frame_reference = 1;
      encoder->x264param.analyse.b_transform_8x8 = 0;
      encoder->x264param.analyse.inter = 0;
      encoder->x264param.analyse.i_me_method = X264_ME_DIA;
      encoder->x264param.analyse.i_subpel_refine =
          MIN (2, encoder->x264param.analyse.i_subpel_refine);
      encoder->x264param.analyse.i_trellis = 0;
      encoder->x264param.analyse.b_fast_pskip = 1;
      break;
    case 2:
      encoder->x264param.rc.b_stat_read = 1;
      encoder->x264param.rc.b_stat_write = 0;
      break;
    case 3:
      encoder->x264param.rc.b_stat_read = 1;
      encoder->x264param.rc.b_stat_write = 1;
      break;
  }

  if (encoder->peer_profile) {
    if (x264_param_apply_profile (&encoder->x264param, encoder->peer_profile))
      GST_WARNING_OBJECT (encoder, "Bad downstream profile name: %s",
          encoder->peer_profile);
  }

  /* If using an intra profile, all frames are intra frames */
  if (encoder->peer_intra_profile)
    encoder->x264param.i_keyint_max = encoder->x264param.i_keyint_min = 1;

  /* Enforce level limits if they were in the caps */
  if (encoder->peer_level) {
    encoder->x264param.i_level_idc = encoder->peer_level->level_idc;

    encoder->x264param.rc.i_bitrate = MIN (encoder->x264param.rc.i_bitrate,
        encoder->peer_level->bitrate);
    encoder->x264param.rc.i_vbv_max_bitrate =
        MIN (encoder->x264param.rc.i_vbv_max_bitrate,
        encoder->peer_level->bitrate);
    encoder->x264param.rc.i_vbv_buffer_size =
        MIN (encoder->x264param.rc.i_vbv_buffer_size, encoder->peer_level->cpb);
    encoder->x264param.analyse.i_mv_range =
        MIN (encoder->x264param.analyse.i_mv_range,
        encoder->peer_level->mv_range);

    if (encoder->peer_level->frame_only) {
      encoder->x264param.b_interlaced = FALSE;
#if X264_BUILD >= 95
      encoder->x264param.b_fake_interlaced = FALSE;
#endif
    }
  }

  encoder->reconfig = FALSE;
  /* good start, will be corrected if needed */
  encoder->dts_offset = 0;

  GST_OBJECT_UNLOCK (encoder);

  encoder->x264enc = x264_encoder_open (&encoder->x264param);
  if (!encoder->x264enc) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE,
        ("Can not initialize x264 encoder."), (NULL));
    return FALSE;
  }

  return TRUE;

unlock_and_return:
  GST_OBJECT_UNLOCK (encoder);
  return FALSE;
}

/* gst_x264_enc_close_encoder
 * @encoder:  Encoder which should close.
 *
 * Close x264 encoder.
 */
static void
gst_x264_enc_close_encoder (GstX264Enc * encoder)
{
  if (encoder->x264enc != NULL) {
    x264_encoder_close (encoder->x264enc);
    encoder->x264enc = NULL;
  }
}

static gboolean
gst_x264_enc_set_profile_and_level (GstX264Enc * encoder, GstCaps * caps)
{
  x264_nal_t *nal;
  int i_nal;
  int header_return;
  gint sps_ni = 0;
  guint8 *sps;


  header_return = x264_encoder_headers (encoder->x264enc, &nal, &i_nal);
  if (header_return < 0) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE, ("Encode x264 header failed."),
        ("x264_encoder_headers return code=%d", header_return));
    return FALSE;
  }

  /* old x264 returns SEI, SPS and PPS, newer one has SEI last */
  if (i_nal == 3 && nal[sps_ni].i_type != 7)
    sps_ni = 1;

  sps = nal[sps_ni].p_payload + 4;
  /* skip NAL unit type */
  sps++;

  gst_codec_utils_h264_caps_set_level_and_profile (caps, sps, 3);

  return TRUE;
}

/*
 * Returns: Buffer with the stream headers.
 */
static GstBuffer *
gst_x264_enc_header_buf (GstX264Enc * encoder)
{
  GstBuffer *buf;
  x264_nal_t *nal;
  int i_nal;
  int header_return;
  int i_size;
  int nal_size;
  guint8 *buffer, *sps;
  gulong buffer_size;
  gint sei_ni = 2, sps_ni = 0, pps_ni = 1;

  if (G_UNLIKELY (encoder->x264enc == NULL))
    return NULL;

  /* Create avcC header. */

  header_return = x264_encoder_headers (encoder->x264enc, &nal, &i_nal);
  if (header_return < 0) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE, ("Encode x264 header failed."),
        ("x264_encoder_headers return code=%d", header_return));
    return NULL;
  }

  /* old x264 returns SEI, SPS and PPS, newer one has SEI last */
  if (i_nal == 3 && nal[sps_ni].i_type != 7) {
    sei_ni = 0;
    sps_ni = 1;
    pps_ni = 2;
  }

  /* x264 is expected to return an SEI (some identification info),
   * and SPS and PPS */
  if (i_nal != 3 || nal[sps_ni].i_type != 7 || nal[pps_ni].i_type != 8 ||
      nal[sps_ni].i_payload < 4 || nal[pps_ni].i_payload < 1) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE, (NULL),
        ("Unexpected x264 header."));
    return NULL;
  }

  GST_MEMDUMP ("SEI", nal[sei_ni].p_payload, nal[sei_ni].i_payload);
  GST_MEMDUMP ("SPS", nal[sps_ni].p_payload, nal[sps_ni].i_payload);
  GST_MEMDUMP ("PPS", nal[pps_ni].p_payload, nal[pps_ni].i_payload);

  /* nal payloads with emulation_prevention_three_byte, and some header data */
  buffer_size = (nal[sps_ni].i_payload + nal[pps_ni].i_payload) * 4 + 100;
  buffer = g_malloc (buffer_size);

  sps = nal[sps_ni].p_payload + 4;
  /* skip NAL unit type */
  sps++;

  buffer[0] = 1;                /* AVC Decoder Configuration Record ver. 1 */
  buffer[1] = sps[0];           /* profile_idc                             */
  buffer[2] = sps[1];           /* profile_compability                     */
  buffer[3] = sps[2];           /* level_idc                               */
  buffer[4] = 0xfc | (4 - 1);   /* nal_length_size_minus1                  */

  i_size = 5;

  buffer[i_size++] = 0xe0 | 1;  /* number of SPSs */

  nal_size = nal[sps_ni].i_payload - 4;
  memcpy (buffer + i_size + 2, nal[sps_ni].p_payload + 4, nal_size);

  GST_WRITE_UINT16_BE (buffer + i_size, nal_size);
  i_size += nal_size + 2;

  buffer[i_size++] = 1;         /* number of PPSs */

  nal_size = nal[pps_ni].i_payload - 4;
  memcpy (buffer + i_size + 2, nal[pps_ni].p_payload + 4, nal_size);

  GST_WRITE_UINT16_BE (buffer + i_size, nal_size);
  i_size += nal_size + 2;

  buf = gst_buffer_new_and_alloc (i_size);
  gst_buffer_fill (buf, 0, buffer, i_size);

  GST_MEMDUMP ("header", buffer, i_size);
  g_free (buffer);

  return buf;
}

/* gst_x264_enc_set_src_caps
 * Returns: TRUE on success.
 */
static gboolean
gst_x264_enc_set_src_caps (GstX264Enc * encoder, GstCaps * caps)
{
  GstCaps *outcaps;
  GstStructure *structure;
  GstVideoCodecState *state;
  GstTagList *tags;

  outcaps = gst_caps_new_empty_simple ("video/x-h264");
  structure = gst_caps_get_structure (outcaps, 0);

  if (encoder->current_byte_stream == GST_X264_ENC_STREAM_FORMAT_FROM_PROPERTY) {
    if (encoder->byte_stream) {
      encoder->current_byte_stream = GST_X264_ENC_STREAM_FORMAT_BYTE_STREAM;
    } else {
      encoder->current_byte_stream = GST_X264_ENC_STREAM_FORMAT_AVC;
    }
  }
  if (encoder->current_byte_stream == GST_X264_ENC_STREAM_FORMAT_AVC) {
    GstBuffer *buf = gst_x264_enc_header_buf (encoder);
    if (buf != NULL) {
      gst_caps_set_simple (outcaps, "codec_data", GST_TYPE_BUFFER, buf, NULL);
      gst_buffer_unref (buf);
    }
    gst_structure_set (structure, "stream-format", G_TYPE_STRING, "avc", NULL);
  } else {
    gst_structure_set (structure, "stream-format", G_TYPE_STRING, "byte-stream",
        NULL);
  }
  gst_structure_set (structure, "alignment", G_TYPE_STRING, "au", NULL);

  if (!gst_x264_enc_set_profile_and_level (encoder, outcaps)) {
    gst_caps_unref (outcaps);
    return FALSE;
  }

  state = gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (encoder),
      outcaps, encoder->input_state);
  GST_DEBUG_OBJECT (encoder, "output caps: %" GST_PTR_FORMAT, state->caps);
  gst_video_codec_state_unref (state);

  tags = gst_tag_list_new_empty ();
  gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_ENCODER, "x264",
      GST_TAG_ENCODER_VERSION, X264_BUILD, NULL);
  gst_video_encoder_merge_tags (GST_VIDEO_ENCODER (encoder), tags,
      GST_TAG_MERGE_REPLACE);
  gst_tag_list_unref (tags);

  return TRUE;
}

static void
gst_x264_enc_set_latency (GstX264Enc * encoder)
{
  GstVideoInfo *info = &encoder->input_state->info;

  if (info->fps_n) {
    GstClockTime latency;
    gint max_delayed_frames;
    max_delayed_frames = x264_encoder_maximum_delayed_frames (encoder->x264enc);
    latency = gst_util_uint64_scale_ceil (GST_SECOND * info->fps_d,
        max_delayed_frames, info->fps_n);

    GST_INFO_OBJECT (encoder,
        "Updating latency to %" GST_TIME_FORMAT " (%d frames)",
        GST_TIME_ARGS (latency), max_delayed_frames);

    gst_video_encoder_set_latency (GST_VIDEO_ENCODER (encoder), latency,
        latency);
  } else {
    /* We can't do live as we don't know our latency */
    gst_video_encoder_set_latency (GST_VIDEO_ENCODER (encoder),
        0, GST_CLOCK_TIME_NONE);
  }
}

static gboolean
gst_x264_enc_set_format (GstVideoEncoder * video_enc,
    GstVideoCodecState * state)
{
  GstX264Enc *encoder = GST_X264_ENC (video_enc);
  GstVideoInfo *info = &state->info;
  GstCaps *template_caps;
  GstCaps *allowed_caps = NULL;
  gboolean level_ok = TRUE;

  /* If the encoder is initialized, do not reinitialize it again if not
   * necessary */
  if (encoder->x264enc) {
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
    gst_x264_enc_flush_frames (encoder, TRUE);

    encoder->sps_id++;
  }

  if (encoder->input_state)
    gst_video_codec_state_unref (state);
  encoder->input_state = gst_video_codec_state_ref (state);

  encoder->peer_profile = NULL;
  encoder->peer_intra_profile = FALSE;
  encoder->peer_level = NULL;

  template_caps = gst_static_pad_template_get_caps (&src_factory);
  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));

  /* Output byte-stream if downstream has ANY caps, it's what people expect,
   * and it makes more sense too */
  if (allowed_caps == template_caps) {
    GST_INFO_OBJECT (encoder,
        "downstream has ANY caps, outputting byte-stream");
    encoder->current_byte_stream = GST_X264_ENC_STREAM_FORMAT_BYTE_STREAM;
  } else if (allowed_caps) {
    GstStructure *s;
    const gchar *profile;
    const gchar *level;
    const gchar *stream_format;

    if (gst_caps_is_empty (allowed_caps)) {
      gst_caps_unref (allowed_caps);
      return FALSE;
    }

    allowed_caps = gst_caps_make_writable (allowed_caps);
    allowed_caps = gst_caps_fixate (allowed_caps);
    s = gst_caps_get_structure (allowed_caps, 0);

    profile = gst_structure_get_string (s, "profile");
    if (profile) {
      /* FIXME - if libx264 ever adds support for FMO, ASO or redundant slices
       * make sure constrained profile has a separate case which disables
       * those */
      if (!strcmp (profile, "constrained-baseline") ||
          !strcmp (profile, "baseline")) {
        encoder->peer_profile = "baseline";
      } else if (!strcmp (profile, "high-10-intra")) {
        encoder->peer_intra_profile = TRUE;
        encoder->peer_profile = "high10";
      } else if (!strcmp (profile, "high-10")) {
        encoder->peer_profile = "high10";
      } else if (!strcmp (profile, "high")) {
        encoder->peer_profile = "high";
      } else if (!strcmp (profile, "main")) {
        encoder->peer_profile = "main";
      } else {
        g_assert_not_reached ();
      }
    }

    level = gst_structure_get_string (s, "level");
    if (level) {
      int level_idc = gst_codec_utils_h264_get_level_idc (level);

      if (level_idc) {
        gint i;

        for (i = 0; x264_levels[i].level_idc; i++) {
          if (level_idc == x264_levels[i].level_idc) {
            int mb_width = (info->width + 15) / 16;
            int mb_height = (info->height + 15) / 16;
            int mbs = mb_width * mb_height;

            if (x264_levels[i].frame_size < mbs ||
                x264_levels[i].frame_size * 8 < mb_width * mb_width ||
                x264_levels[i].frame_size * 8 < mb_height * mb_height) {
              GST_WARNING_OBJECT (encoder,
                  "Frame size larger than level %s allows", level);
              level_ok = FALSE;
              break;
            }

            if (info->fps_d && x264_levels[i].mbps
                < (gint64) mbs * info->fps_n / info->fps_d) {
              GST_WARNING_OBJECT (encoder,
                  "Macroblock rate higher than level %s allows", level);
              level_ok = FALSE;
              break;
            }

            encoder->peer_level = &x264_levels[i];
            break;
          }
        }
      }
    }

    stream_format = gst_structure_get_string (s, "stream-format");
    encoder->current_byte_stream = GST_X264_ENC_STREAM_FORMAT_FROM_PROPERTY;
    if (stream_format) {
      if (!strcmp (stream_format, "avc")) {
        encoder->current_byte_stream = GST_X264_ENC_STREAM_FORMAT_AVC;
        g_string_append_printf (encoder->option_string, ":annexb=0");
      } else if (!strcmp (stream_format, "byte-stream")) {
        encoder->current_byte_stream = GST_X264_ENC_STREAM_FORMAT_BYTE_STREAM;
        g_string_append_printf (encoder->option_string, ":annexb=1");
      } else {
        /* means we have both in caps and _FROM_PROPERTY should be the option */
      }
    }

    gst_caps_unref (allowed_caps);
  }

  gst_caps_unref (template_caps);

  if (!level_ok)
    return FALSE;

  if (!gst_x264_enc_init_encoder (encoder))
    return FALSE;

  if (!gst_x264_enc_set_src_caps (encoder, state->caps)) {
    gst_x264_enc_close_encoder (encoder);
    return FALSE;
  }

  gst_x264_enc_set_latency (encoder);

  return TRUE;
}

static GstFlowReturn
gst_x264_enc_finish (GstVideoEncoder * encoder)
{
  gst_x264_enc_flush_frames (GST_X264_ENC (encoder), TRUE);
  return GST_FLOW_OK;
}

static gboolean
gst_x264_enc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_x264_enc_handle_frame (GstVideoEncoder * video_enc,
    GstVideoCodecFrame * frame)
{
  GstX264Enc *encoder = GST_X264_ENC (video_enc);
  GstVideoInfo *info = &encoder->input_state->info;
  GstFlowReturn ret;
  x264_picture_t pic_in;
  gint i_nal, i;
  FrameData *fdata;
  gint nplanes;

  if (G_UNLIKELY (encoder->x264enc == NULL))
    goto not_inited;

  /* create x264_picture_t from the buffer */
  /* mostly taken from mplayer (file ve_x264.c) */

  /* set up input picture */
  memset (&pic_in, 0, sizeof (pic_in));

  fdata = gst_x264_enc_queue_frame (encoder, frame, info);
  if (!fdata)
    goto invalid_frame;

  pic_in.img.i_csp =
      gst_x264_enc_gst_to_x264_video_format (info->finfo->format, &nplanes);
  pic_in.img.i_plane = nplanes;
  for (i = 0; i < nplanes; i++) {
    pic_in.img.plane[i] = GST_VIDEO_FRAME_PLANE_DATA (&fdata->vframe, i);
    pic_in.img.i_stride[i] = GST_VIDEO_FRAME_COMP_STRIDE (&fdata->vframe, i);
  }

  pic_in.i_type = X264_TYPE_AUTO;
  pic_in.i_pts = frame->pts;
  pic_in.i_dts = frame->dts;
  pic_in.opaque = GINT_TO_POINTER (frame->system_frame_number);

  ret = gst_x264_enc_encode_frame (encoder, &pic_in, frame, &i_nal, TRUE);

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
gst_x264_enc_encode_frame (GstX264Enc * encoder, x264_picture_t * pic_in,
    GstVideoCodecFrame * input_frame, int *i_nal, gboolean send)
{
  GstVideoCodecFrame *frame = NULL;
  GstBuffer *out_buf = NULL;
  x264_picture_t pic_out;
  x264_nal_t *nal;
  int i_size;
  int encoder_return;
  GstFlowReturn ret = GST_FLOW_OK;
  guint8 *data;
  gboolean update_latency = FALSE;

  if (G_UNLIKELY (encoder->x264enc == NULL)) {
    if (input_frame)
      gst_video_codec_frame_unref (input_frame);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  GST_OBJECT_LOCK (encoder);
  if (encoder->reconfig) {
    encoder->reconfig = FALSE;
    if (x264_encoder_reconfig (encoder->x264enc, &encoder->x264param) < 0)
      GST_WARNING_OBJECT (encoder, "Could not reconfigure");
    update_latency = TRUE;
  }

  if (pic_in && input_frame) {
    if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (input_frame)) {
      GST_INFO_OBJECT (encoder, "Forcing key frame");
      if (encoder->intra_refresh)
        x264_encoder_intra_refresh (encoder->x264enc);
      else
        pic_in->i_type = X264_TYPE_IDR;
    }
  }
  GST_OBJECT_UNLOCK (encoder);

  if (G_UNLIKELY (update_latency))
    gst_x264_enc_set_latency (encoder);

  encoder_return = x264_encoder_encode (encoder->x264enc,
      &nal, i_nal, pic_in, &pic_out);

  if (encoder_return < 0) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE, ("Encode x264 frame failed."),
        ("x264_encoder_encode return code=%d", encoder_return));
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
    goto out;
  }

  i_size = encoder_return;
  data = nal[0].p_payload;

  frame = gst_video_encoder_get_frame (GST_VIDEO_ENCODER (encoder),
      GPOINTER_TO_INT (pic_out.opaque));
  g_assert (frame || !send);

  if (!send || !frame) {
    ret = GST_FLOW_OK;
    goto out;
  }

  out_buf = gst_buffer_new_allocate (NULL, i_size, NULL);
  gst_buffer_fill (out_buf, 0, data, i_size);
  frame->output_buffer = out_buf;

  GST_LOG_OBJECT (encoder,
      "output: dts %" G_GINT64_FORMAT " pts %" G_GINT64_FORMAT,
      (gint64) pic_out.i_dts, (gint64) pic_out.i_pts);

  /* we want to know if x264 is messing around with this */
  g_assert (frame->pts == pic_out.i_pts);
  if (pic_out.b_keyframe) {
    /* expect dts == pts, and also positive ts,
     * so arrange for an offset if needed */
    if (pic_out.i_dts + encoder->dts_offset != pic_out.i_pts) {
      encoder->dts_offset = pic_out.i_pts - pic_out.i_dts;
      GST_DEBUG_OBJECT (encoder, "determined dts offset %" G_GINT64_FORMAT,
          encoder->dts_offset);
    }
  }

  frame->dts = pic_out.i_dts + encoder->dts_offset;
  /* should be ok now, surprise if not */
  if (frame->dts < 0) {
    GST_WARNING_OBJECT (encoder, "negative dts after offset compensation");
    frame->dts = GST_CLOCK_TIME_NONE;
  }

  if (pic_out.b_keyframe) {
    GST_DEBUG_OBJECT (encoder, "Output keyframe");
    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
  }

out:
  if (frame) {
    gst_x264_enc_dequeue_frame (encoder, frame);
    gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (encoder), frame);
  }

  return ret;
}

static void
gst_x264_enc_flush_frames (GstX264Enc * encoder, gboolean send)
{
  GstFlowReturn flow_ret;
  gint i_nal;

  /* first send the remaining frames */
  if (encoder->x264enc)
    do {
      flow_ret = gst_x264_enc_encode_frame (encoder, NULL, NULL, &i_nal, send);
    } while (flow_ret == GST_FLOW_OK
        && x264_encoder_delayed_frames (encoder->x264enc) > 0);
}

static gboolean
gst_x264_enc_stop (GstVideoEncoder * encoder)
{
  return gst_x264_enc_reset (encoder, TRUE);
}

static void
gst_x264_enc_reconfig (GstX264Enc * encoder)
{
  switch (encoder->pass) {
    case GST_X264_ENC_PASS_QUAL:
      encoder->x264param.rc.f_rf_constant = encoder->quantizer;
      encoder->x264param.rc.i_vbv_max_bitrate = encoder->bitrate;
      encoder->x264param.rc.i_vbv_buffer_size
          = encoder->x264param.rc.i_vbv_max_bitrate
          * encoder->vbv_buf_capacity / 1000;
      break;
    case GST_X264_ENC_PASS_CBR:
    case GST_X264_ENC_PASS_PASS1:
    case GST_X264_ENC_PASS_PASS2:
    case GST_X264_ENC_PASS_PASS3:
    default:
      encoder->x264param.rc.i_bitrate = encoder->bitrate;
      encoder->x264param.rc.i_vbv_max_bitrate = encoder->bitrate;
      encoder->x264param.rc.i_vbv_buffer_size
          = encoder->x264param.rc.i_vbv_max_bitrate
          * encoder->vbv_buf_capacity / 1000;
      break;
  }

  encoder->reconfig = TRUE;
}

static void
gst_x264_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstX264Enc *encoder;
  GstState state;

  const gchar *partitions = NULL;

  encoder = GST_X264_ENC (object);

  GST_OBJECT_LOCK (encoder);
  /* state at least matters for sps, bytestream, pass,
   * and so by extension ... */

  state = GST_STATE (encoder);
  if ((state != GST_STATE_READY && state != GST_STATE_NULL) &&
      !(pspec->flags & GST_PARAM_MUTABLE_PLAYING))
    goto wrong_state;

  switch (prop_id) {
    case ARG_PASS:
      encoder->pass = g_value_get_enum (value);
      break;
    case ARG_QUANTIZER:
      encoder->quantizer = g_value_get_uint (value);
      gst_x264_enc_reconfig (encoder);
      break;
    case ARG_BITRATE:
      encoder->bitrate = g_value_get_uint (value);
      gst_x264_enc_reconfig (encoder);
      break;
    case ARG_VBV_BUF_CAPACITY:
      encoder->vbv_buf_capacity = g_value_get_uint (value);
      gst_x264_enc_reconfig (encoder);
      break;
    case ARG_SPEED_PRESET:
      encoder->speed_preset = g_value_get_enum (value);
      break;
    case ARG_PSY_TUNE:
      encoder->psy_tune = g_value_get_enum (value);
      break;
    case ARG_TUNE:
      encoder->tune = g_value_get_flags (value);
      break;
    case ARG_OPTION_STRING:
      g_string_assign (encoder->option_string_prop, g_value_get_string (value));
      break;
    case ARG_THREADS:
      encoder->threads = g_value_get_uint (value);
      g_string_append_printf (encoder->option_string, ":threads=%d",
          encoder->threads);
      break;
    case ARG_SLICED_THREADS:
      encoder->sliced_threads = g_value_get_boolean (value);
      g_string_append_printf (encoder->option_string, ":sliced-threads=%d",
          encoder->sliced_threads);
      break;
    case ARG_SYNC_LOOKAHEAD:
      encoder->sync_lookahead = g_value_get_int (value);
      g_string_append_printf (encoder->option_string, ":sync-lookahead=%d",
          encoder->sync_lookahead);
      break;
    case ARG_MULTIPASS_CACHE_FILE:
      if (encoder->mp_cache_file)
        g_free (encoder->mp_cache_file);
      encoder->mp_cache_file = g_value_dup_string (value);
      g_string_append_printf (encoder->option_string, ":stats=%s",
          encoder->mp_cache_file);
      break;
    case ARG_BYTE_STREAM:
      encoder->byte_stream = g_value_get_boolean (value);
      g_string_append_printf (encoder->option_string, ":annexb=%d",
          encoder->byte_stream);
      break;
    case ARG_INTRA_REFRESH:
      encoder->intra_refresh = g_value_get_boolean (value);
      g_string_append_printf (encoder->option_string, ":intra-refresh=%d",
          encoder->intra_refresh);
      break;
    case ARG_ME:
      encoder->me = g_value_get_enum (value);
      g_string_append_printf (encoder->option_string, ":me=%s",
          x264_motion_est_names[encoder->me]);
      break;
    case ARG_SUBME:
      encoder->subme = g_value_get_uint (value);
      g_string_append_printf (encoder->option_string, ":subme=%d",
          encoder->subme);
      break;
    case ARG_ANALYSE:
      encoder->analyse = g_value_get_flags (value);
      partitions = gst_x264_enc_build_partitions (encoder->analyse);
      if (partitions) {
        g_string_append_printf (encoder->option_string, ":partitions=%s",
            partitions);
        g_free ((gpointer) partitions);
      }
      break;
    case ARG_DCT8x8:
      encoder->dct8x8 = g_value_get_boolean (value);
      g_string_append_printf (encoder->option_string, ":8x8dct=%d",
          encoder->dct8x8);
      break;
    case ARG_REF:
      encoder->ref = g_value_get_uint (value);
      g_string_append_printf (encoder->option_string, ":ref=%d", encoder->ref);
      break;
    case ARG_BFRAMES:
      encoder->bframes = g_value_get_uint (value);
      g_string_append_printf (encoder->option_string, ":bframes=%d",
          encoder->bframes);
      break;
    case ARG_B_ADAPT:
      encoder->b_adapt = g_value_get_boolean (value);
      g_string_append_printf (encoder->option_string, ":b-adapt=%d",
          encoder->b_adapt);
      break;
    case ARG_B_PYRAMID:
      encoder->b_pyramid = g_value_get_boolean (value);
      g_string_append_printf (encoder->option_string, ":b-pyramid=%s",
          x264_b_pyramid_names[encoder->b_pyramid]);
      break;
    case ARG_WEIGHTB:
      encoder->weightb = g_value_get_boolean (value);
      g_string_append_printf (encoder->option_string, ":weightb=%d",
          encoder->weightb);
      break;
    case ARG_SPS_ID:
      encoder->sps_id = g_value_get_uint (value);
      g_string_append_printf (encoder->option_string, ":sps-id=%d",
          encoder->sps_id);
      break;
    case ARG_AU_NALU:
      encoder->au_nalu = g_value_get_boolean (value);
      g_string_append_printf (encoder->option_string, ":aud=%d",
          encoder->au_nalu);
      break;
    case ARG_TRELLIS:
      encoder->trellis = g_value_get_boolean (value);
      g_string_append_printf (encoder->option_string, ":trellis=%d",
          encoder->trellis);
      break;
    case ARG_KEYINT_MAX:
      encoder->keyint_max = g_value_get_uint (value);
      g_string_append_printf (encoder->option_string, ":keyint=%d",
          encoder->keyint_max);
      break;
    case ARG_CABAC:
      encoder->cabac = g_value_get_boolean (value);
      g_string_append_printf (encoder->option_string, ":cabac=%d",
          encoder->cabac);
      break;
    case ARG_QP_MIN:
      encoder->qp_min = g_value_get_uint (value);
      g_string_append_printf (encoder->option_string, ":qpmin=%d",
          encoder->qp_min);
      break;
    case ARG_QP_MAX:
      encoder->qp_max = g_value_get_uint (value);
      g_string_append_printf (encoder->option_string, ":qpmax=%d",
          encoder->qp_max);
      break;
    case ARG_QP_STEP:
      encoder->qp_step = g_value_get_uint (value);
      g_string_append_printf (encoder->option_string, ":qpstep=%d",
          encoder->qp_step);
      break;
    case ARG_IP_FACTOR:
      encoder->ip_factor = g_value_get_float (value);
      g_string_append_printf (encoder->option_string, ":ip-factor=%f",
          encoder->ip_factor);
      break;
    case ARG_PB_FACTOR:
      encoder->pb_factor = g_value_get_float (value);
      g_string_append_printf (encoder->option_string, ":pb-factor=%f",
          encoder->pb_factor);
      break;
    case ARG_RC_MB_TREE:
      encoder->mb_tree = g_value_get_boolean (value);
      g_string_append_printf (encoder->option_string, ":mbtree=%d",
          encoder->mb_tree);
      break;
    case ARG_RC_LOOKAHEAD:
      encoder->rc_lookahead = g_value_get_int (value);
      g_string_append_printf (encoder->option_string, ":rc-lookahead=%d",
          encoder->rc_lookahead);
      break;
    case ARG_NR:
      encoder->noise_reduction = g_value_get_uint (value);
      g_string_append_printf (encoder->option_string, ":nr=%d",
          encoder->noise_reduction);
      break;
    case ARG_INTERLACED:
      encoder->interlaced = g_value_get_boolean (value);
      g_string_append_printf (encoder->option_string, ":interlaced=%d",
          encoder->interlaced);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (encoder);
  return;

  /* ERROR */
wrong_state:
  {
    GST_WARNING_OBJECT (encoder, "setting property in wrong state");
    GST_OBJECT_UNLOCK (encoder);
  }
}

static void
gst_x264_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstX264Enc *encoder;

  encoder = GST_X264_ENC (object);

  GST_OBJECT_LOCK (encoder);
  switch (prop_id) {
    case ARG_THREADS:
      g_value_set_uint (value, encoder->threads);
      break;
    case ARG_SLICED_THREADS:
      g_value_set_boolean (value, encoder->sliced_threads);
      break;
    case ARG_SYNC_LOOKAHEAD:
      g_value_set_int (value, encoder->sync_lookahead);
      break;
    case ARG_PASS:
      g_value_set_enum (value, encoder->pass);
      break;
    case ARG_QUANTIZER:
      g_value_set_uint (value, encoder->quantizer);
      break;
    case ARG_MULTIPASS_CACHE_FILE:
      g_value_set_string (value, encoder->mp_cache_file);
      break;
    case ARG_BYTE_STREAM:
      g_value_set_boolean (value, encoder->byte_stream);
      break;
    case ARG_BITRATE:
      g_value_set_uint (value, encoder->bitrate);
      break;
    case ARG_INTRA_REFRESH:
      g_value_set_boolean (value, encoder->intra_refresh);
      break;
    case ARG_VBV_BUF_CAPACITY:
      g_value_set_uint (value, encoder->vbv_buf_capacity);
      break;
    case ARG_ME:
      g_value_set_enum (value, encoder->me);
      break;
    case ARG_SUBME:
      g_value_set_uint (value, encoder->subme);
      break;
    case ARG_ANALYSE:
      g_value_set_flags (value, encoder->analyse);
      break;
    case ARG_DCT8x8:
      g_value_set_boolean (value, encoder->dct8x8);
      break;
    case ARG_REF:
      g_value_set_uint (value, encoder->ref);
      break;
    case ARG_BFRAMES:
      g_value_set_uint (value, encoder->bframes);
      break;
    case ARG_B_ADAPT:
      g_value_set_boolean (value, encoder->b_adapt);
      break;
    case ARG_B_PYRAMID:
      g_value_set_boolean (value, encoder->b_pyramid);
      break;
    case ARG_WEIGHTB:
      g_value_set_boolean (value, encoder->weightb);
      break;
    case ARG_SPS_ID:
      g_value_set_uint (value, encoder->sps_id);
      break;
    case ARG_AU_NALU:
      g_value_set_boolean (value, encoder->au_nalu);
      break;
    case ARG_TRELLIS:
      g_value_set_boolean (value, encoder->trellis);
      break;
    case ARG_KEYINT_MAX:
      g_value_set_uint (value, encoder->keyint_max);
      break;
    case ARG_QP_MIN:
      g_value_set_uint (value, encoder->qp_min);
      break;
    case ARG_QP_MAX:
      g_value_set_uint (value, encoder->qp_max);
      break;
    case ARG_QP_STEP:
      g_value_set_uint (value, encoder->qp_step);
      break;
    case ARG_CABAC:
      g_value_set_boolean (value, encoder->cabac);
      break;
    case ARG_IP_FACTOR:
      g_value_set_float (value, encoder->ip_factor);
      break;
    case ARG_PB_FACTOR:
      g_value_set_float (value, encoder->pb_factor);
      break;
    case ARG_RC_MB_TREE:
      g_value_set_boolean (value, encoder->mb_tree);
      break;
    case ARG_RC_LOOKAHEAD:
      g_value_set_int (value, encoder->rc_lookahead);
      break;
    case ARG_NR:
      g_value_set_uint (value, encoder->noise_reduction);
      break;
    case ARG_INTERLACED:
      g_value_set_boolean (value, encoder->interlaced);
      break;
    case ARG_SPEED_PRESET:
      g_value_set_enum (value, encoder->speed_preset);
      break;
    case ARG_PSY_TUNE:
      g_value_set_enum (value, encoder->psy_tune);
      break;
    case ARG_TUNE:
      g_value_set_flags (value, encoder->tune);
      break;
    case ARG_OPTION_STRING:
      g_value_set_string (value, encoder->option_string_prop->str);
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
  GST_DEBUG_CATEGORY_INIT (x264_enc_debug, "x264enc", 0,
      "h264 encoding element");

  GST_INFO ("x264 build: %u", X264_BUILD);

  return gst_element_register (plugin, "x264enc",
      GST_RANK_PRIMARY, GST_TYPE_X264_ENC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    x264,
    "libx264-based H264 plugins",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
