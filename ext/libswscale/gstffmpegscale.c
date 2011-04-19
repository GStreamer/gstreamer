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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_FFMPEG_UNINSTALLED
#include <swscale.h>
#else
#include <libswscale/swscale.h>
#endif

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
  gint in_width, in_height;
  gint out_width, out_height;

  enum PixelFormat in_pixfmt, out_pixfmt;
  struct SwsContext *ctx;

  /* cached auxiliary data */
  gint in_stride[3], in_offset[3];
  gint out_stride[3], out_offset[3];

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
        GST_VIDEO_CAPS_RGB "; " GST_VIDEO_CAPS_BGR "; " \
        GST_VIDEO_CAPS_xRGB "; " GST_VIDEO_CAPS_xBGR "; " \
        GST_VIDEO_CAPS_ARGB "; " GST_VIDEO_CAPS_ABGR "; " \
        GST_VIDEO_CAPS_YUV ("{ I420, YUY2, UYVY, Y41B, Y42B }")
#else
#define VIDEO_CAPS \
        GST_VIDEO_CAPS_RGB "; " GST_VIDEO_CAPS_BGR "; " \
        GST_VIDEO_CAPS_RGBx "; " GST_VIDEO_CAPS_BGRx "; " \
        GST_VIDEO_CAPS_RGBA "; " GST_VIDEO_CAPS_BGRA "; " \
        GST_VIDEO_CAPS_YUV ("{ I420, YUY2, UYVY, Y41B, Y42B }")
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
        g_enum_register_static ("GstFFMpegVideoScaleMethod",
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

GST_BOILERPLATE (GstFFMpegScale, gst_ffmpegscale, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM);

static void gst_ffmpegscale_finalize (GObject * object);
static void gst_ffmpegscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ffmpegscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_ffmpegscale_stop (GstBaseTransform * trans);
static GstCaps *gst_ffmpegscale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps);
static void gst_ffmpegscale_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_ffmpegscale_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, guint * size);
static gboolean gst_ffmpegscale_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_ffmpegscale_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

static gboolean gst_ffmpegscale_handle_src_event (GstPad * pad,
    GstEvent * event);

static void
gst_ffmpegscale_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details_simple (element_class, "FFMPEG Scale element",
      "Filter/Converter/Video",
      "Converts video from one resolution to another",
      "Luca Ognibene <luogni@tin.it>, Mark Nauwelaerts <mnauw@users.sf.net>");
}

static void
gst_ffmpegscale_class_init (GstFFMpegScaleClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->finalize = gst_ffmpegscale_finalize;
  gobject_class->set_property = gst_ffmpegscale_set_property;
  gobject_class->get_property = gst_ffmpegscale_get_property;

  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_FFMPEGSCALE_METHOD, DEFAULT_PROP_METHOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  trans_class->stop = GST_DEBUG_FUNCPTR (gst_ffmpegscale_stop);
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_ffmpegscale_transform_caps);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_ffmpegscale_fixate_caps);
  trans_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_ffmpegscale_get_unit_size);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_ffmpegscale_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_ffmpegscale_transform);

  trans_class->passthrough_on_same_caps = TRUE;
}

static void
gst_ffmpegscale_init (GstFFMpegScale * scale, GstFFMpegScaleClass * klass)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM (scale);

  gst_pad_set_event_function (trans->srcpad, gst_ffmpegscale_handle_src_event);

  scale->method = DEFAULT_PROP_METHOD;
  scale->ctx = NULL;
  scale->in_pixfmt = PIX_FMT_NONE;
  scale->out_pixfmt = PIX_FMT_NONE;
}

