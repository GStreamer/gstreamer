/* GStreamer libswscale wrapper
 * Copyright (C) 2005 Luca Ognibene <luogni@tin.it>
 * Copyright (C) 2006 Martin Zlomek <martin.zlomek@itonis.tv>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libswscale/swscale.h>

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#ifdef HAVE_ORC
#include <orc/orc.h>
#endif

#include <string.h>

typedef struct _GstFFMpegScale
{
  GstBaseTransform element;

  /* pads */
  GstPad *sinkpad, *srcpad;

  /* state */
  GstVideoInfo in_info, out_info;

  enum AVPixelFormat in_pixfmt, out_pixfmt;
  struct SwsContext *ctx;

  /* property */
  gint method;
} GstFFMpegScale;

typedef struct _GstFFMpegScaleClass
{
  GstBaseTransformClass parent_class;
} GstFFMpegScaleClass;

#define GST_TYPE_FFMPEGSCALE \
	(gst_ffmpegscale_get_type())
#define GST_FFMPEGSCALE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGSCALE,GstFFMpegScale))
#define GST_FFMPEGSCALE_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGSCALE,GstFFMpegScaleClass))
#define GST_IS_FFMPEGSCALE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGSCALE))
#define GST_IS_FFMPEGSCALE_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGSCALE))

GType gst_ffmpegscale_get_type (void);

GST_DEBUG_CATEGORY (ffmpegscale_debug);
#define GST_CAT_DEFAULT ffmpegscale_debug

/* libswscale supported formats depend on endianness */
#if G_BYTE_ORDER == G_BIG_ENDIAN
#define VIDEO_CAPS \
        GST_VIDEO_CAPS_MAKE ("{ RGB, BGR, xRGB, xBGR, ARGB, ABGR, I420, YUY2, UYVY, Y41B, Y42B }")
#else
#define VIDEO_CAPS \
        GST_VIDEO_CAPS_MAKE ("{ RGB, BGR, RGBx, BGRx, RGBA, BGRA, I420, YUY2, UYVY, Y41B, Y42B }")
#endif

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS)
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS)
    );

static gint gst_ffmpegscale_method_flags[] = {
  SWS_FAST_BILINEAR,
  SWS_BILINEAR,
  SWS_BICUBIC,
  SWS_X,
  SWS_POINT,
  SWS_AREA,
  SWS_BICUBLIN,
  SWS_GAUSS,
  SWS_SINC,
  SWS_LANCZOS,
  SWS_SPLINE,
};

#define GST_TYPE_FFMPEGSCALE_METHOD (gst_ffmpegscale_method_get_type())
static GType
gst_ffmpegscale_method_get_type (void)
{
  static GType ffmpegscale_method_type = 0;

  static const GEnumValue ffmpegscale_methods[] = {
    {0, "Fast Bilinear", "fast-bilinear"},
    {1, "Bilinear", "bilinear"},
    {2, "Bicubic", "bicubic"},
    {3, "Experimental", "experimental"},
    {4, "Nearest Neighbour", "nearest-neighbour"},
    {5, "Area", "area"},
    {6, "Luma Bicubic / Chroma Linear", "bicubic-lin"},
    {7, "Gauss", "gauss"},
    {8, "SincR", "sincr"},
    {9, "Lanczos", "lanczos"},
    {10, "Natural Bicubic Spline", "bicubic-spline"},
    {0, NULL, NULL},
  };

  if (!ffmpegscale_method_type) {
    ffmpegscale_method_type =
        g_enum_register_static ("GstLibAVVideoScaleMethod",
        ffmpegscale_methods);
  }
  return ffmpegscale_method_type;
}

#define DEFAULT_PROP_METHOD    2

enum
{
  PROP_0,
  PROP_METHOD
      /* FILL ME */
};

#define gst_ffmpegscale_parent_class parent_class
G_DEFINE_TYPE (GstFFMpegScale, gst_ffmpegscale, GST_TYPE_BASE_TRANSFORM);

static void gst_ffmpegscale_finalize (GObject * object);
static void gst_ffmpegscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ffmpegscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_ffmpegscale_stop (GstBaseTransform * trans);
static GstCaps *gst_ffmpegscale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_ffmpegscale_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_ffmpegscale_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, gsize * size);
static gboolean gst_ffmpegscale_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_ffmpegscale_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

static gboolean gst_ffmpegscale_src_event (GstBaseTransform * trans,
    GstEvent * event);

