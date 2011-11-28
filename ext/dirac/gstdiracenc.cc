/* Dirac Encoder
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstbasevideoencoder.h>
#include <gst/video/gstbasevideoutils.h>
#include <string.h>
#include <libdirac_encoder/dirac_encoder.h>
#include <math.h>

GST_DEBUG_CATEGORY_EXTERN (dirac_debug);
#define GST_CAT_DEFAULT dirac_debug

#define GST_TYPE_DIRAC_ENC \
  (gst_dirac_enc_get_type())
#define GST_DIRAC_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DIRAC_ENC,GstDiracEnc))
#define GST_DIRAC_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DIRAC_ENC,GstDiracEncClass))
#define GST_IS_DIRAC_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DIRAC_ENC))
#define GST_IS_DIRAC_ENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DIRAC_ENC))

typedef struct _GstDiracEnc GstDiracEnc;
typedef struct _GstDiracEncClass GstDiracEncClass;

struct _GstDiracEnc
{
  GstBaseVideoEncoder base_encoder;

  GstPad *sinkpad;
  GstPad *srcpad;

#if 0
  /* video properties */
  int width;
  int height;
  int fps_n, fps_d;
  int par_n, par_d;
  guint64 duration;
  guint32 fourcc;

  /* segment properties */
  GstClockTime segment_start;
  GstClockTime segment_position;
#endif

  /* state */
#if 0
  gboolean got_offset;
  guint64 granulepos_offset;
  guint64 granulepos_low;
  guint64 granulepos_hi;
  gboolean started;
  gint64 timestamp_offset;
  int picture_number;
#endif

  dirac_encoder_context_t enc_ctx;
  dirac_encoder_t *encoder;
  dirac_sourceparams_t *src_params;
  GstBuffer *seq_header_buffer;
  guint64 last_granulepos;
  guint64 granule_offset;

  GstBuffer *codec_data;
  GstBuffer *buffer;
  GstCaps *srccaps;
  int pull_frame_num;

  int frame_index;
};

struct _GstDiracEncClass
{
  GstBaseVideoEncoderClass parent_class;
};

GType gst_dirac_enc_get_type (void);

enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_L1_SEP,
  PROP_NUM_L1,
  PROP_XBLEN,
  PROP_YBLEN,
  PROP_XBSEP,
  PROP_YBSEP,
  PROP_CPD,
  PROP_QF,
  PROP_TARGETRATE,
  PROP_LOSSLESS,
  PROP_IWLT_FILTER,
  PROP_RWLT_FILTER,
  PROP_WLT_DEPTH,
  PROP_MULTI_QUANTS,
  PROP_MV_PREC,
  PROP_NO_SPARTITION,
  PROP_PREFILTER,
  PROP_PREFILTER_STRENGTH,
  PROP_PICTURE_CODING_MODE,
  PROP_USE_VLC
};

static void gst_dirac_enc_finalize (GObject * object);
static void gst_dirac_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dirac_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_dirac_enc_set_format (GstBaseVideoEncoder *
    base_video_encoder, GstVideoState * state);
static gboolean gst_dirac_enc_start (GstBaseVideoEncoder * base_video_encoder);
static gboolean gst_dirac_enc_stop (GstBaseVideoEncoder * base_video_encoder);
static GstFlowReturn gst_dirac_enc_finish (GstBaseVideoEncoder * base_video_encoder);
static GstFlowReturn gst_dirac_enc_handle_frame (GstBaseVideoEncoder *
    base_video_encoder, GstVideoFrame * frame);
static GstFlowReturn gst_dirac_enc_shape_output (GstBaseVideoEncoder *
    base_video_encoder, GstVideoFrame * frame);
static void gst_dirac_enc_create_codec_data (GstDiracEnc * dirac_enc,
    GstBuffer * seq_header);

static GstFlowReturn
gst_dirac_enc_process (GstDiracEnc * dirac_enc, gboolean end_sequence);
#if 0
static gboolean gst_dirac_enc_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_dirac_enc_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_dirac_enc_chain (GstPad * pad, GstBuffer * buf);
static GstFlowReturn gst_dirac_enc_process (GstDiracEnc * dirac_enc,
    gboolean end_sequence);
static GstStateChangeReturn gst_dirac_enc_change_state (GstElement * element,
    GstStateChange transition);
static const GstQueryType *gst_dirac_enc_get_query_types (GstPad * pad);
static gboolean gst_dirac_enc_src_query (GstPad * pad, GstQuery * query);
#endif

static GstStaticPadTemplate gst_dirac_enc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, YUY2, UYVY, AYUV }"))
    );

static GstStaticPadTemplate gst_dirac_enc_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-dirac;video/x-qt-part;video/x-mp4-part")
    );

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

GST_BOILERPLATE_FULL (GstDiracEnc, gst_dirac_enc, GstBaseVideoEncoder,
    GST_TYPE_BASE_VIDEO_ENCODER, _do_init);

static void
gst_dirac_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_dirac_enc_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_dirac_enc_sink_template);

  gst_element_class_set_details_simple (element_class, "Dirac Encoder",
      "Codec/Encoder/Video",
      "Encode raw YUV video into Dirac stream",
      "David Schleef <ds@schleef.org>");
}

