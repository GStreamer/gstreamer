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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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
 * in which case the #GstX264Enc:quantizer property controls much of the outcome.
 *
 * The H264 profile that is eventually used depends on a few settings.
 * If #GstX264Enc:dct8x8 is enabled, then High profile is used.
 * Otherwise, if #GstX264Enc:cabac entropy coding is enabled or #GstX264Enc:bframes
 * are allowed, then Main Profile is in effect, and otherwise Baseline profile
 * applies.  As such, Main is presently the default profile, which is fine for
 * most players and settings, but in some cases (e.g. hardware platforms)
 * a more restricted profile/level may be necessary.
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
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstx264enc.h"

#if X264_BUILD >= 76
#define X264_ENC_NALS 1
#endif

#include <string.h>
#include <stdlib.h>

GST_DEBUG_CATEGORY_STATIC (x264_enc_debug);
#define GST_CAT_DEFAULT x264_enc_debug

enum
{
  ARG_0,
  ARG_THREADS,
  ARG_PASS,
  ARG_QUANTIZER,
  ARG_STATS_FILE,
  ARG_MULTIPASS_CACHE_FILE,
  ARG_BYTE_STREAM,
  ARG_BITRATE,
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
  ARG_NR,
  ARG_INTERLACED
};

#define ARG_THREADS_DEFAULT            1
#define ARG_PASS_DEFAULT               0
#define ARG_QUANTIZER_DEFAULT          21
#define ARG_MULTIPASS_CACHE_FILE_DEFAULT "x264.log"
#define ARG_STATS_FILE_DEFAULT         ARG_MULTIPASS_CACHE_FILE_DEFAULT
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
    {GST_X264_ENC_PASS_QUANT, "Constant Quantizer", "quant"},
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
  static const GEnumValue me_types[] = {
    {X264_ME_DIA, "diamond search, radius 1 (fast)", "dia"},
    {X264_ME_HEX, "hexagonal search, radius 2", "hex"},
    {X264_ME_UMH, "uneven multi-hexagon search", "umh"},
    {X264_ME_ESA, "exhaustive search (slow)", "esa"},
    {0, NULL, NULL}
  };

  if (!me_type) {
    me_type = g_enum_register_static ("GstX264EncMe", me_types);
  }
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

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) I420, "
        "framerate = (fraction) [0, MAX], "
        "width = (int) [ 16, MAX ], " "height = (int) [ 16, MAX ]")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ], "
        "stream-format = (string) { byte-stream, avc }")
    );

static void gst_x264_enc_finalize (GObject * object);
static void gst_x264_enc_reset (GstX264Enc * encoder);

static gboolean gst_x264_enc_init_encoder (GstX264Enc * encoder);
static void gst_x264_enc_close_encoder (GstX264Enc * encoder);

static gboolean gst_x264_enc_sink_set_caps (GstPad * pad, GstCaps * caps);
static gboolean gst_x264_enc_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_x264_enc_src_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_x264_enc_chain (GstPad * pad, GstBuffer * buf);
static void gst_x264_enc_flush_frames (GstX264Enc * encoder, gboolean send);
static GstFlowReturn gst_x264_enc_encode_frame (GstX264Enc * encoder,
    x264_picture_t * pic_in, int *i_nal, gboolean send);
static GstStateChangeReturn gst_x264_enc_change_state (GstElement * element,
    GstStateChange transition);

static void gst_x264_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_x264_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
_do_init (GType object_type)
{
  const GInterfaceInfo preset_interface_info = {
    NULL,                       /* interface_init */
    NULL,                       /* interface_finalize */
    NULL                        /* interface_data */
  };

  g_type_add_interface_static (object_type, GST_TYPE_PRESET,
      &preset_interface_info);
}

GST_BOILERPLATE_FULL (GstX264Enc, gst_x264_enc, GstElement, GST_TYPE_ELEMENT,
    _do_init);

static void
gst_x264_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "x264enc", "Codec/Encoder/Video", "H264 Encoder",
      "Josef Zlomek <josef.zlomek@itonis.tv>, "
      "Mark Nauwelaerts <mnauw@users.sf.net>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