static void
gst_ffmpegscale_reset (GstFFMpegScale * scale)
{
  if (scale->ctx != NULL) {
    sws_freeContext (scale->ctx);
    scale->ctx = NULL;
  }

  scale->in_pixfmt = PIX_FMT_NONE;
  scale->out_pixfmt = PIX_FMT_NONE;
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
  GstCaps *rgbcaps;
  GstCaps *graycaps;

  caps = gst_caps_copy (caps);

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);

    gst_structure_set_name (structure, "video/x-raw-yuv");
    gst_structure_remove_field (structure, "format");
    gst_structure_remove_field (structure, "endianness");
    gst_structure_remove_field (structure, "depth");
    gst_structure_remove_field (structure, "bpp");
    gst_structure_remove_field (structure, "red_mask");
    gst_structure_remove_field (structure, "green_mask");
    gst_structure_remove_field (structure, "blue_mask");
    gst_structure_remove_field (structure, "alpha_mask");
    gst_structure_remove_field (structure, "palette_data");
  }

  rgbcaps = gst_caps_copy (caps);

  for (i = 0; i < gst_caps_get_size (rgbcaps); i++) {
    structure = gst_caps_get_structure (rgbcaps, i);

    gst_structure_set_name (structure, "video/x-raw-rgb");
  }
  graycaps = gst_caps_copy (caps);

  for (i = 0; i < gst_caps_get_size (graycaps); i++) {
    structure = gst_caps_get_structure (graycaps, i);

    gst_structure_set_name (structure, "video/x-raw-gray");
  }

  gst_caps_append (caps, graycaps);
  gst_caps_append (caps, rgbcaps);

  return caps;
}

static GstCaps *
gst_ffmpegscale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
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

  gst_caps_merge_structure (ret, gst_structure_copy (structure));

  /* if pixel aspect ratio, make a range of it */
  if ((par = gst_structure_get_value (structure, "pixel-aspect-ratio"))) {
    gst_structure_set (structure,
        "pixel-aspect-ratio", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

    gst_caps_merge_structure (ret, structure);
  } else {
    gst_structure_free (structure);
  }

  /* now also unfix colour space format */
  gst_caps_append (ret, gst_ffmpegscale_caps_remove_format_info (ret));

  GST_DEBUG_OBJECT (trans, "returning caps: %" GST_PTR_FORMAT, ret);

  return ret;
}

static void
gst_ffmpegscale_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;

  g_return_if_fail (gst_caps_is_fixed (caps));

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
    g_return_if_fail (gst_value_is_fixed (from_par));

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
      return;
    }

    gst_structure_get_int (ins, "width", &from_w);
    gst_structure_get_int (ins, "height", &from_h);

    if (!gst_video_calculate_display_ratio (&num, &den, from_w, from_h,
            from_par_n, from_par_d, to_par_n, to_par_d)) {
      GST_ELEMENT_ERROR (trans, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
      return;
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
}

static gboolean
gst_ffmpegscale_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size)
{
  gint width, height;
  GstVideoFormat format;

  if (!gst_video_format_parse_caps (caps, &format, &width, &height))
    return FALSE;

  *size = gst_video_format_get_size (format, width, height);

  GST_DEBUG_OBJECT (trans, "unit size = %d for format %d w %d height %d",
      *size, format, width, height);

  return TRUE;
}

/* Convert a GstCaps (video/raw) to a FFMPEG PixFmt
 */