static void
gst_ffmpegscale_class_init (GstFFMpegScaleClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->finalize = gst_ffmpegscale_finalize;
  gobject_class->set_property = gst_ffmpegscale_set_property;
  gobject_class->get_property = gst_ffmpegscale_get_property;

  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_FFMPEGSCALE_METHOD, DEFAULT_PROP_METHOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);
  gst_element_class_add_static_pad_template (gstelement_class, &sink_factory);

  gst_element_class_set_static_metadata (gstelement_class,
      "libav Scale element", "Filter/Converter/Video",
      "Converts video from one resolution to another",
      "Luca Ognibene <luogni@tin.it>, Mark Nauwelaerts <mnauw@users.sf.net>");

  trans_class->stop = GST_DEBUG_FUNCPTR (gst_ffmpegscale_stop);
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_ffmpegscale_transform_caps);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_ffmpegscale_fixate_caps);
  trans_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_ffmpegscale_get_unit_size);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_ffmpegscale_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_ffmpegscale_transform);
  trans_class->src_event = GST_DEBUG_FUNCPTR (gst_ffmpegscale_src_event);

  trans_class->passthrough_on_same_caps = TRUE;
}

static void
gst_ffmpegscale_init (GstFFMpegScale * scale)
{
  scale->method = DEFAULT_PROP_METHOD;
  scale->ctx = NULL;
  scale->in_pixfmt = AV_PIX_FMT_NONE;
  scale->out_pixfmt = AV_PIX_FMT_NONE;
}

static void
gst_ffmpegscale_reset (GstFFMpegScale * scale)
{
  if (scale->ctx != NULL) {
    sws_freeContext (scale->ctx);
    scale->ctx = NULL;
  }

  scale->in_pixfmt = AV_PIX_FMT_NONE;
  scale->out_pixfmt = AV_PIX_FMT_NONE;
}

static void
gst_ffmpegscale_finalize (GObject * object)
{
  GstFFMpegScale *scale = GST_FFMPEGSCALE (object);

  gst_ffmpegscale_reset (scale);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* copies the given caps */
static GstCaps *
gst_ffmpegscale_caps_remove_format_info (GstCaps * caps)
{
  int i;
  GstStructure *structure;

  caps = gst_caps_copy (caps);

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);

    gst_structure_remove_field (structure, "format");
  }

  return caps;
}

