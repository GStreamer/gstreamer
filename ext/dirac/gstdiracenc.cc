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
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  /* parameters */
  int level;

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

  /* state */
  gboolean got_offset;
  guint64 granulepos_offset;
  guint64 granulepos_low;
  guint64 granulepos_hi;
  gboolean started;
  gint64 timestamp_offset;
  int picture_number;

  dirac_encoder_context_t enc_ctx;
  dirac_encoder_t *encoder;
  dirac_sourceparams_t *src_params;
  GstBuffer *buffer;
};

struct _GstDiracEncClass
{
  GstElementClass parent_class;
};


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

static gboolean gst_dirac_enc_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_dirac_enc_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_dirac_enc_chain (GstPad * pad, GstBuffer * buf);
static GstFlowReturn gst_dirac_enc_process (GstDiracEnc * dirac_enc,
    gboolean end_sequence);
static GstStateChangeReturn gst_dirac_enc_change_state (GstElement * element,
    GstStateChange transition);
static const GstQueryType *gst_dirac_enc_get_query_types (GstPad * pad);
static gboolean gst_dirac_enc_src_query (GstPad * pad, GstQuery * query);

static GstStaticPadTemplate gst_dirac_enc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, YV12, YUY2, UYVY, AYUV }"))
    );

static GstStaticPadTemplate gst_dirac_enc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-dirac")
    );

GST_BOILERPLATE (GstDiracEnc, gst_dirac_enc, GstElement, GST_TYPE_ELEMENT);