static void
gst_x264_enc_class_init (GstX264EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_x264_enc_set_property;
  gobject_class->get_property = gst_x264_enc_get_property;
  gobject_class->finalize = gst_x264_enc_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_x264_enc_change_state);

  g_object_class_install_property (gobject_class, ARG_THREADS,
      g_param_spec_uint ("threads", "Threads",
          "Number of threads used by the codec (0 for automatic)",
          0, 4, ARG_THREADS_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_PASS,
      g_param_spec_enum ("pass", "Encoding pass/type",
          "Encoding pass/type", GST_X264_ENC_PASS_TYPE,
          ARG_PASS_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_QUANTIZER,
      g_param_spec_uint ("quantizer", "Constant Quantizer",
          "Constant quantizer or quality to apply",
          1, 50, ARG_QUANTIZER_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_STATS_FILE,
      g_param_spec_string ("stats-file", "Stats File",
          "Filename for multipass statistics (deprecated, use multipass-stats-file)",
          ARG_STATS_FILE_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MULTIPASS_CACHE_FILE,
      g_param_spec_string ("multipass-cache-file", "Multipass Cache File",
          "Filename for multipass cache file",
          ARG_MULTIPASS_CACHE_FILE_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BYTE_STREAM,
      g_param_spec_boolean ("byte-stream", "Byte Stream",
          "Generate byte stream format of NALU",
          ARG_BYTE_STREAM_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate", "Bitrate in kbit/sec", 1,
          100 * 1024, ARG_BITRATE_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_VBV_BUF_CAPACITY,
      g_param_spec_uint ("vbv-buf-capacity", "VBV buffer capacity",
          "Size of the VBV buffer in milliseconds",
          300, 10000, ARG_VBV_BUF_CAPACITY_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_ME,
      g_param_spec_enum ("me", "Motion Estimation",
          "Integer pixel motion estimation method", GST_X264_ENC_ME_TYPE,
          ARG_ME_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_SUBME,
      g_param_spec_uint ("subme", "Subpixel Motion Estimation",
          "Subpixel motion estimation and partition decision quality: 1=fast, 6=best",
          1, 6, ARG_SUBME_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_ANALYSE,
      g_param_spec_flags ("analyse", "Analyse", "Partitions to consider",
          GST_X264_ENC_ANALYSE_TYPE, ARG_ANALYSE_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_DCT8x8,
      g_param_spec_boolean ("dct8x8", "DCT8x8",
          "Adaptive spatial transform size",
          ARG_DCT8x8_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_REF,
      g_param_spec_uint ("ref", "Reference Frames",
          "Number of reference frames",
          1, 12, ARG_REF_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BFRAMES,
      g_param_spec_uint ("bframes", "B-Frames",
          "Number of B-frames between I and P",
          0, 4, ARG_BFRAMES_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_B_ADAPT,
      g_param_spec_boolean ("b-adapt", "B-Adapt",
          "Automatically decide how many B-frames to use",
          ARG_B_ADAPT_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_B_PYRAMID,
      g_param_spec_boolean ("b-pyramid", "B-Pyramid",
          "Keep some B-frames as references", ARG_B_PYRAMID_DEFAULT,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_WEIGHTB,
      g_param_spec_boolean ("weightb", "Weighted B-Frames",
          "Weighted prediction for B-frames", ARG_WEIGHTB_DEFAULT,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_SPS_ID,
      g_param_spec_uint ("sps-id", "SPS ID",
          "SPS and PPS ID number",
          0, 31, ARG_SPS_ID_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_AU_NALU,
      g_param_spec_boolean ("aud", "AUD",
          "Use AU (Access Unit) delimiter", ARG_AU_NALU_DEFAULT,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_TRELLIS,
      g_param_spec_boolean ("trellis", "Trellis quantization",
          "Enable trellis searched quantization", ARG_TRELLIS_DEFAULT,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_KEYINT_MAX,
      g_param_spec_uint ("key-int-max", "Key-frame maximal interval",
          "Maximal distance between two key-frames (0 for automatic)",
          0, G_MAXINT, ARG_KEYINT_MAX_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_CABAC,
      g_param_spec_boolean ("cabac", "Use CABAC",
          "Enable CABAC entropy coding", ARG_CABAC_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_QP_MIN,
      g_param_spec_uint ("qp-min", "Minimum Quantizer",
          "Minimum quantizer", 1, 51, ARG_QP_MIN_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_QP_MAX,
      g_param_spec_uint ("qp-max", "Maximum Quantizer",
          "Maximum quantizer", 1, 51, ARG_QP_MAX_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_QP_STEP,
      g_param_spec_uint ("qp-step", "Maximum Quantizer Difference",
          "Maximum quantizer difference between frames",
          1, 50, ARG_QP_STEP_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_IP_FACTOR,
      g_param_spec_float ("ip-factor", "IP-Factor",
          "Quantizer factor between I- and P-frames",
          0, 2, ARG_IP_FACTOR_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_PB_FACTOR,
      g_param_spec_float ("pb-factor", "PB-Factor",
          "Quantizer factor between P- and B-frames",
          0, 2, ARG_PB_FACTOR_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_NR,
      g_param_spec_uint ("noise-reduction", "Noise Reducation",
          "Noise reduction strength",
          0, 100000, ARG_NR_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_INTERLACED,
      g_param_spec_boolean ("interlaced", "Interlaced",
          "Interlaced material", ARG_INTERLACED_DEFAULT, G_PARAM_READWRITE));
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
gst_x264_enc_init (GstX264Enc * encoder, GstX264EncClass * klass)
{
  encoder->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (encoder->sinkpad,
      GST_DEBUG_FUNCPTR (gst_x264_enc_sink_set_caps));
  gst_pad_set_event_function (encoder->sinkpad,
      GST_DEBUG_FUNCPTR (gst_x264_enc_sink_event));
  gst_pad_set_chain_function (encoder->sinkpad,
      GST_DEBUG_FUNCPTR (gst_x264_enc_chain));
  gst_element_add_pad (GST_ELEMENT (encoder), encoder->sinkpad);

  encoder->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_use_fixed_caps (encoder->srcpad);
  gst_element_add_pad (GST_ELEMENT (encoder), encoder->srcpad);

  gst_pad_set_event_function (encoder->srcpad,
      GST_DEBUG_FUNCPTR (gst_x264_enc_src_event));

  /* properties */
  encoder->threads = ARG_THREADS_DEFAULT;
  encoder->pass = ARG_PASS_DEFAULT;
  encoder->quantizer = ARG_QUANTIZER_DEFAULT;
  encoder->mp_cache_file = g_strdup (ARG_MULTIPASS_CACHE_FILE_DEFAULT);
  encoder->byte_stream = ARG_BYTE_STREAM_DEFAULT;
  encoder->bitrate = ARG_BITRATE_DEFAULT;
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
  encoder->noise_reduction = ARG_NR_DEFAULT;
  encoder->interlaced = ARG_INTERLACED_DEFAULT;

  /* resources */
  encoder->delay = g_queue_new ();
  encoder->buffer_size = 100000;
  encoder->buffer = g_malloc (encoder->buffer_size);

  x264_param_default (&encoder->x264param);

  /* log callback setup; part of parameters */
  encoder->x264param.pf_log = gst_x264_enc_log_callback;
  encoder->x264param.p_log_private = encoder;
  encoder->x264param.i_log_level = X264_LOG_DEBUG;

  gst_x264_enc_reset (encoder);
}

static void
gst_x264_enc_reset (GstX264Enc * encoder)
{
  encoder->x264enc = NULL;
  encoder->width = 0;
  encoder->height = 0;

  GST_OBJECT_LOCK (encoder);
  encoder->i_type = X264_TYPE_AUTO;
  if (encoder->forcekeyunit_event)
    gst_event_unref (encoder->forcekeyunit_event);
  encoder->forcekeyunit_event = NULL;
  GST_OBJECT_UNLOCK (encoder);
}

static void
gst_x264_enc_finalize (GObject * object)
{
  GstX264Enc *encoder = GST_X264_ENC (object);

  g_free (encoder->mp_cache_file);
  encoder->mp_cache_file = NULL;
  g_free (encoder->buffer);
  encoder->buffer = NULL;
  g_queue_free (encoder->delay);
  encoder->delay = NULL;

  gst_x264_enc_close_encoder (encoder);

  G_OBJECT_CLASS (parent_class)->finalize (object);
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

  /* make sure that the encoder is closed */
  gst_x264_enc_close_encoder (encoder);

  GST_OBJECT_LOCK (encoder);

  /* set up encoder parameters */
  encoder->x264param.i_threads = encoder->threads;
  encoder->x264param.i_fps_num = encoder->fps_num;
  encoder->x264param.i_fps_den = encoder->fps_den;
  encoder->x264param.i_width = encoder->width;
  encoder->x264param.i_height = encoder->height;
  if (encoder->par_den > 0) {
    encoder->x264param.vui.i_sar_width = encoder->par_num;
    encoder->x264param.vui.i_sar_height = encoder->par_den;
  }
  /* FIXME 0.11 : 2s default keyframe interval seems excessive
   * (10s is x264 default) */
  encoder->x264param.i_keyint_max = encoder->keyint_max ? encoder->keyint_max :
      (2 * encoder->fps_num / encoder->fps_den);
  encoder->x264param.b_cabac = encoder->cabac;
  encoder->x264param.b_aud = encoder->au_nalu;
  encoder->x264param.i_sps_id = encoder->sps_id;
  if ((((encoder->height == 576) && ((encoder->width == 720)
                  || (encoder->width == 704) || (encoder->width == 352)))
          || ((encoder->height == 288) && (encoder->width == 352)))
      && (encoder->fps_den == 1) && (encoder->fps_num == 25)) {
    encoder->x264param.vui.i_vidformat = 1;     /* PAL */
  } else if ((((encoder->height == 480) && ((encoder->width == 720)
                  || (encoder->width == 704) || (encoder->width == 352)))
          || ((encoder->height == 240) && (encoder->width == 352)))
      && (encoder->fps_den == 1001) && ((encoder->fps_num == 30000)
          || (encoder->fps_num == 24000))) {
    encoder->x264param.vui.i_vidformat = 2;     /* NTSC */
  } else
    encoder->x264param.vui.i_vidformat = 5;     /* unspecified */
  encoder->x264param.analyse.i_trellis = encoder->trellis ? 1 : 0;
  encoder->x264param.analyse.b_psnr = 0;
  encoder->x264param.analyse.i_me_method = encoder->me;
  encoder->x264param.analyse.i_subpel_refine = encoder->subme;
  encoder->x264param.analyse.inter = encoder->analyse;
  encoder->x264param.analyse.b_transform_8x8 = encoder->dct8x8;
  encoder->x264param.analyse.b_weighted_bipred = encoder->weightb;
  encoder->x264param.analyse.i_weighted_pred = 0;
  encoder->x264param.analyse.i_noise_reduction = encoder->noise_reduction;
  encoder->x264param.i_frame_reference = encoder->ref;
  encoder->x264param.i_bframe = encoder->bframes;
#if X264_BUILD < 78
  encoder->x264param.b_bframe_pyramid = encoder->b_pyramid;
#else
  encoder->x264param.i_bframe_pyramid =
      encoder->b_pyramid ? X264_B_PYRAMID_NORMAL : X264_B_PYRAMID_NONE;
#endif
#if X264_BUILD < 63
  encoder->x264param.b_bframe_adaptive = encoder->b_adapt;
#else
  encoder->x264param.i_bframe_adaptive =
      encoder->b_adapt ? X264_B_ADAPT_FAST : X264_B_ADAPT_NONE;
#endif
  encoder->x264param.b_interlaced = encoder->interlaced;
  encoder->x264param.b_deblocking_filter = 1;
  encoder->x264param.i_deblocking_filter_alphac0 = 0;
  encoder->x264param.i_deblocking_filter_beta = 0;
  encoder->x264param.rc.f_ip_factor = encoder->ip_factor;
  encoder->x264param.rc.f_pb_factor = encoder->pb_factor;
#ifdef X264_ENC_NALS
  encoder->x264param.b_annexb = encoder->byte_stream;
#endif

  switch (encoder->pass) {
    case GST_X264_ENC_PASS_QUANT:
      encoder->x264param.rc.i_rc_method = X264_RC_CQP;
      encoder->x264param.rc.i_qp_constant = encoder->quantizer;
      break;
    case GST_X264_ENC_PASS_QUAL:
      encoder->x264param.rc.i_rc_method = X264_RC_CRF;
      encoder->x264param.rc.f_rf_constant = encoder->quantizer;
      break;
    case GST_X264_ENC_PASS_CBR:
    case GST_X264_ENC_PASS_PASS1:
    case GST_X264_ENC_PASS_PASS2:
    case GST_X264_ENC_PASS_PASS3:
    default:
      encoder->x264param.rc.i_rc_method = X264_RC_ABR;
      encoder->x264param.rc.i_bitrate = encoder->bitrate;
      encoder->x264param.rc.i_vbv_max_bitrate = encoder->bitrate;
      encoder->x264param.rc.i_vbv_buffer_size
          = encoder->x264param.rc.i_vbv_max_bitrate
          * encoder->vbv_buf_capacity / 1000;
      encoder->x264param.rc.i_qp_min = encoder->qp_min;
      encoder->x264param.rc.i_qp_max = encoder->qp_max;
      encoder->x264param.rc.i_qp_step = encoder->qp_step;
      pass = encoder->pass & 0xF;
      break;
  }

  switch (pass) {
    case 0:
      encoder->x264param.rc.b_stat_read = 0;
      encoder->x264param.rc.b_stat_write = 0;
      break;
    case 1:
      /* Turbo mode parameters. */
      encoder->x264param.i_frame_reference = (encoder->ref + 1) >> 1;
      encoder->x264param.analyse.i_subpel_refine =
          CLAMP (encoder->subme - 1, 1, 3);
      encoder->x264param.analyse.inter &= ~X264_ANALYSE_PSUB8x8;
      encoder->x264param.analyse.inter &= ~X264_ANALYSE_BSUB16x16;
      encoder->x264param.analyse.i_trellis = 0;

      encoder->x264param.rc.b_stat_read = 0;
      encoder->x264param.rc.b_stat_write = 1;
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
  encoder->x264param.rc.psz_stat_in = encoder->mp_cache_file;
  encoder->x264param.rc.psz_stat_out = encoder->mp_cache_file;

  GST_OBJECT_UNLOCK (encoder);

  encoder->x264enc = x264_encoder_open (&encoder->x264param);
  if (!encoder->x264enc) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE,
        ("Can not initialize x264 encoder."), (NULL));
    return FALSE;
  }

  return TRUE;
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
#ifndef X264_ENC_NALS
  int i_data;
#endif
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

  /* old style API: nal's are not encapsulated, and have no sync/size prefix,
   * new style API: nal's are encapsulated, and have 4-byte size prefix */
#ifndef X264_ENC_NALS
  sps = nal[sps_ni].p_payload;
#else
  sps = nal[sps_ni].p_payload + 4;
  /* skip NAL unit type */
  sps++;
#endif

  buffer[0] = 1;                /* AVC Decoder Configuration Record ver. 1 */
  buffer[1] = sps[0];           /* profile_idc                             */
  buffer[2] = sps[1];           /* profile_compability                     */
  buffer[3] = sps[2];           /* level_idc                               */
  buffer[4] = 0xfc | (4 - 1);   /* nal_length_size_minus1                  */

  i_size = 5;

  buffer[i_size++] = 0xe0 | 1;  /* number of SPSs */

#ifndef X264_ENC_NALS
  i_data = buffer_size - i_size - 2;
  nal_size = x264_nal_encode (buffer + i_size + 2, &i_data, 0, &nal[sps_ni]);
#else
  nal_size = nal[sps_ni].i_payload - 4;
  memcpy (buffer + i_size + 2, nal[sps_ni].p_payload + 4, nal_size);
#endif
  GST_WRITE_UINT16_BE (buffer + i_size, nal_size);
  i_size += nal_size + 2;

  buffer[i_size++] = 1;         /* number of PPSs */

#ifndef X264_ENC_NALS
  i_data = buffer_size - i_size - 2;
  nal_size = x264_nal_encode (buffer + i_size + 2, &i_data, 0, &nal[pps_ni]);
#else
  nal_size = nal[pps_ni].i_payload - 4;
  memcpy (buffer + i_size + 2, nal[pps_ni].p_payload + 4, nal_size);
#endif
  GST_WRITE_UINT16_BE (buffer + i_size, nal_size);
  i_size += nal_size + 2;

  buf = gst_buffer_new_and_alloc (i_size);
  memcpy (GST_BUFFER_DATA (buf), buffer, i_size);
  g_free (buffer);

  GST_MEMDUMP ("header", GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

  return buf;
}

/* gst_x264_enc_set_src_caps
 * Returns: TRUE on success.
 */
static gboolean
gst_x264_enc_set_src_caps (GstX264Enc * encoder, GstPad * pad, GstCaps * caps)
{
  GstBuffer *buf;
  GstCaps *outcaps;
  GstStructure *structure;
  gboolean res;

  outcaps = gst_caps_new_simple ("video/x-h264",
      "width", G_TYPE_INT, encoder->width,
      "height", G_TYPE_INT, encoder->height,
      "framerate", GST_TYPE_FRACTION, encoder->fps_num, encoder->fps_den,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, encoder->par_num,
      encoder->par_den, NULL);

  structure = gst_caps_get_structure (outcaps, 0);

  if (!encoder->byte_stream) {
    buf = gst_x264_enc_header_buf (encoder);
    if (buf != NULL) {
      gst_caps_set_simple (outcaps, "codec_data", GST_TYPE_BUFFER, buf, NULL);
      gst_buffer_unref (buf);
    }
    gst_structure_set (structure, "stream-format", G_TYPE_STRING, "avc", NULL);
  } else {
    gst_structure_set (structure, "stream-format", G_TYPE_STRING, "byte-stream",
        NULL);
  }

  res = gst_pad_set_caps (pad, outcaps);
  gst_caps_unref (outcaps);

  return res;
}

static gboolean
gst_x264_enc_sink_set_caps (GstPad * pad, GstCaps * caps)
{
  GstX264Enc *encoder = GST_X264_ENC (GST_OBJECT_PARENT (pad));
  gint width, height;
  gint fps_num, fps_den;
  gint par_num, par_den;
  gint i;

  /* get info from caps */
  /* only I420 supported for now; so apparently claims x264enc ? */
  if (!gst_video_format_parse_caps (caps, &encoder->format, &width, &height) ||
      encoder->format != GST_VIDEO_FORMAT_I420)
    return FALSE;
  if (!gst_video_parse_caps_framerate (caps, &fps_num, &fps_den))
    return FALSE;
  if (!gst_video_parse_caps_pixel_aspect_ratio (caps, &par_num, &par_den)) {
    par_num = 1;
    par_den = 1;
  }

  /* If the encoder is initialized, do not
     reinitialize it again if not necessary */
  if (encoder->x264enc) {
    if (width == encoder->width && height == encoder->height
        && fps_num == encoder->fps_num && fps_den == encoder->fps_den
        && par_num == encoder->par_num && par_den == encoder->par_den)
      return TRUE;

    /* clear out pending frames */
    gst_x264_enc_flush_frames (encoder, TRUE);

    encoder->sps_id++;
  }

  /* store input description */
  encoder->width = width;
  encoder->height = height;
  encoder->fps_num = fps_num;
  encoder->fps_den = fps_den;
  encoder->par_num = par_num;
  encoder->par_den = par_den;

  /* prepare a cached image description  */
  encoder->image_size = gst_video_format_get_size (encoder->format, width,
      height);
  for (i = 0; i < 3; ++i) {
    /* only offsets now, is shifted later */
    encoder->offset[i] = gst_video_format_get_component_offset (encoder->format,
        i, width, height);
    encoder->stride[i] = gst_video_format_get_row_stride (encoder->format,
        i, width);
  }

  if (!gst_x264_enc_init_encoder (encoder))
    return FALSE;

  if (!gst_x264_enc_set_src_caps (encoder, encoder->srcpad, caps)) {
    gst_x264_enc_close_encoder (encoder);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_x264_enc_src_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = TRUE;
  GstX264Enc *encoder;
  gboolean forward = TRUE;

  encoder = GST_X264_ENC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_UPSTREAM:{
      const GstStructure *s;
      s = gst_event_get_structure (event);
      if (gst_structure_has_name (s, "GstForceKeyUnit")) {
        /* Set I frame request */
        GST_OBJECT_LOCK (encoder);
        encoder->i_type = X264_TYPE_I;
        encoder->forcekeyunit_event = gst_event_copy (event);
        GST_EVENT_TYPE (encoder->forcekeyunit_event) =
            GST_EVENT_CUSTOM_DOWNSTREAM;
        GST_OBJECT_UNLOCK (encoder);
        forward = FALSE;
        gst_event_unref (event);
      }
      break;
    }
    default:
      break;
  }

  if (forward)
    ret = gst_pad_push_event (encoder->sinkpad, event);

  gst_object_unref (encoder);
  return ret;
}

static gboolean
gst_x264_enc_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret;
  GstX264Enc *encoder;

  encoder = GST_X264_ENC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_x264_enc_flush_frames (encoder, TRUE);
      break;
      /* no flushing if flush received,
       * buffers in encoder are considered (in the) past */
    case GST_EVENT_CUSTOM_DOWNSTREAM:{
      const GstStructure *s;
      s = gst_event_get_structure (event);
      if (gst_structure_has_name (s, "GstForceKeyUnit")) {
        GST_OBJECT_LOCK (encoder);
        encoder->i_type = X264_TYPE_I;
        GST_OBJECT_UNLOCK (encoder);
      }
      break;
    }
    default:
      break;
  }

  ret = gst_pad_push_event (encoder->srcpad, event);

  gst_object_unref (encoder);
  return ret;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_x264_enc_chain (GstPad * pad, GstBuffer * buf)
{
  GstX264Enc *encoder = GST_X264_ENC (GST_OBJECT_PARENT (pad));
  GstFlowReturn ret;
  x264_picture_t pic_in;
  gint i_nal, i;
  if (G_UNLIKELY (encoder->x264enc == NULL))
    goto not_inited;

  /* create x264_picture_t from the buffer */
  /* mostly taken from mplayer (file ve_x264.c) */
  if (G_UNLIKELY (GST_BUFFER_SIZE (buf) < encoder->image_size))
    goto wrong_buffer_size;

  /* remember the timestamp and duration */
  g_queue_push_tail (encoder->delay, buf);

  /* set up input picture */
  memset (&pic_in, 0, sizeof (pic_in));

  pic_in.img.i_csp = X264_CSP_I420;
  pic_in.img.i_plane = 3;
  for (i = 0; i < 3; i++) {
    pic_in.img.plane[i] = GST_BUFFER_DATA (buf) + encoder->offset[i];
    pic_in.img.i_stride[i] = encoder->stride[i];
  }

  GST_OBJECT_LOCK (encoder);
  pic_in.i_type = encoder->i_type;

  /* Reset encoder forced picture type */
  encoder->i_type = X264_TYPE_AUTO;
  GST_OBJECT_UNLOCK (encoder);

  pic_in.i_pts = GST_BUFFER_TIMESTAMP (buf);

  ret = gst_x264_enc_encode_frame (encoder, &pic_in, &i_nal, TRUE);

  /* input buffer is released later on */
  return ret;

/* ERRORS */
not_inited:
  {
    GST_WARNING_OBJECT (encoder, "Got buffer before set_caps was called");
    gst_buffer_unref (buf);
    return GST_FLOW_NOT_NEGOTIATED;
  }
wrong_buffer_size:
  {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE,
        ("Encode x264 frame failed."),
        ("Wrong buffer size %d (should be %d)",
            GST_BUFFER_SIZE (buf), encoder->image_size));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_x264_enc_encode_frame (GstX264Enc * encoder, x264_picture_t * pic_in,
    int *i_nal, gboolean send)
{
  GstBuffer *out_buf = NULL, *in_buf = NULL;
  x264_picture_t pic_out;
  x264_nal_t *nal;
  int i_size;
#ifndef X264_ENC_NALS
  int nal_size;
  gint i;
#endif
  int encoder_return;
  GstFlowReturn ret;
  GstClockTime timestamp;
  GstClockTime duration;
  guint8 *data;
  GstEvent *forcekeyunit_event = NULL;

  if (G_UNLIKELY (encoder->x264enc == NULL))
    return GST_FLOW_NOT_NEGOTIATED;

  encoder_return = x264_encoder_encode (encoder->x264enc,
      &nal, i_nal, pic_in, &pic_out);

  if (encoder_return < 0) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE, ("Encode x264 frame failed."),
        ("x264_encoder_encode return code=%d", encoder_return));
    return GST_FLOW_ERROR;
  }

  if (!*i_nal) {
    return GST_FLOW_OK;
  }
#ifndef X264_ENC_NALS
  i_size = 0;
  for (i = 0; i < *i_nal; i++) {
    gint i_data = encoder->buffer_size - i_size - 4;

    if (i_data < nal[i].i_payload * 2) {
      encoder->buffer_size += 2 * nal[i].i_payload;
      encoder->buffer = g_realloc (encoder->buffer, encoder->buffer_size);
      i_data = encoder->buffer_size - i_size - 4;
    }

    nal_size =
        x264_nal_encode (encoder->buffer + i_size + 4, &i_data, 0, &nal[i]);
    if (encoder->byte_stream)
      GST_WRITE_UINT32_BE (encoder->buffer + i_size, 1);
    else
      GST_WRITE_UINT32_BE (encoder->buffer + i_size, nal_size);

    i_size += nal_size + 4;
  }
  data = encoder->buffer;
#else
  i_size = encoder_return;
  data = nal[0].p_payload;
#endif

  in_buf = g_queue_pop_head (encoder->delay);
  if (in_buf) {
    timestamp = GST_BUFFER_TIMESTAMP (in_buf);
    duration = GST_BUFFER_DURATION (in_buf);
    gst_buffer_unref (in_buf);
  } else {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE, (NULL),
        ("Timestamp queue empty."));
    return GST_FLOW_ERROR;
  }

  if (!send)
    return GST_FLOW_OK;

  ret = gst_pad_alloc_buffer (encoder->srcpad, GST_BUFFER_OFFSET_NONE,
      i_size, GST_PAD_CAPS (encoder->srcpad), &out_buf);
  if (ret != GST_FLOW_OK)
    return ret;

  memcpy (GST_BUFFER_DATA (out_buf), data, i_size);
  GST_BUFFER_SIZE (out_buf) = i_size;

  /* PTS */
  /* FIXME ??: maybe use DTS here, since:
   * - it is so practiced by other encoders,
   * - downstream (e.g. muxers) might not enjoy non-monotone timestamps,
   *   whereas a decoder can also deal with DTS */
  GST_BUFFER_TIMESTAMP (out_buf) = pic_out.i_pts;
  GST_BUFFER_DURATION (out_buf) = duration;

  if (pic_out.i_type == X264_TYPE_IDR) {
    GST_BUFFER_FLAG_UNSET (out_buf, GST_BUFFER_FLAG_DELTA_UNIT);
  } else {
    GST_BUFFER_FLAG_SET (out_buf, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  GST_OBJECT_LOCK (encoder);
  forcekeyunit_event = encoder->forcekeyunit_event;
  encoder->forcekeyunit_event = NULL;
  GST_OBJECT_UNLOCK (encoder);
  if (forcekeyunit_event) {
    gst_structure_set (forcekeyunit_event->structure,
        "timestamp", G_TYPE_UINT64, GST_BUFFER_TIMESTAMP (out_buf), NULL);
    gst_pad_push_event (encoder->srcpad, forcekeyunit_event);
  }

  return gst_pad_push (encoder->srcpad, out_buf);
}

static void
gst_x264_enc_flush_frames (GstX264Enc * encoder, gboolean send)
{
  GstFlowReturn flow_ret;
  gint i_nal;

  /* first send the remaining frames */
  if (encoder->x264enc)
    do {
      flow_ret = gst_x264_enc_encode_frame (encoder, NULL, &i_nal, send);
    } while (flow_ret == GST_FLOW_OK && i_nal > 0);

  /* in any case, make sure the delay queue is emptied */
  while (!g_queue_is_empty (encoder->delay))
    gst_buffer_unref (g_queue_pop_head (encoder->delay));
}

static GstStateChangeReturn
gst_x264_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstX264Enc *encoder = GST_X264_ENC (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  ret = parent_class->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto out;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_x264_enc_flush_frames (encoder, FALSE);
      gst_x264_enc_close_encoder (encoder);
      gst_x264_enc_reset (encoder);
      break;
    default:
      break;
  }

out:
  return ret;
}

static void
gst_x264_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstX264Enc *encoder;
  GstState state;

  encoder = GST_X264_ENC (object);

  GST_OBJECT_LOCK (encoder);
  /* state at least matters for sps, bytestream, pass,
   * and so by extension ... */
  state = GST_STATE (encoder);
  if (state != GST_STATE_READY && state != GST_STATE_NULL)
    goto wrong_state;

  switch (prop_id) {
    case ARG_THREADS:
      encoder->threads = g_value_get_uint (value);
      break;
    case ARG_PASS:
      encoder->pass = g_value_get_enum (value);
      break;
    case ARG_QUANTIZER:
      encoder->quantizer = g_value_get_uint (value);
      break;
    case ARG_STATS_FILE:
    case ARG_MULTIPASS_CACHE_FILE:
      if (encoder->mp_cache_file)
        g_free (encoder->mp_cache_file);
      encoder->mp_cache_file = g_value_dup_string (value);
      break;
    case ARG_BYTE_STREAM:
      encoder->byte_stream = g_value_get_boolean (value);
      break;
    case ARG_BITRATE:
      encoder->bitrate = g_value_get_uint (value);
      break;
    case ARG_VBV_BUF_CAPACITY:
      encoder->vbv_buf_capacity = g_value_get_uint (value);
      break;
    case ARG_ME:
      encoder->me = g_value_get_enum (value);
      break;
    case ARG_SUBME:
      encoder->subme = g_value_get_uint (value);
      break;
    case ARG_ANALYSE:
      encoder->analyse = g_value_get_flags (value);
      break;
    case ARG_DCT8x8:
      encoder->dct8x8 = g_value_get_boolean (value);
      break;
    case ARG_REF:
      encoder->ref = g_value_get_uint (value);
      break;
    case ARG_BFRAMES:
      encoder->bframes = g_value_get_uint (value);
      break;
    case ARG_B_ADAPT:
      encoder->b_adapt = g_value_get_boolean (value);
      break;
    case ARG_B_PYRAMID:
      encoder->b_pyramid = g_value_get_boolean (value);
      break;
    case ARG_WEIGHTB:
      encoder->weightb = g_value_get_boolean (value);
      break;
    case ARG_SPS_ID:
      encoder->sps_id = g_value_get_uint (value);
      break;
    case ARG_AU_NALU:
      encoder->au_nalu = g_value_get_boolean (value);
      break;
    case ARG_TRELLIS:
      encoder->trellis = g_value_get_boolean (value);
      break;
    case ARG_KEYINT_MAX:
      encoder->keyint_max = g_value_get_uint (value);
      break;
    case ARG_CABAC:
      encoder->cabac = g_value_get_boolean (value);
      break;
    case ARG_QP_MIN:
      encoder->qp_min = g_value_get_uint (value);
      break;
    case ARG_QP_MAX:
      encoder->qp_max = g_value_get_uint (value);
      break;
    case ARG_QP_STEP:
      encoder->qp_step = g_value_get_uint (value);
      break;
    case ARG_IP_FACTOR:
      encoder->ip_factor = g_value_get_float (value);
      break;
    case ARG_PB_FACTOR:
      encoder->pb_factor = g_value_get_float (value);
      break;
    case ARG_NR:
      encoder->noise_reduction = g_value_get_uint (value);
      break;
    case ARG_INTERLACED:
      encoder->interlaced = g_value_get_boolean (value);
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
    GST_DEBUG_OBJECT (encoder, "setting property in wrong state");
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
    case ARG_PASS:
      g_value_set_enum (value, encoder->pass);
      break;
    case ARG_QUANTIZER:
      g_value_set_uint (value, encoder->quantizer);
      break;
    case ARG_STATS_FILE:
    case ARG_MULTIPASS_CACHE_FILE:
      g_value_set_string (value, encoder->mp_cache_file);
      break;
    case ARG_BYTE_STREAM:
      g_value_set_boolean (value, encoder->byte_stream);
      break;
    case ARG_BITRATE:
      g_value_set_uint (value, encoder->bitrate);
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
    case ARG_NR:
      g_value_set_uint (value, encoder->noise_reduction);
      break;
    case ARG_INTERLACED:
      g_value_set_boolean (value, encoder->interlaced);
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

  return gst_element_register (plugin, "x264enc",
      GST_RANK_PRIMARY, GST_TYPE_X264_ENC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "x264",
    "libx264-based H264 plugins",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