static GstCaps *
gst_ffmpegscale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *ret;
  GstStructure *structure;
  const GValue *par;

  /* this function is always called with a simple caps */
  g_return_val_if_fail (GST_CAPS_IS_SIMPLE (caps), NULL);

  structure = gst_caps_get_structure (caps, 0);

  ret = gst_caps_copy (caps);
  structure = gst_structure_copy (gst_caps_get_structure (ret, 0));

  gst_structure_set (structure,
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

  ret = gst_caps_merge_structure (ret, gst_structure_copy (structure));

  /* if pixel aspect ratio, make a range of it */
  if ((par = gst_structure_get_value (structure, "pixel-aspect-ratio"))) {
    gst_structure_set (structure,
        "pixel-aspect-ratio", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

    ret = gst_caps_merge_structure (ret, structure);
  } else {
    gst_structure_free (structure);
  }

  /* now also unfix colour space format */
  gst_caps_append (ret, gst_ffmpegscale_caps_remove_format_info (ret));

  GST_DEBUG_OBJECT (trans, "returning caps: %" GST_PTR_FORMAT, ret);

  return ret;
}

static GstCaps *
gst_ffmpegscale_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;

  othercaps = gst_caps_make_writable (othercaps);

  GST_DEBUG_OBJECT (trans, "trying to fixate othercaps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, othercaps, caps);

  ins = gst_caps_get_structure (caps, 0);
  outs = gst_caps_get_structure (othercaps, 0);

  from_par = gst_structure_get_value (ins, "pixel-aspect-ratio");
  to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");

  /* we have both PAR but they might not be fixated */
  if (from_par && to_par) {
    gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
    gint count = 0, w = 0, h = 0;
    guint num, den;

    /* from_par should be fixed */
    g_return_val_if_fail (gst_value_is_fixed (from_par), othercaps);

    from_par_n = gst_value_get_fraction_numerator (from_par);
    from_par_d = gst_value_get_fraction_denominator (from_par);

    /* fixate the out PAR */
    if (!gst_value_is_fixed (to_par)) {
      GST_DEBUG_OBJECT (trans, "fixating to_par to %dx%d", from_par_n,
          from_par_d);
      gst_structure_fixate_field_nearest_fraction (outs, "pixel-aspect-ratio",
          from_par_n, from_par_d);
    }

    to_par_n = gst_value_get_fraction_numerator (to_par);
    to_par_d = gst_value_get_fraction_denominator (to_par);

    /* if both width and height are already fixed, we can't do anything
     * about it anymore */
    if (gst_structure_get_int (outs, "width", &w))
      ++count;
    if (gst_structure_get_int (outs, "height", &h))
      ++count;
    if (count == 2) {
      GST_DEBUG_OBJECT (trans, "dimensions already set to %dx%d, not fixating",
          w, h);
      return othercaps;
    }

    gst_structure_get_int (ins, "width", &from_w);
    gst_structure_get_int (ins, "height", &from_h);

    if (!gst_video_calculate_display_ratio (&num, &den, from_w, from_h,
            from_par_n, from_par_d, to_par_n, to_par_d)) {
      GST_ELEMENT_ERROR (trans, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
      return othercaps;
    }

    GST_DEBUG_OBJECT (trans,
        "scaling input with %dx%d and PAR %d/%d to output PAR %d/%d",
        from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d);
    GST_DEBUG_OBJECT (trans, "resulting output should respect ratio of %d/%d",
        num, den);

    /* now find a width x height that respects this display ratio.
     * prefer those that have one of w/h the same as the incoming video
     * using wd / hd = num / den */

    /* if one of the output width or height is fixed, we work from there */
    if (h) {
      GST_DEBUG_OBJECT (trans, "height is fixed,scaling width");
      w = (guint) gst_util_uint64_scale_int (h, num, den);
    } else if (w) {
      GST_DEBUG_OBJECT (trans, "width is fixed, scaling height");
      h = (guint) gst_util_uint64_scale_int (w, den, num);
    } else {
      /* none of width or height is fixed, figure out both of them based only on
       * the input width and height */
      /* check hd / den is an integer scale factor, and scale wd with the PAR */
      if (from_h % den == 0) {
        GST_DEBUG_OBJECT (trans, "keeping video height");
        h = from_h;
        w = (guint) gst_util_uint64_scale_int (h, num, den);
      } else if (from_w % num == 0) {
        GST_DEBUG_OBJECT (trans, "keeping video width");
        w = from_w;
        h = (guint) gst_util_uint64_scale_int (w, den, num);
      } else {
        GST_DEBUG_OBJECT (trans, "approximating but keeping video height");
        h = from_h;
        w = (guint) gst_util_uint64_scale_int (h, num, den);
      }
    }
    GST_DEBUG_OBJECT (trans, "scaling to %dx%d", w, h);

    /* now fixate */
    gst_structure_fixate_field_nearest_int (outs, "width", w);
    gst_structure_fixate_field_nearest_int (outs, "height", h);
  } else {
    gint width, height;

    if (gst_structure_get_int (ins, "width", &width)) {
      if (gst_structure_has_field (outs, "width")) {
        gst_structure_fixate_field_nearest_int (outs, "width", width);
      }
    }
    if (gst_structure_get_int (ins, "height", &height)) {
      if (gst_structure_has_field (outs, "height")) {
        gst_structure_fixate_field_nearest_int (outs, "height", height);
      }
    }
  }

  GST_DEBUG_OBJECT (trans, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);

  return othercaps;
}

static gboolean
gst_ffmpegscale_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    gsize * size)
{
  GstVideoInfo info;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  *size = info.size;

  GST_DEBUG_OBJECT (trans,
      "unit size = %" G_GSIZE_FORMAT " for format %d w %d height %d", *size,
      GST_VIDEO_INFO_FORMAT (&info), GST_VIDEO_INFO_WIDTH (&info),
      GST_VIDEO_INFO_HEIGHT (&info));

  return TRUE;
}

/* Convert a GstCaps (video/raw) to a FFMPEG PixFmt
 */
static enum AVPixelFormat
gst_ffmpeg_caps_to_pixfmt (const GstCaps * caps)
{
  GstVideoInfo info;
  enum AVPixelFormat pix_fmt;

  GST_DEBUG ("converting caps %" GST_PTR_FORMAT, caps);

  if (gst_video_info_from_caps (&info, caps))
    goto invalid_caps;

  switch (GST_VIDEO_INFO_FORMAT (&info)) {
    case GST_VIDEO_FORMAT_YUY2:
      pix_fmt = AV_PIX_FMT_YUYV422;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      pix_fmt = AV_PIX_FMT_UYVY422;
      break;
    case GST_VIDEO_FORMAT_I420:
      pix_fmt = AV_PIX_FMT_YUV420P;
      break;
    case GST_VIDEO_FORMAT_Y41B:
      pix_fmt = AV_PIX_FMT_YUV411P;
      break;
    case GST_VIDEO_FORMAT_Y42B:
      pix_fmt = AV_PIX_FMT_YUV422P;
      break;
    case GST_VIDEO_FORMAT_YUV9:
      pix_fmt = AV_PIX_FMT_YUV410P;
      break;
    case GST_VIDEO_FORMAT_ARGB:
      pix_fmt = AV_PIX_FMT_ARGB;
      break;
    case GST_VIDEO_FORMAT_RGBA:
      pix_fmt = AV_PIX_FMT_RGBA;
      break;
    case GST_VIDEO_FORMAT_BGRA:
      pix_fmt = AV_PIX_FMT_BGRA;
      break;
    case GST_VIDEO_FORMAT_ABGR:
      pix_fmt = AV_PIX_FMT_ABGR;
      break;
    case GST_VIDEO_FORMAT_BGR:
      pix_fmt = AV_PIX_FMT_BGR24;
      break;
    case GST_VIDEO_FORMAT_RGB:
      pix_fmt = AV_PIX_FMT_RGB24;
      break;
    case GST_VIDEO_FORMAT_RGB16:
      pix_fmt = AV_PIX_FMT_RGB565;
      break;
    case GST_VIDEO_FORMAT_RGB15:
      pix_fmt = AV_PIX_FMT_RGB555;
      break;
    case GST_VIDEO_FORMAT_RGB8P:
      pix_fmt = AV_PIX_FMT_PAL8;
      break;
    default:
      pix_fmt = AV_PIX_FMT_NONE;
      break;
  }
  return pix_fmt;

  /* ERROR */
invalid_caps:
  {
    return AV_PIX_FMT_NONE;
  }
}

static gboolean
gst_ffmpegscale_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstFFMpegScale *scale = GST_FFMPEGSCALE (trans);
#ifdef HAVE_ORC
  guint mmx_flags, altivec_flags;
#endif
  gint swsflags;
  gboolean ok;

  g_return_val_if_fail (scale->method <
      G_N_ELEMENTS (gst_ffmpegscale_method_flags), FALSE);

  if (scale->ctx) {
    sws_freeContext (scale->ctx);
    scale->ctx = NULL;
  }

  ok = gst_video_info_from_caps (&scale->in_info, incaps);
  ok &= gst_video_info_from_caps (&scale->out_info, outcaps);

  scale->in_pixfmt = gst_ffmpeg_caps_to_pixfmt (incaps);
  scale->out_pixfmt = gst_ffmpeg_caps_to_pixfmt (outcaps);

  if (!ok || scale->in_pixfmt == AV_PIX_FMT_NONE ||
      scale->out_pixfmt == AV_PIX_FMT_NONE ||
      GST_VIDEO_INFO_FORMAT (&scale->in_info) == GST_VIDEO_FORMAT_UNKNOWN ||
      GST_VIDEO_INFO_FORMAT (&scale->out_info) == GST_VIDEO_FORMAT_UNKNOWN)
    goto refuse_caps;

  GST_DEBUG_OBJECT (scale, "format %d => %d, from=%dx%d -> to=%dx%d",
      GST_VIDEO_INFO_FORMAT (&scale->in_info),
      GST_VIDEO_INFO_FORMAT (&scale->out_info),
      GST_VIDEO_INFO_WIDTH (&scale->in_info),
      GST_VIDEO_INFO_HEIGHT (&scale->in_info),
      GST_VIDEO_INFO_WIDTH (&scale->out_info),
      GST_VIDEO_INFO_HEIGHT (&scale->out_info));

#ifdef HAVE_ORC
  mmx_flags = orc_target_get_default_flags (orc_target_get_by_name ("mmx"));
  altivec_flags =
      orc_target_get_default_flags (orc_target_get_by_name ("altivec"));
  swsflags = (mmx_flags & ORC_TARGET_MMX_MMX ? SWS_CPU_CAPS_MMX : 0)
      | (mmx_flags & ORC_TARGET_MMX_MMXEXT ? SWS_CPU_CAPS_MMX2 : 0)
      | (mmx_flags & ORC_TARGET_MMX_3DNOW ? SWS_CPU_CAPS_3DNOW : 0)
      | (altivec_flags & ORC_TARGET_ALTIVEC_ALTIVEC ? SWS_CPU_CAPS_ALTIVEC : 0);
#else
  swsflags = 0;
#endif

  scale->ctx = sws_getContext (scale->in_info.width, scale->in_info.height,
      scale->in_pixfmt, scale->out_info.width, scale->out_info.height,
      scale->out_pixfmt, swsflags | gst_ffmpegscale_method_flags[scale->method],
      NULL, NULL, NULL);
  if (!scale->ctx)
    goto setup_failed;

  return TRUE;

  /* ERRORS */
setup_failed:
  {
    GST_ELEMENT_ERROR (trans, LIBRARY, INIT, (NULL), (NULL));
    return FALSE;
  }
refuse_caps:
  {
    GST_DEBUG_OBJECT (trans, "refused caps %" GST_PTR_FORMAT, incaps);
    return FALSE;
  }
}