static void
gst_dirac_enc_class_init (GstDiracEncClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseVideoEncoderClass *basevideoencoder_class;

  //int i;

  gobject_class = G_OBJECT_CLASS (klass);
  basevideoencoder_class = GST_BASE_VIDEO_ENCODER_CLASS (klass);

  gobject_class->set_property = gst_dirac_enc_set_property;
  gobject_class->get_property = gst_dirac_enc_get_property;
  gobject_class->finalize = gst_dirac_enc_finalize;

  g_object_class_install_property (gobject_class, PROP_L1_SEP,
      g_param_spec_int ("l1-sep", "l1_sep", "l1_sep",
          1, 1000, 24,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_NUM_L1,
      g_param_spec_int ("num-l1", "num_l1", "num_l1",
          0, 1000, 1,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_XBLEN,
      g_param_spec_int ("xblen", "xblen", "xblen",
          4, 64, 8,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_YBLEN,
      g_param_spec_int ("yblen", "yblen", "yblen",
          4, 64, 8,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_XBSEP,
      g_param_spec_int ("xbsep", "xbsep", "xbsep",
          4, 64, 12,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_YBSEP,
      g_param_spec_int ("ybsep", "ybsep", "ybsep",
          4, 64, 12,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_CPD,
      g_param_spec_int ("cpd", "cpd", "cpd",
          1, 100, 60,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_QF,
      g_param_spec_double ("qf", "qf", "qf",
          0.0, 10.0, 7.0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_TARGETRATE,
      g_param_spec_int ("targetrate", "targetrate", "targetrate",
          0, 10000, 1000,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_LOSSLESS,
      g_param_spec_boolean ("lossless", "lossless", "lossless",
          FALSE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_IWLT_FILTER,
      g_param_spec_int ("iwlt-filter", "iwlt_filter", "iwlt_filter",
          0, 7, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_RWLT_FILTER,
      g_param_spec_int ("rwlt-filter", "rwlt_filter", "rwlt_filter",
          0, 7, 1,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_WLT_DEPTH,
      g_param_spec_int ("wlt-depth", "wlt_depth", "wlt_depth",
          1, 4, 3,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_MULTI_QUANTS,
      g_param_spec_boolean ("multi-quants", "multi_quants", "multi_quants",
          FALSE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_MV_PREC,
      g_param_spec_int ("mv-prec", "mv_prec", "mv_prec",
          0, 3, 1,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_NO_SPARTITION,
      g_param_spec_boolean ("no-spartition", "no_spartition", "no_spartition",
          FALSE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_PREFILTER,
      g_param_spec_int ("prefilter", "prefilter", "prefilter",
          0, 3, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_PREFILTER_STRENGTH,
      g_param_spec_int ("pf-strength", "pf_strength", "pf_strength",
          0, 10, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_PICTURE_CODING_MODE,
      g_param_spec_int ("picture-coding-mode", "picture_coding_mode",
          "picture_coding_mode", 0, 1, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_USE_VLC,
      g_param_spec_boolean ("use-vlc", "use_vlc", "use_vlc", FALSE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  basevideoencoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_dirac_enc_set_format);
  basevideoencoder_class->start = GST_DEBUG_FUNCPTR (gst_dirac_enc_start);
  basevideoencoder_class->stop = GST_DEBUG_FUNCPTR (gst_dirac_enc_stop);
  basevideoencoder_class->finish = GST_DEBUG_FUNCPTR (gst_dirac_enc_finish);
  basevideoencoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_dirac_enc_handle_frame);
  basevideoencoder_class->shape_output =
      GST_DEBUG_FUNCPTR (gst_dirac_enc_shape_output);
}

static void
gst_dirac_enc_init (GstDiracEnc * dirac_enc, GstDiracEncClass * klass)
{
  GST_DEBUG ("gst_dirac_enc_init");

  dirac_encoder_context_init (&dirac_enc->enc_ctx, VIDEO_FORMAT_CUSTOM);
}

static gboolean
gst_dirac_enc_set_format (GstBaseVideoEncoder * base_video_encoder,
    GstVideoState * state)
{
  GstDiracEnc *dirac_enc = GST_DIRAC_ENC (base_video_encoder);
  GstCaps *caps;
  gboolean ret;

  GST_DEBUG ("set_format");

  gst_base_video_encoder_set_latency_fields (base_video_encoder, 2 * 2);

  switch (state->format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      dirac_enc->enc_ctx.src_params.chroma = format420;
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      dirac_enc->enc_ctx.src_params.chroma = format422;
      break;
    case GST_VIDEO_FORMAT_AYUV:
      dirac_enc->enc_ctx.src_params.chroma = format444;
      break;
    default:
      g_assert_not_reached ();
  }

  dirac_enc->enc_ctx.src_params.frame_rate.numerator = state->fps_n;
  dirac_enc->enc_ctx.src_params.frame_rate.denominator = state->fps_d;

  dirac_enc->enc_ctx.src_params.width = state->width;
  dirac_enc->enc_ctx.src_params.height = state->height;

  dirac_enc->enc_ctx.src_params.clean_area.width = state->width;
  dirac_enc->enc_ctx.src_params.clean_area.height = state->height;
  dirac_enc->enc_ctx.src_params.clean_area.left_offset = 0;
  dirac_enc->enc_ctx.src_params.clean_area.top_offset = 0;

  dirac_enc->enc_ctx.src_params.pix_asr.numerator = state->par_n;
  dirac_enc->enc_ctx.src_params.pix_asr.denominator = state->par_d;

  dirac_enc->enc_ctx.src_params.signal_range.luma_offset = 16;
  dirac_enc->enc_ctx.src_params.signal_range.luma_excursion = 219;
  dirac_enc->enc_ctx.src_params.signal_range.chroma_offset = 128;
  dirac_enc->enc_ctx.src_params.signal_range.chroma_excursion = 224;
  dirac_enc->enc_ctx.src_params.colour_spec.col_primary = CP_HDTV_COMP_INTERNET;
  dirac_enc->enc_ctx.src_params.colour_spec.col_matrix.kr = 0.2126;
  dirac_enc->enc_ctx.src_params.colour_spec.col_matrix.kb = 0.0722;
  dirac_enc->enc_ctx.src_params.colour_spec.trans_func = TF_TV;

  dirac_enc->enc_ctx.decode_flag = 0;
  dirac_enc->enc_ctx.instr_flag = 0;

  dirac_enc->granule_offset = ~0;

  dirac_enc->encoder = dirac_encoder_init (&dirac_enc->enc_ctx, FALSE);

  caps = gst_caps_new_simple ("video/x-dirac",
      "width", G_TYPE_INT, state->width,
      "height", G_TYPE_INT, state->height,
      "framerate", GST_TYPE_FRACTION, state->fps_n,
      state->fps_d,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, state->par_n,
      state->par_d, NULL);

  ret = gst_pad_set_caps (GST_BASE_VIDEO_CODEC_SRC_PAD (dirac_enc), caps);
  gst_caps_unref (caps);

  return ret;
}

static void
gst_dirac_enc_finalize (GObject * object)
{
  GstDiracEnc *dirac_enc;

  g_return_if_fail (GST_IS_DIRAC_ENC (object));
  dirac_enc = GST_DIRAC_ENC (object);

  if (dirac_enc->encoder) {
    dirac_encoder_close (dirac_enc->encoder);
    dirac_enc->encoder = NULL;
  }

  if (dirac_enc->codec_data) {
    gst_buffer_unref (dirac_enc->codec_data);
    dirac_enc->codec_data = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_dirac_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDiracEnc *encoder;

  g_return_if_fail (GST_IS_DIRAC_ENC (object));
  encoder = GST_DIRAC_ENC (object);

  GST_DEBUG ("gst_dirac_enc_set_property");

  switch (prop_id) {
    case PROP_L1_SEP:
      encoder->enc_ctx.enc_params.L1_sep = g_value_get_int (value);
      break;
    case PROP_NUM_L1:
      encoder->enc_ctx.enc_params.num_L1 = g_value_get_int (value);
      break;
    case PROP_XBLEN:
      encoder->enc_ctx.enc_params.xblen = g_value_get_int (value);
      break;
    case PROP_YBLEN:
      encoder->enc_ctx.enc_params.yblen = g_value_get_int (value);
      break;
    case PROP_XBSEP:
      encoder->enc_ctx.enc_params.xbsep = g_value_get_int (value);
      break;
    case PROP_YBSEP:
      encoder->enc_ctx.enc_params.ybsep = g_value_get_int (value);
      break;
    case PROP_CPD:
      encoder->enc_ctx.enc_params.cpd = g_value_get_int (value);
      break;
    case PROP_QF:
      encoder->enc_ctx.enc_params.qf = g_value_get_double (value);
      break;
    case PROP_TARGETRATE:
      encoder->enc_ctx.enc_params.trate = g_value_get_int (value);
      break;
    case PROP_LOSSLESS:
      encoder->enc_ctx.enc_params.lossless = g_value_get_boolean (value);
      break;
    case PROP_IWLT_FILTER:
      encoder->enc_ctx.enc_params.intra_wlt_filter =
          (dirac_wlt_filter_t) g_value_get_int (value);
      break;
    case PROP_RWLT_FILTER:
      encoder->enc_ctx.enc_params.inter_wlt_filter =
          (dirac_wlt_filter_t) g_value_get_int (value);
      break;
    case PROP_WLT_DEPTH:
      encoder->enc_ctx.enc_params.wlt_depth = g_value_get_int (value);
      break;
    case PROP_MULTI_QUANTS:
      encoder->enc_ctx.enc_params.multi_quants = g_value_get_boolean (value);
      break;
    case PROP_MV_PREC:
      encoder->enc_ctx.enc_params.mv_precision =
          (dirac_mvprecision_t) g_value_get_int (value);
      break;
    case PROP_NO_SPARTITION:
      encoder->enc_ctx.enc_params.spatial_partition =
          !g_value_get_boolean (value);
      break;
    case PROP_PREFILTER:
      encoder->enc_ctx.enc_params.prefilter =
          (dirac_prefilter_t) g_value_get_int (value);
      break;
    case PROP_PREFILTER_STRENGTH:
      encoder->enc_ctx.enc_params.prefilter_strength = g_value_get_int (value);
      break;
    case PROP_PICTURE_CODING_MODE:
      encoder->enc_ctx.enc_params.picture_coding_mode = g_value_get_int (value);
      break;
    case PROP_USE_VLC:
      encoder->enc_ctx.enc_params.using_ac = !g_value_get_boolean (value);
      break;
  }
}

static void
gst_dirac_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDiracEnc *encoder;

  g_return_if_fail (GST_IS_DIRAC_ENC (object));
  encoder = GST_DIRAC_ENC (object);

  switch (prop_id) {
    case PROP_L1_SEP:
      g_value_set_int (value, encoder->enc_ctx.enc_params.L1_sep);
      break;
    case PROP_NUM_L1:
      g_value_set_int (value, encoder->enc_ctx.enc_params.num_L1);
      break;
    case PROP_XBLEN:
      g_value_set_int (value, encoder->enc_ctx.enc_params.xblen);
      break;
    case PROP_YBLEN:
      g_value_set_int (value, encoder->enc_ctx.enc_params.yblen);
      break;
    case PROP_XBSEP:
      g_value_set_int (value, encoder->enc_ctx.enc_params.xbsep);
      break;
    case PROP_YBSEP:
      g_value_set_int (value, encoder->enc_ctx.enc_params.ybsep);
      break;
    case PROP_CPD:
      g_value_set_int (value, encoder->enc_ctx.enc_params.cpd);
      break;
    case PROP_QF:
      g_value_set_double (value, encoder->enc_ctx.enc_params.qf);
      break;
    case PROP_TARGETRATE:
      g_value_set_int (value, encoder->enc_ctx.enc_params.trate);
      break;
    case PROP_LOSSLESS:
      g_value_set_boolean (value, encoder->enc_ctx.enc_params.lossless);
      break;
    case PROP_IWLT_FILTER:
      g_value_set_int (value, encoder->enc_ctx.enc_params.intra_wlt_filter);
      break;
    case PROP_RWLT_FILTER:
      g_value_set_int (value, encoder->enc_ctx.enc_params.inter_wlt_filter);
      break;
    case PROP_WLT_DEPTH:
      g_value_set_int (value, encoder->enc_ctx.enc_params.wlt_depth);
      break;
    case PROP_MULTI_QUANTS:
      g_value_set_boolean (value, encoder->enc_ctx.enc_params.multi_quants);
      break;
    case PROP_MV_PREC:
      g_value_set_int (value, encoder->enc_ctx.enc_params.mv_precision);
      break;
    case PROP_NO_SPARTITION:
      g_value_set_boolean (value,
          !encoder->enc_ctx.enc_params.spatial_partition);
      break;
    case PROP_PREFILTER:
      g_value_set_int (value, encoder->enc_ctx.enc_params.prefilter);
      break;
    case PROP_PREFILTER_STRENGTH:
      g_value_set_int (value, encoder->enc_ctx.enc_params.prefilter_strength);
      break;
    case PROP_PICTURE_CODING_MODE:
      g_value_set_int (value, encoder->enc_ctx.enc_params.picture_coding_mode);
      break;
    case PROP_USE_VLC:
      g_value_set_boolean (value, !encoder->enc_ctx.enc_params.using_ac);
      break;
  }
}

#if 0
static gboolean
gst_dirac_enc_sink_event (GstPad * pad, GstEvent * event)
{
  GstDiracEnc *dirac_enc;
  gboolean ret;

  dirac_enc = GST_DIRAC_ENC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_dirac_enc_process (dirac_enc, TRUE);
      ret = gst_pad_push_event (dirac_enc->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      double rate;
      double applied_rate;
      GstFormat format;
      gint64 start;
      gint64 stop;
      gint64 position;

      gst_event_parse_new_segment_full (event, &update, &rate,
          &applied_rate, &format, &start, &stop, &position);

      GST_DEBUG ("new segment %lld %lld", start, position);
      dirac_enc->segment_start = start;
      dirac_enc->segment_position = position;

      ret = gst_pad_push_event (dirac_enc->srcpad, event);
    }
      break;
    default:
      ret = gst_pad_push_event (dirac_enc->srcpad, event);
      break;
  }

  return ret;
}
#endif

#if 0
#define OGG_DIRAC_GRANULE_SHIFT 30
#define OGG_DIRAC_GRANULE_LOW_MASK ((1ULL<<OGG_DIRAC_GRANULE_SHIFT)-1)

static gint64
granulepos_to_frame (gint64 granulepos)
{
  if (granulepos == -1)
    return -1;

  return (granulepos >> OGG_DIRAC_GRANULE_SHIFT) +
      (granulepos & OGG_DIRAC_GRANULE_LOW_MASK);
}

static const GstQueryType *
gst_dirac_enc_get_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    //GST_QUERY_POSITION,
    //GST_QUERY_DURATION,
    GST_QUERY_CONVERT
        /* FIXME */
        //0
  };

  return query_types;
}
#endif

#if 0
static gboolean
gst_dirac_enc_sink_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstDiracEnc *enc;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  enc = GST_DIRAC_ENC (gst_pad_get_parent (pad));

  /* FIXME: check if we are in a decoding state */

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
#if 0
        case GST_FORMAT_DEFAULT:
          *dest_value = gst_util_uint64_scale_int (src_value, 1,
              enc->bytes_per_picture);
          break;
#endif
        case GST_FORMAT_TIME:
          /* seems like a rather silly conversion, implement me if you like */
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale (src_value,
              GST_SECOND * enc->fps_d, enc->fps_n);
          break;
#if 0
        case GST_FORMAT_BYTES:
          *dest_value = gst_util_uint64_scale_int (src_value,
              enc->bytes_per_picture, 1);
          break;
#endif
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
      break;
  }
}
#endif

#if 0
static gboolean
gst_dirac_enc_src_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstDiracEnc *enc;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  enc = GST_DIRAC_ENC (gst_pad_get_parent (pad));

  /* FIXME: check if we are in a encoding state */

  switch (src_format) {
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale (granulepos_to_frame (src_value),
              enc->fps_d * GST_SECOND, enc->fps_n);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
        {
          *dest_value = gst_util_uint64_scale (src_value,
              enc->fps_n, enc->fps_d * GST_SECOND);
          break;
        }
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
      break;
  }

  gst_object_unref (enc);

  return res;
}

static gboolean
gst_dirac_enc_src_query (GstPad * pad, GstQuery * query)
{
  GstDiracEnc *enc;
  gboolean res;

  enc = GST_DIRAC_ENC (gst_pad_get_parent (pad));

  switch GST_QUERY_TYPE
    (query) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res = gst_dirac_enc_src_convert (pad, src_fmt, src_val, &dest_fmt,
          &dest_val);
      if (!res)
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
    }
  gst_object_unref (enc);
  return res;
error:
  GST_DEBUG_OBJECT (enc, "query failed");
  gst_object_unref (enc);
  return res;
}
#endif

/*
 * start is called once the input format is known.  This function
 * must decide on an output format and negotiate it.
 */
static gboolean
gst_dirac_enc_start (GstBaseVideoEncoder * base_video_encoder)
{
  return TRUE;
}

static gboolean
gst_dirac_enc_stop (GstBaseVideoEncoder * base_video_encoder)
{
  //GstDiracEnc *dirac_enc = GST_DIRAC_ENC (base_video_encoder);

#if 0
  if (dirac_enc->encoder) {
    dirac_encoder_free (dirac_enc->encoder);
    dirac_enc->encoder = NULL;
  }
#endif

  return TRUE;
}

static GstFlowReturn
gst_dirac_enc_finish (GstBaseVideoEncoder * base_video_encoder)
{
  GstDiracEnc *dirac_enc = GST_DIRAC_ENC (base_video_encoder);

  GST_DEBUG ("finish");

  gst_dirac_enc_process (dirac_enc, TRUE);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_dirac_enc_handle_frame (GstBaseVideoEncoder * base_video_encoder,
    GstVideoFrame * frame)
{
  GstDiracEnc *dirac_enc = GST_DIRAC_ENC (base_video_encoder);
  GstFlowReturn ret;
  int r;
  const GstVideoState *state;
  uint8_t *data;
  gboolean copied = FALSE;
  int size;

  state = gst_base_video_encoder_get_state (base_video_encoder);

  if (dirac_enc->granule_offset == ~0ULL) {
    dirac_enc->granule_offset =
        gst_util_uint64_scale (frame->presentation_timestamp,
        2 * state->fps_n, GST_SECOND * state->fps_d);
    GST_DEBUG ("granule offset %" G_GINT64_FORMAT, dirac_enc->granule_offset);
  }

  switch (state->format) {
    case GST_VIDEO_FORMAT_I420:
      data = GST_BUFFER_DATA (frame->sink_buffer);
      size = GST_BUFFER_SIZE (frame->sink_buffer);
      break;
    case GST_VIDEO_FORMAT_YUY2:
    {
      uint8_t *bufdata = GST_BUFFER_DATA (frame->sink_buffer);
      int i, j;

      data = (uint8_t *) g_malloc (GST_BUFFER_SIZE (frame->sink_buffer));
      copied = TRUE;
      size = GST_BUFFER_SIZE (frame->sink_buffer);
      for (j = 0; j < state->height; j++) {
        for (i = 0; i < state->width; i++) {
          data[j * state->width + i] = bufdata[j * state->width * 2 + i * 2];
        }
        for (i = 0; i < state->width / 2; i++) {
          data[state->height * state->width +
              j * (state->width / 2) + i] =
              bufdata[j * state->width * 2 + i * 4 + 1];
          data[state->height * state->width +
              +state->height * (state->width / 2)
              + j * (state->width / 2) + i] =
              bufdata[j * state->width * 2 + i * 4 + 3];
        }
      }
    }
      break;
    case GST_VIDEO_FORMAT_UYVY:
    {
      uint8_t *bufdata = GST_BUFFER_DATA (frame->sink_buffer);
      int i, j;

      data = (uint8_t *) g_malloc (GST_BUFFER_SIZE (frame->sink_buffer));
      copied = TRUE;
      size = GST_BUFFER_SIZE (frame->sink_buffer);
      for (j = 0; j < state->height; j++) {
        for (i = 0; i < state->width; i++) {
          data[j * state->width + i] =
              bufdata[j * state->width * 2 + i * 2 + 1];
        }
        for (i = 0; i < state->width / 2; i++) {
          data[state->height * state->width +
              j * (state->width / 2) + i] =
              bufdata[j * state->width * 2 + i * 4 + 0];
          data[state->height * state->width +
              +state->height * (state->width / 2)
              + j * (state->width / 2) + i] =
              bufdata[j * state->width * 2 + i * 4 + 2];
        }
      }
    }
      break;
    case GST_VIDEO_FORMAT_AYUV:
    {
      uint8_t *bufdata = GST_BUFFER_DATA (frame->sink_buffer);
      int i, j;

      size = state->height * state->width * 3;
      data = (uint8_t *) g_malloc (size);
      copied = TRUE;
      for (j = 0; j < state->height; j++) {
        for (i = 0; i < state->width; i++) {
          data[j * state->width + i] =
              bufdata[j * state->width * 4 + i * 4 + 1];
          data[state->height * state->width
              + j * state->width + i] =
              bufdata[j * state->width * 4 + i * 4 + 2];
          data[2 * state->height * state->width +
              +j * state->width + i] =
              bufdata[j * state->width * 4 + i * 4 + 3];
        }
      }
    }
      break;
    default:
      g_assert_not_reached ();
  }

  r = dirac_encoder_load (dirac_enc->encoder, data,
      GST_BUFFER_SIZE (frame->sink_buffer));
  if (copied) {
    g_free (data);
  }
  if (r != (int) GST_BUFFER_SIZE (frame->sink_buffer)) {
    GST_ERROR ("failed to push picture");
    return GST_FLOW_ERROR;
  }

  GST_DEBUG ("handle frame");

  gst_buffer_unref (frame->sink_buffer);
  frame->sink_buffer = NULL;

  frame->system_frame_number = dirac_enc->frame_index;
  dirac_enc->frame_index++;

  ret = gst_dirac_enc_process (dirac_enc, FALSE);

  return ret;
}

#if 0
static gboolean
gst_pad_is_negotiated (GstPad * pad)
{
  GstCaps *caps;

  g_return_val_if_fail (pad != NULL, FALSE);

  caps = gst_pad_get_negotiated_caps (pad);
  if (caps) {
    gst_caps_unref (caps);
    return TRUE;
  }

  return FALSE;
}
#endif

#if 0
static GstFlowReturn
gst_dirac_enc_chain (GstPad * pad, GstBuffer * buf)
{
  GstDiracEnc *dirac_enc;
  GstFlowReturn ret;

  dirac_enc = GST_DIRAC_ENC (gst_pad_get_parent (pad));

  if (!gst_pad_is_negotiated (pad)) {
    return GST_FLOW_NOT_NEGOTIATED;
  }
  if (GST_BUFFER_TIMESTAMP (buf) < dirac_enc->segment_start) {
    GST_DEBUG ("dropping early buffer");
    return GST_FLOW_OK;
  }
  if (!dirac_enc->got_offset) {
    dirac_enc->granulepos_offset =
        gst_util_uint64_scale (GST_BUFFER_TIMESTAMP (buf), dirac_enc->fps_n,
        GST_SECOND * dirac_enc->fps_d);

    GST_DEBUG ("using granulepos offset %lld", dirac_enc->granulepos_offset);
    dirac_enc->granulepos_hi = 0;
    dirac_enc->got_offset = TRUE;

    dirac_enc->timestamp_offset = GST_BUFFER_TIMESTAMP (buf);
    dirac_enc->picture_number = 0;
  }
  if (!dirac_enc->started) {
    dirac_enc->encoder = dirac_encoder_init (&dirac_enc->enc_ctx, FALSE);
    dirac_enc->started = TRUE;
  }

  switch (dirac_enc->fourcc) {
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      dirac_encoder_load (dirac_enc->encoder, GST_BUFFER_DATA (buf),
          GST_BUFFER_SIZE (buf));
      break;
    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
    {
      uint8_t *data;
      uint8_t *bufdata = GST_BUFFER_DATA (buf);
      int i, j;

      data = (uint8_t *) g_malloc (GST_BUFFER_SIZE (buf));
      for (j = 0; j < dirac_enc->height; j++) {
        for (i = 0; i < dirac_enc->width; i++) {
          data[j * dirac_enc->width + i] =
              bufdata[j * dirac_enc->width * 2 + i * 2];
        }
        for (i = 0; i < dirac_enc->width / 2; i++) {
          data[dirac_enc->height * dirac_enc->width +
              j * (dirac_enc->width / 2) + i] =
              bufdata[j * dirac_enc->width * 2 + i * 4 + 1];
          data[dirac_enc->height * dirac_enc->width +
              +dirac_enc->height * (dirac_enc->width / 2)
              + j * (dirac_enc->width / 2) + i] =
              bufdata[j * dirac_enc->width * 2 + i * 4 + 3];
        }
      }
      dirac_encoder_load (dirac_enc->encoder, data, GST_BUFFER_SIZE (buf));
      g_free (data);
    }
      break;
    case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
    {
      uint8_t *data;
      uint8_t *bufdata = GST_BUFFER_DATA (buf);
      int i, j;

      data = (uint8_t *) g_malloc (GST_BUFFER_SIZE (buf));
      for (j = 0; j < dirac_enc->height; j++) {
        for (i = 0; i < dirac_enc->width; i++) {
          data[j * dirac_enc->width + i] =
              bufdata[j * dirac_enc->width * 2 + i * 2 + 1];
        }
        for (i = 0; i < dirac_enc->width / 2; i++) {
          data[dirac_enc->height * dirac_enc->width +
              j * (dirac_enc->width / 2) + i] =
              bufdata[j * dirac_enc->width * 2 + i * 4 + 0];
          data[dirac_enc->height * dirac_enc->width +
              +dirac_enc->height * (dirac_enc->width / 2)
              + j * (dirac_enc->width / 2) + i] =
              bufdata[j * dirac_enc->width * 2 + i * 4 + 2];
        }
      }
      dirac_encoder_load (dirac_enc->encoder, data, GST_BUFFER_SIZE (buf));
      g_free (data);
    }
      break;
    case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
    {
      uint8_t *data;
      uint8_t *bufdata = GST_BUFFER_DATA (buf);
      int i, j;

      data = (uint8_t *) g_malloc (GST_BUFFER_SIZE (buf));
      for (j = 0; j < dirac_enc->height; j++) {
        for (i = 0; i < dirac_enc->width; i++) {
          data[j * dirac_enc->width + i] =
              bufdata[j * dirac_enc->width * 4 + i * 4 + 1];
        }
        for (i = 0; i < dirac_enc->width; i++) {
          data[dirac_enc->height * dirac_enc->width
              + j * dirac_enc->width + i] =
              bufdata[j * dirac_enc->width * 4 + i * 4 + 2];
          data[2 * dirac_enc->height * dirac_enc->width +
              +j * dirac_enc->width + i] =
              bufdata[j * dirac_enc->width * 4 + i * 4 + 3];
        }
      }
      dirac_encoder_load (dirac_enc->encoder, data, GST_BUFFER_SIZE (buf));
      g_free (data);
    }
      break;
    default:
      g_assert_not_reached ();
  }

  ret = gst_dirac_enc_process (dirac_enc, FALSE);

  gst_buffer_unref (buf);
  gst_object_unref (dirac_enc);

  return ret;
}
#endif

#define DIRAC_PARSE_CODE_IS_SEQ_HEADER(x) ((x) == 0x00)
#define DIRAC_PARSE_CODE_IS_END_OF_SEQUENCE(x) ((x) == 0x10)
#define DIRAC_PARSE_CODE_IS_PICTURE(x) ((x) & 0x8)
#define DIRAC_PARSE_CODE_NUM_REFS(x) ((x) & 0x3)
#define DIRAC_PARSE_CODE_IS_INTRA(x) (DIRAC_PARSE_CODE_IS_PICTURE(x) && DIRAC_PARSE_CODE_NUM_REFS(x) == 0)

static GstFlowReturn
gst_dirac_enc_process (GstDiracEnc * dirac_enc, gboolean end_sequence)
{
  GstBuffer *outbuf;
  GstFlowReturn ret = GST_FLOW_OK;
  int parse_code;
  int state;
  GstVideoFrame *frame;

  do {
    outbuf = gst_buffer_new_and_alloc (32 * 1024 * 1024);
    dirac_enc->encoder->enc_buf.buffer = GST_BUFFER_DATA (outbuf);
    dirac_enc->encoder->enc_buf.size = GST_BUFFER_SIZE (outbuf);

    if (end_sequence) {
      dirac_encoder_end_sequence (dirac_enc->encoder);
    }
    state = dirac_encoder_output (dirac_enc->encoder);

    switch (state) {
      case ENC_STATE_BUFFER:
        GST_DEBUG ("BUFFER");
        gst_buffer_unref (outbuf);
        break;
      case ENC_STATE_INVALID:
        GST_DEBUG ("Dirac returned ENC_STATE_INVALID");
        gst_buffer_unref (outbuf);
        return GST_FLOW_ERROR;
      case ENC_STATE_EOS:
        frame =
            gst_base_video_encoder_get_oldest_frame (GST_BASE_VIDEO_ENCODER
            (dirac_enc));

        /* FIXME: Get the frame from somewhere somehow... */
        if (frame) {
          frame->src_buffer = outbuf;
          GST_BUFFER_SIZE (outbuf) = dirac_enc->encoder->enc_buf.size;

          ret =
              gst_base_video_encoder_finish_frame (GST_BASE_VIDEO_ENCODER
              (dirac_enc), frame);

          if (ret != GST_FLOW_OK) {
            GST_DEBUG ("pad_push returned %d", ret);
            return ret;
          }
        }
        break;
      case ENC_STATE_AVAIL:
        GST_DEBUG ("AVAIL");
        /* FIXME this doesn't reorder frames */
        frame =
            gst_base_video_encoder_get_oldest_frame (GST_BASE_VIDEO_ENCODER
            (dirac_enc));
        if (frame == NULL) {
          GST_ERROR ("didn't get frame %d", dirac_enc->pull_frame_num);
        }
        dirac_enc->pull_frame_num++;

        parse_code = ((guint8 *) GST_BUFFER_DATA (outbuf))[4];

        if (DIRAC_PARSE_CODE_IS_SEQ_HEADER (parse_code)) {
          frame->is_sync_point = TRUE;
        }

        if (!dirac_enc->codec_data) {
          GstCaps *caps;
          const GstVideoState *state = gst_base_video_encoder_get_state (GST_BASE_VIDEO_ENCODER (dirac_enc));

          gst_dirac_enc_create_codec_data (dirac_enc, outbuf);
          
          caps = gst_caps_new_simple ("video/x-dirac",
              "width", G_TYPE_INT, state->width,
              "height", G_TYPE_INT, state->height,
              "framerate", GST_TYPE_FRACTION, state->fps_n,
              state->fps_d,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, state->par_n,
              state->par_d, "streamheader", GST_TYPE_BUFFER, dirac_enc->codec_data,
              NULL);
          if (!gst_pad_set_caps (GST_BASE_VIDEO_CODEC_SRC_PAD (dirac_enc), caps))
            ret = GST_FLOW_NOT_NEGOTIATED;
          gst_caps_unref (caps);

          if (ret != GST_FLOW_OK) {
            GST_ERROR ("Failed to set srcpad caps");
            gst_buffer_unref (outbuf);
            return ret;
          }
        }

        frame->src_buffer = outbuf;
        GST_BUFFER_SIZE (outbuf) = dirac_enc->encoder->enc_buf.size;

        ret =
            gst_base_video_encoder_finish_frame (GST_BASE_VIDEO_ENCODER
            (dirac_enc), frame);

        if (ret != GST_FLOW_OK) {
          GST_DEBUG ("pad_push returned %d", ret);
          return ret;
        }
        break;
      default:
        GST_ERROR ("Dirac returned state==%d", state);
        gst_buffer_unref (outbuf);
        return GST_FLOW_ERROR;
    }
  } while (state == ENC_STATE_AVAIL);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_dirac_enc_shape_output_ogg (GstBaseVideoEncoder * base_video_encoder,
    GstVideoFrame * frame)
{
  GstDiracEnc *dirac_enc;
  int delay;
  int dist;
  int pt;
  int dt;
  guint64 granulepos_hi;
  guint64 granulepos_low;
  GstBuffer *buf = frame->src_buffer;

  dirac_enc = GST_DIRAC_ENC (base_video_encoder);

  pt = frame->presentation_frame_number * 2 + dirac_enc->granule_offset;
  dt = frame->decode_frame_number * 2 + dirac_enc->granule_offset;
  delay = pt - dt;
  dist = frame->distance_from_sync;

  GST_DEBUG ("sys %d dpn %d pt %d dt %d delay %d dist %d",
      (int) frame->system_frame_number,
      (int) frame->decode_frame_number, pt, dt, delay, dist);

  granulepos_hi = (((uint64_t) pt - delay) << 9) | ((dist >> 8));
  granulepos_low = (delay << 9) | (dist & 0xff);
  GST_DEBUG ("granulepos %" G_GINT64_FORMAT ":%" G_GINT64_FORMAT, granulepos_hi,
      granulepos_low);

  if (frame->is_eos) {
    GST_BUFFER_OFFSET_END (buf) = dirac_enc->last_granulepos;
  } else {
    dirac_enc->last_granulepos = (granulepos_hi << 22) | (granulepos_low);
    GST_BUFFER_OFFSET_END (buf) = dirac_enc->last_granulepos;
  }

  gst_buffer_set_caps (buf,
      GST_PAD_CAPS (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_encoder)));

  return gst_pad_push (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_encoder), buf);
}

static GstFlowReturn
gst_dirac_enc_shape_output (GstBaseVideoEncoder * base_video_encoder,
    GstVideoFrame * frame)
{
  gst_dirac_enc_shape_output_ogg (base_video_encoder, frame);

  return GST_FLOW_ERROR;
}

static void
gst_dirac_enc_create_codec_data (GstDiracEnc * dirac_enc,
    GstBuffer * seq_header)
{
  GstBuffer *buf;
  int size;

  size = GST_READ_UINT32_BE (GST_BUFFER_DATA (seq_header) + 9);

#define DIRAC_PARSE_HEADER_SIZE 13
  buf = gst_buffer_new_and_alloc (size + DIRAC_PARSE_HEADER_SIZE);

  memcpy (GST_BUFFER_DATA (buf), GST_BUFFER_DATA (seq_header), size);
  GST_WRITE_UINT32_BE (GST_BUFFER_DATA (buf) + size + 0, 0x42424344);
#define DIRAC_PARSE_CODE_END_OF_SEQUENCE 0x10
  GST_WRITE_UINT8 (GST_BUFFER_DATA (buf) + size + 4,
      DIRAC_PARSE_CODE_END_OF_SEQUENCE);
  GST_WRITE_UINT32_BE (GST_BUFFER_DATA (buf) + size + 5, 0);
  GST_WRITE_UINT32_BE (GST_BUFFER_DATA (buf) + size + 9, size);

  /* ogg(mux) expects the header buffers to have 0 timestamps -
     set OFFSET and OFFSET_END accordingly */
  GST_BUFFER_OFFSET (buf) = 0;
  GST_BUFFER_OFFSET_END (buf) = 0;
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_IN_CAPS);

  if (dirac_enc->codec_data) {
    gst_buffer_unref (dirac_enc->codec_data);
  }
  dirac_enc->codec_data = buf;
}