static void
gst_dirac_enc_base_init (gpointer g_class)
{
  static GstElementDetails dirac_enc_details =
      GST_ELEMENT_DETAILS ((gchar *) "Dirac Encoder",
      (gchar *) "Codec/Encoder/Video",
      (gchar *) "Encode raw YUV video into Dirac stream",
      (gchar *) "David Schleef <ds@schleef.org>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_dirac_enc_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_dirac_enc_sink_template));

  gst_element_class_set_details (element_class, &dirac_enc_details);
}

static void
gst_dirac_enc_class_init (GstDiracEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  //int i;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_dirac_enc_set_property;
  gobject_class->get_property = gst_dirac_enc_get_property;
  gobject_class->finalize = gst_dirac_enc_finalize;

  g_object_class_install_property (gobject_class, PROP_L1_SEP,
      g_param_spec_int ("l1_sep", "l1_sep", "l1_sep",
          1, 1000, 24, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_NUM_L1,
      g_param_spec_int ("num_l1", "num_l1", "num_l1",
          0, 1000, 1, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_XBLEN,
      g_param_spec_int ("xblen", "xblen", "xblen",
          4, 64, 8, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_YBLEN,
      g_param_spec_int ("yblen", "yblen", "yblen",
          4, 64, 8, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_XBSEP,
      g_param_spec_int ("xbsep", "xbsep", "xbsep",
          4, 64, 12, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_YBSEP,
      g_param_spec_int ("ybsep", "ybsep", "ybsep",
          4, 64, 12, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_CPD,
      g_param_spec_int ("cpd", "cpd", "cpd",
          1, 100, 60, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_QF,
      g_param_spec_double ("qf", "qf", "qf",
          0.0, 10.0, 7.0, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_TARGETRATE,
      g_param_spec_int ("targetrate", "targetrate", "targetrate",
          0, 10000, 1000, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_LOSSLESS,
      g_param_spec_boolean ("lossless", "lossless", "lossless",
          FALSE, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_IWLT_FILTER,
      g_param_spec_int ("iwlt_filter", "iwlt_filter", "iwlt_filter",
          0, 7, 0, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_RWLT_FILTER,
      g_param_spec_int ("rwlt_filter", "rwlt_filter", "rwlt_filter",
          0, 7, 1, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_WLT_DEPTH,
      g_param_spec_int ("wlt_depth", "wlt_depth", "wlt_depth",
          1, 4, 3, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_MULTI_QUANTS,
      g_param_spec_boolean ("multi_quants", "multi_quants", "multi_quants",
          FALSE, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_MV_PREC,
      g_param_spec_int ("mv_prec", "mv_prec", "mv_prec",
          0, 3, 1, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_NO_SPARTITION,
      g_param_spec_boolean ("no_spartition", "no_spartition", "no_spartition",
          FALSE, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_PREFILTER,
      g_param_spec_int ("prefilter", "prefilter", "prefilter",
          0, 3, 0, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_PREFILTER_STRENGTH,
      g_param_spec_int ("pf_strength", "pf_strength", "pf_strength",
          0, 10, 0, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_PICTURE_CODING_MODE,
      g_param_spec_int ("picture_coding_mode", "picture_coding_mode",
          "picture_coding_mode", 0, 1, 0, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_USE_VLC,
      g_param_spec_boolean ("use_vlc", "use_vlc", "use_vlc", FALSE,
          (GParamFlags) G_PARAM_READWRITE));

  gstelement_class->change_state = gst_dirac_enc_change_state;
}

static void
gst_dirac_enc_init (GstDiracEnc * dirac_enc, GstDiracEncClass * klass)
{
  GST_DEBUG ("gst_dirac_enc_init");

  dirac_enc->src_params = &dirac_enc->enc_ctx.src_params;

  dirac_enc->sinkpad =
      gst_pad_new_from_static_template (&gst_dirac_enc_sink_template, "sink");
  gst_pad_set_chain_function (dirac_enc->sinkpad, gst_dirac_enc_chain);
  gst_pad_set_event_function (dirac_enc->sinkpad, gst_dirac_enc_sink_event);
  gst_pad_set_setcaps_function (dirac_enc->sinkpad, gst_dirac_enc_sink_setcaps);
  //gst_pad_set_query_function (dirac_enc->sinkpad, gst_dirac_enc_sink_query);
  gst_element_add_pad (GST_ELEMENT (dirac_enc), dirac_enc->sinkpad);

  dirac_enc->srcpad =
      gst_pad_new_from_static_template (&gst_dirac_enc_src_template, "src");
  gst_pad_set_query_type_function (dirac_enc->srcpad,
      gst_dirac_enc_get_query_types);
  gst_pad_set_query_function (dirac_enc->srcpad, gst_dirac_enc_src_query);
  gst_element_add_pad (GST_ELEMENT (dirac_enc), dirac_enc->srcpad);

  dirac_encoder_context_init (&dirac_enc->enc_ctx, VIDEO_FORMAT_CUSTOM);
}

static gboolean
gst_dirac_enc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstDiracEnc *dirac_enc = GST_DIRAC_ENC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_fourcc (structure, "format", &dirac_enc->fourcc);
  gst_structure_get_int (structure, "width", &dirac_enc->width);
  gst_structure_get_int (structure, "height", &dirac_enc->height);
  gst_structure_get_fraction (structure, "framerate", &dirac_enc->fps_n,
      &dirac_enc->fps_d);
  dirac_enc->par_n = 1;
  dirac_enc->par_d = 1;
  gst_structure_get_fraction (structure, "pixel-aspect-ratio",
      &dirac_enc->par_n, &dirac_enc->par_d);

  if (dirac_enc->fourcc != GST_MAKE_FOURCC ('I', '4', '2', '0')) {
    GST_ERROR
        ("Dirac encoder element is known to be buggy for video formats other that I420");
  }

  switch (dirac_enc->fourcc) {
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
    case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
      dirac_enc->enc_ctx.src_params.chroma = format420;
      break;
    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
    case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
      dirac_enc->enc_ctx.src_params.chroma = format422;
      break;
    case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
      dirac_enc->enc_ctx.src_params.chroma = format444;
      break;
    default:
      g_assert_not_reached ();
  }

  dirac_enc->enc_ctx.src_params.frame_rate.numerator = dirac_enc->fps_n;
  dirac_enc->enc_ctx.src_params.frame_rate.denominator = dirac_enc->fps_d;

  dirac_enc->enc_ctx.src_params.width = dirac_enc->width;
  dirac_enc->enc_ctx.src_params.height = dirac_enc->height;
#if 0
  /* FIXME */
  dirac_enc->enc_ctx.src_params.clean_width = dirac_enc->width;
  dirac_enc->enc_ctx.src_params.clean_height = dirac_enc->height;
#endif

#if 0
  /* FIXME */
  dirac_enc->enc_ctx.src_params.aspect_ratio_numerator = dirac_enc->par_n;
  dirac_enc->enc_ctx.src_params.aspect_ratio_denominator = dirac_enc->par_d;
#endif

#if 0
  /* FIXME */
  dirac_video_format_set_std_signal_range (dirac_enc->video_format,
      DIRAC_SIGNAL_RANGE_8BIT_VIDEO);
  dirac_video_format_set_std_colour_spec (dirac_enc->video_format,
      DIRAC_COLOUR_SPEC_HDTV);
#endif

  dirac_enc->enc_ctx.decode_flag = 0;
  dirac_enc->enc_ctx.instr_flag = 0;

  dirac_enc->duration = gst_util_uint64_scale_int (GST_SECOND,
      dirac_enc->fps_d, dirac_enc->fps_n);

  gst_object_unref (GST_OBJECT (dirac_enc));

  return TRUE;
}

static void
gst_dirac_enc_finalize (GObject * object)
{
  GstDiracEnc *dirac_enc;

  g_return_if_fail (GST_IS_DIRAC_ENC (object));
  dirac_enc = GST_DIRAC_ENC (object);

  if (dirac_enc->encoder) {
    /* FIXME */
    //dirac_encoder_free (dirac_enc->encoder);
    dirac_enc->encoder = NULL;
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
      encoder->enc_ctx.enc_params.using_ac = g_value_get_boolean (value);
      break;
  }
}

static void
gst_dirac_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDiracEnc *src;

  g_return_if_fail (GST_IS_DIRAC_ENC (object));
  src = GST_DIRAC_ENC (object);

#if 0
  if (prop_id >= 1) {
    const DiracEncoderSetting *setting;

    setting = dirac_encoder_get_setting_info (prop_id - 1);
    switch (G_VALUE_TYPE (value)) {
      case G_TYPE_DOUBLE:
        g_value_set_double (value,
            dirac_encoder_setting_get_double (src->encoder, setting->name));
        break;
      case G_TYPE_INT:
        g_value_set_int (value,
            dirac_encoder_setting_get_double (src->encoder, setting->name));
        break;
      case G_TYPE_BOOLEAN:
        g_value_set_boolean (value,
            dirac_encoder_setting_get_double (src->encoder, setting->name));
        break;
    }
  }
#endif
}

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

  dirac_encoder_load (dirac_enc->encoder, GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf));

  ret = gst_dirac_enc_process (dirac_enc, FALSE);

  gst_object_unref (dirac_enc);

  return ret;
}

#define SCHRO_PARSE_CODE_IS_SEQ_HEADER(x) ((x) == 0x00)
#define SCHRO_PARSE_CODE_IS_END_OF_SEQUENCE(x) ((x) == 0x10)
#define SCHRO_PARSE_CODE_IS_PICTURE(x) ((x) & 0x8)
#define SCHRO_PARSE_CODE_NUM_REFS(x) ((x) & 0x3)
#define SCHRO_PARSE_CODE_IS_INTRA(x) (SCHRO_PARSE_CODE_IS_PICTURE(x) && SCHRO_PARSE_CODE_NUM_REFS(x) == 0)

static GstFlowReturn
gst_dirac_enc_process (GstDiracEnc * dirac_enc, gboolean end_sequence)
{
  GstBuffer *outbuf;
  GstFlowReturn ret;
  int presentation_frame;
  int parse_code;
  int state;

  do {
    outbuf = gst_buffer_new_and_alloc (32 * 1024 * 1024);
    dirac_enc->encoder->enc_buf.buffer = GST_BUFFER_DATA (outbuf);
    dirac_enc->encoder->enc_buf.size = GST_BUFFER_SIZE (outbuf);

    if (end_sequence) {
      /* FIXME this is a hack to make the code simpler. */
      dirac_encoder_end_sequence (dirac_enc->encoder);
      state = ENC_STATE_AVAIL;
    } else {
      state = dirac_encoder_output (dirac_enc->encoder);
    }

    switch (state) {
      case ENC_STATE_BUFFER:
        break;
      case ENC_STATE_INVALID:
        GST_ERROR ("Dirac returned ENC_STATE_INVALID");
        gst_buffer_unref (outbuf);
        return GST_FLOW_ERROR;
      case ENC_STATE_AVAIL:
        parse_code = ((guint8 *) GST_BUFFER_DATA (outbuf))[4];
        /* FIXME */
        presentation_frame = 0;

        if (SCHRO_PARSE_CODE_IS_SEQ_HEADER (parse_code)) {
          dirac_enc->granulepos_hi = dirac_enc->granulepos_offset +
              presentation_frame + 1;
        }

        dirac_enc->granulepos_low = dirac_enc->granulepos_offset +
            presentation_frame + 1 - dirac_enc->granulepos_hi;

        gst_buffer_set_caps (outbuf,
            gst_caps_new_simple ("video/x-dirac",
                "width", G_TYPE_INT, dirac_enc->width,
                "height", G_TYPE_INT, dirac_enc->height,
                "framerate", GST_TYPE_FRACTION, dirac_enc->fps_n,
                dirac_enc->fps_d, NULL));

        GST_BUFFER_SIZE (outbuf) = dirac_enc->encoder->enc_buf.size;
        if (SCHRO_PARSE_CODE_IS_PICTURE (parse_code)) {
          GST_BUFFER_OFFSET_END (outbuf) =
              (dirac_enc->granulepos_hi << OGG_DIRAC_GRANULE_SHIFT) +
              dirac_enc->granulepos_low;
          GST_BUFFER_OFFSET (outbuf) = gst_util_uint64_scale (
              (dirac_enc->granulepos_hi + dirac_enc->granulepos_low),
              dirac_enc->fps_d * GST_SECOND, dirac_enc->fps_n);
          GST_BUFFER_DURATION (outbuf) = dirac_enc->duration;
          GST_BUFFER_TIMESTAMP (outbuf) =
              dirac_enc->timestamp_offset +
              gst_util_uint64_scale (dirac_enc->picture_number,
              dirac_enc->fps_d * GST_SECOND, dirac_enc->fps_n);
          dirac_enc->picture_number++;
          if (!SCHRO_PARSE_CODE_IS_INTRA (parse_code)) {
            GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
          }
        } else {
          GST_BUFFER_OFFSET_END (outbuf) = 0;
          GST_BUFFER_OFFSET (outbuf) = 0;
          GST_BUFFER_DURATION (outbuf) = -1;
          //GST_BUFFER_TIMESTAMP (outbuf) = -1;
          GST_BUFFER_TIMESTAMP (outbuf) =
              dirac_enc->timestamp_offset +
              gst_util_uint64_scale (dirac_enc->picture_number,
              dirac_enc->fps_d * GST_SECOND, dirac_enc->fps_n);
        }

        GST_INFO
            ("size %d offset %lld granulepos %llu:%llu timestamp %lld duration %lld",
            GST_BUFFER_SIZE (outbuf), GST_BUFFER_OFFSET (outbuf),
            GST_BUFFER_OFFSET_END (outbuf) >> OGG_DIRAC_GRANULE_SHIFT,
            GST_BUFFER_OFFSET_END (outbuf) & OGG_DIRAC_GRANULE_LOW_MASK,
            GST_BUFFER_TIMESTAMP (outbuf), GST_BUFFER_DURATION (outbuf));

        ret = gst_pad_push (dirac_enc->srcpad, outbuf);

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
    if (end_sequence) {
      /* FIXME more hackage */
      return GST_FLOW_OK;
    }
  } while (state == ENC_STATE_AVAIL);

  gst_buffer_unref (outbuf);
  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_dirac_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstDiracEnc *dirac_enc;
  GstStateChangeReturn ret;

  dirac_enc = GST_DIRAC_ENC (element);

  switch (transition) {
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);

  switch (transition) {
    default:
      break;
  }

  return ret;
}