static GstFlowReturn
gst_ffmpegscale_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstFFMpegScale *scale = GST_FFMPEGSCALE (trans);
  GstVideoFrame in_frame, out_frame;

  if (!gst_video_frame_map (&in_frame, &scale->in_info, inbuf, GST_MAP_READ))
    goto invalid_buffer;

  if (!gst_video_frame_map (&out_frame, &scale->out_info, outbuf,
          GST_MAP_WRITE))
    goto invalid_buffer;

  sws_scale (scale->ctx, (const guint8 **) in_frame.data, in_frame.info.stride,
      0, scale->in_info.height, (guint8 **) out_frame.data,
      out_frame.info.stride);

  gst_video_frame_unmap (&in_frame);
  gst_video_frame_unmap (&out_frame);

  return GST_FLOW_OK;

  /* ERRORS */
invalid_buffer:
  {
    return GST_FLOW_OK;
  }
}

static gboolean
gst_ffmpegscale_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstFFMpegScale *scale;
  GstStructure *structure;
  gdouble pointer;
  gboolean res;

  scale = GST_FFMPEGSCALE (trans);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      event = gst_event_make_writable (event);

      structure = gst_event_writable_structure (event);
      if (gst_structure_get_double (structure, "pointer_x", &pointer)) {
        gst_structure_set (structure,
            "pointer_x", G_TYPE_DOUBLE,
            pointer * scale->in_info.width / scale->out_info.width, NULL);
      }
      if (gst_structure_get_double (structure, "pointer_y", &pointer)) {
        gst_structure_set (structure,
            "pointer_y", G_TYPE_DOUBLE,
            pointer * scale->in_info.height / scale->out_info.height, NULL);
      }
      break;
    default:
      break;
  }

  res = GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);

  return res;
}