static enum PixelFormat
gst_ffmpeg_caps_to_pixfmt (const GstCaps * caps)
{
  GstStructure *structure;
  enum PixelFormat pix_fmt = PIX_FMT_NONE;

  GST_DEBUG ("converting caps %" GST_PTR_FORMAT, caps);
  g_return_val_if_fail (gst_caps_get_size (caps) == 1, PIX_FMT_NONE);
  structure = gst_caps_get_structure (caps, 0);

  if (strcmp (gst_structure_get_name (structure), "video/x-raw-yuv") == 0) {
    guint32 fourcc;

    if (gst_structure_get_fourcc (structure, "format", &fourcc)) {
      switch (fourcc) {
        case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
          pix_fmt = PIX_FMT_YUYV422;
          break;
        case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
          pix_fmt = PIX_FMT_UYVY422;
          break;
        case GST_MAKE_FOURCC ('I', '4', '2', '0'):
          pix_fmt = PIX_FMT_YUV420P;
          break;
        case GST_MAKE_FOURCC ('Y', '4', '1', 'B'):
          pix_fmt = PIX_FMT_YUV411P;
          break;
        case GST_MAKE_FOURCC ('Y', '4', '2', 'B'):
          pix_fmt = PIX_FMT_YUV422P;
          break;
        case GST_MAKE_FOURCC ('Y', 'U', 'V', '9'):
          pix_fmt = PIX_FMT_YUV410P;
          break;
      }
    }
  } else if (strcmp (gst_structure_get_name (structure),
          "video/x-raw-rgb") == 0) {
    gint bpp = 0, rmask = 0, endianness = 0;

    if (gst_structure_get_int (structure, "bpp", &bpp) &&
        gst_structure_get_int (structure, "endianness", &endianness) &&
        endianness == G_BIG_ENDIAN) {
      if (gst_structure_get_int (structure, "red_mask", &rmask)) {
        switch (bpp) {
          case 32:
            if (rmask == 0x00ff0000)
              pix_fmt = PIX_FMT_ARGB;
            else if (rmask == 0xff000000)
              pix_fmt = PIX_FMT_RGBA;
            else if (rmask == 0xff00)
              pix_fmt = PIX_FMT_BGRA;
            else if (rmask == 0xff)
              pix_fmt = PIX_FMT_ABGR;
            break;
          case 24:
            if (rmask == 0x0000FF)
              pix_fmt = PIX_FMT_BGR24;
            else
              pix_fmt = PIX_FMT_RGB24;
            break;
          case 16:
            if (endianness == G_BYTE_ORDER)
              pix_fmt = PIX_FMT_RGB565;
            break;
          case 15:
            if (endianness == G_BYTE_ORDER)
              pix_fmt = PIX_FMT_RGB555;
            break;
          default:
            /* nothing */
            break;
        }
      } else {
        if (bpp == 8) {
          pix_fmt = PIX_FMT_PAL8;
        }
      }
    }
  }

  return pix_fmt;
}

static void
gst_ffmpegscale_fill_info (GstFFMpegScale * scale, GstVideoFormat format,
    guint width, guint height, gint stride[], gint offset[])
{
  gint i;

  for (i = 0; i < 3; i++) {
    stride[i] = gst_video_format_get_row_stride (format, i, width);
    offset[i] = gst_video_format_get_component_offset (format, i, width,
        height);
    /* stay close to the ffmpeg offset way */
    if (offset[i] < 3)
      offset[i] = 0;
    GST_DEBUG_OBJECT (scale, "format %d, component %d; stride %d, offset %d",
        format, i, stride[i], offset[i]);
  }
}

static gboolean
gst_ffmpegscale_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstFFMpegScale *scale = GST_FFMPEGSCALE (trans);
  guint mmx_flags, altivec_flags;
  gint swsflags;
  GstVideoFormat in_format, out_format;
  gboolean ok;

  g_return_val_if_fail (scale->method <
      G_N_ELEMENTS (gst_ffmpegscale_method_flags), FALSE);

  if (scale->ctx) {
    sws_freeContext (scale->ctx);
    scale->ctx = NULL;
  }

  ok = gst_video_format_parse_caps (incaps, &in_format, &scale->in_width,
      &scale->in_height);
  ok &= gst_video_format_parse_caps (outcaps, &out_format, &scale->out_width,
      &scale->out_height);
  scale->in_pixfmt = gst_ffmpeg_caps_to_pixfmt (incaps);
  scale->out_pixfmt = gst_ffmpeg_caps_to_pixfmt (outcaps);

  if (!ok || scale->in_pixfmt == PIX_FMT_NONE ||
      scale->out_pixfmt == PIX_FMT_NONE ||
      in_format == GST_VIDEO_FORMAT_UNKNOWN ||
      out_format == GST_VIDEO_FORMAT_UNKNOWN)
    goto refuse_caps;

  GST_DEBUG_OBJECT (scale, "format %d => %d, from=%dx%d -> to=%dx%d", in_format,
      out_format, scale->in_width, scale->in_height, scale->out_width,
      scale->out_height);

  gst_ffmpegscale_fill_info (scale, in_format, scale->in_width,
      scale->in_height, scale->in_stride, scale->in_offset);
  gst_ffmpegscale_fill_info (scale, out_format, scale->out_width,
      scale->out_height, scale->out_stride, scale->out_offset);