static gboolean
gst_ffmpegscale_stop (GstBaseTransform * trans)
{
  GstFFMpegScale *scale = GST_FFMPEGSCALE (trans);

  gst_ffmpegscale_reset (scale);

  return TRUE;
}

static void
gst_ffmpegscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFFMpegScale *scale = GST_FFMPEGSCALE (object);

  switch (prop_id) {
    case PROP_METHOD:
      scale->method = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ffmpegscale_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstFFMpegScale *scale = GST_FFMPEGSCALE (object);

  switch (prop_id) {
    case PROP_METHOD:
      g_value_set_enum (value, scale->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#ifndef GST_DISABLE_GST_DEBUG
static void
gst_ffmpeg_log_callback (void *ptr, int level, const char *fmt, va_list vl)
{
  GstDebugLevel gst_level;

  switch (level) {
    case AV_LOG_QUIET:
      gst_level = GST_LEVEL_NONE;
      break;
    case AV_LOG_ERROR:
      gst_level = GST_LEVEL_ERROR;
      break;
    case AV_LOG_INFO:
      gst_level = GST_LEVEL_INFO;
      break;
    case AV_LOG_DEBUG:
      gst_level = GST_LEVEL_DEBUG;
      break;
    default:
      gst_level = GST_LEVEL_INFO;
      break;
  }

  gst_debug_log_valist (ffmpegscale_debug, gst_level, "", "", 0, NULL, fmt, vl);
}
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (ffmpegscale_debug, "avvideoscale", 0,
      "video scaling element");

#ifdef HAVE_ORC
  orc_init ();
#endif

#ifndef GST_DISABLE_GST_DEBUG
  av_log_set_callback (gst_ffmpeg_log_callback);
#endif

  return gst_element_register (plugin, "avvideoscale",
      GST_RANK_NONE, GST_TYPE_FFMPEGSCALE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    avvideoscale,
    "libav videoscaling element (" LIBAV_SOURCE ")", plugin_init,
    PACKAGE_VERSION,
#ifdef GST_LIBAV_ENABLE_GPL
    "GPL",
#else
    "LGPL",
#endif
    "libav", "http://www.libav.org/")