#ifdef HAVE_ORC
  mmx_flags = orc_target_get_default_flags (orc_target_get_by_name ("mmx"));
  altivec_flags =
      orc_target_get_default_flags (orc_target_get_by_name ("altivec"));
  swsflags = (mmx_flags & ORC_TARGET_MMX_MMX ? SWS_CPU_CAPS_MMX : 0)
      | (mmx_flags & ORC_TARGET_MMX_MMXEXT ? SWS_CPU_CAPS_MMX2 : 0)
      | (mmx_flags & ORC_TARGET_MMX_3DNOW ? SWS_CPU_CAPS_3DNOW : 0)
      | (altivec_flags & ORC_TARGET_ALTIVEC_ALTIVEC ? SWS_CPU_CAPS_ALTIVEC : 0);
#else
  mmx_flags = 0;
  altivec_flags = 0;
  swsflags = 0;
#endif


  scale->ctx = sws_getContext (scale->in_width, scale->in_height,
      scale->in_pixfmt, scale->out_width, scale->out_height, scale->out_pixfmt,
      swsflags | gst_ffmpegscale_method_flags[scale->method], NULL, NULL, NULL);
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
  guint8 *in_data[3] = { NULL, NULL, NULL };
  guint8 *out_data[3] = { NULL, NULL, NULL };
  gint i;

  for (i = 0; i < 3; i++) {
    /* again; stay close to the ffmpeg offset way */
    if (!i || scale->in_offset[i])
      in_data[i] = GST_BUFFER_DATA (inbuf) + scale->in_offset[i];
    if (!i || scale->out_offset[i])
      out_data[i] = GST_BUFFER_DATA (outbuf) + scale->out_offset[i];
  }

  sws_scale (scale->ctx, (const guint8 **) in_data, scale->in_stride, 0,
      scale->in_height, out_data, scale->out_stride);

  return GST_FLOW_OK;
}

static gboolean
gst_ffmpegscale_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstFFMpegScale *scale;
  GstStructure *structure;
  gdouble pointer;
  gboolean res;

  scale = GST_FFMPEGSCALE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      event =
          GST_EVENT (gst_mini_object_make_writable (GST_MINI_OBJECT (event)));

      structure = (GstStructure *) gst_event_get_structure (event);
      if (gst_structure_get_double (structure, "pointer_x", &pointer)) {
        gst_structure_set (structure,
            "pointer_x", G_TYPE_DOUBLE,
            pointer * scale->in_width / scale->out_width, NULL);
      }
      if (gst_structure_get_double (structure, "pointer_y", &pointer)) {
        gst_structure_set (structure,
            "pointer_y", G_TYPE_DOUBLE,
            pointer * scale->in_height / scale->out_height, NULL);
      }
      break;
    default:
      break;
  }

  res = gst_pad_event_default (pad, event);

  gst_object_unref (scale);

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
  GST_DEBUG_CATEGORY_INIT (ffmpegscale_debug, "ffvideoscale", 0,
      "video scaling element");

#ifdef HAVE_ORC
  orc_init ();
#endif

#ifndef GST_DISABLE_GST_DEBUG
  av_log_set_callback (gst_ffmpeg_log_callback);
#endif

  return gst_element_register (plugin, "ffvideoscale",
      GST_RANK_NONE, GST_TYPE_FFMPEGSCALE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "ffvideoscale",
    "videoscaling element (" FFMPEG_SOURCE ")",
    plugin_init,
    PACKAGE_VERSION, "GPL", "FFMpeg", "http://ffmpeg.sourceforge.net/")
