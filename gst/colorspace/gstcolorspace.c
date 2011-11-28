/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * This file:
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2010 David Schleef <ds@schleef.org>
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
 * SECTION:element-colorspace
 *
 * Convert video frames between a great variety of colorspace formats.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! video/x-raw-yuv,format=\(fourcc\)YUY2 ! colorspace ! ximagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstcolorspace.h"
#include <gst/video/video.h>

#include <string.h>

GST_DEBUG_CATEGORY (colorspace_debug);
#define GST_CAT_DEFAULT colorspace_debug
GST_DEBUG_CATEGORY (colorspace_performance);

enum
{
  PROP_0,
  PROP_DITHER
};

#define CSP_VIDEO_CAPS						\
  "video/x-raw-yuv, width = "GST_VIDEO_SIZE_RANGE" , "			\
  "height="GST_VIDEO_SIZE_RANGE",framerate="GST_VIDEO_FPS_RANGE","	\
  "format= (fourcc) { I420 , NV12 , NV21 , YV12 , YUY2 , Y42B , Y444 , YUV9 , YVU9 , Y41B , Y800 , Y8 , GREY , Y16 , UYVY , YVYU , IYU1 , v308 , AYUV, v210, v216, A420, AY64 } ;" \
  GST_VIDEO_CAPS_RGB";"							\
  GST_VIDEO_CAPS_BGR";"							\
  GST_VIDEO_CAPS_RGBx";"						\
  GST_VIDEO_CAPS_xRGB";"						\
  GST_VIDEO_CAPS_BGRx";"						\
  GST_VIDEO_CAPS_xBGR";"						\
  GST_VIDEO_CAPS_RGBA";"						\
  GST_VIDEO_CAPS_ARGB";"						\
  GST_VIDEO_CAPS_BGRA";"						\
  GST_VIDEO_CAPS_ABGR";"						\
  GST_VIDEO_CAPS_RGB_16";"						\
  GST_VIDEO_CAPS_BGR_16";"						\
  GST_VIDEO_CAPS_RGB_15";"						\
  GST_VIDEO_CAPS_BGR_15";"						\
  GST_VIDEO_CAPS_RGB8_PALETTED "; "                                     \
  GST_VIDEO_CAPS_GRAY8";"						\
  GST_VIDEO_CAPS_GRAY16("BIG_ENDIAN")";"				\
  GST_VIDEO_CAPS_GRAY16("LITTLE_ENDIAN")";"                             \
  GST_VIDEO_CAPS_r210";"                                                \
  GST_VIDEO_CAPS_ARGB_64

static GstStaticPadTemplate gst_csp_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CSP_VIDEO_CAPS)
    );

static GstStaticPadTemplate gst_csp_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CSP_VIDEO_CAPS)
    );

GType gst_csp_get_type (void);

static void gst_csp_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_csp_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_csp_dispose (GObject * object);

static gboolean gst_csp_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_csp_get_unit_size (GstBaseTransform * btrans,
    GstCaps * caps, guint * size);
static GstFlowReturn gst_csp_transform (GstBaseTransform * btrans,
    GstBuffer * inbuf, GstBuffer * outbuf);

static GQuark _QRAWRGB;         /* "video/x-raw-rgb" */
static GQuark _QRAWYUV;         /* "video/x-raw-yuv" */
static GQuark _QALPHAMASK;      /* "alpha_mask" */


static GType
dither_method_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {DITHER_NONE, "No dithering (default)", "none"},
      {DITHER_VERTERR, "Vertical error propogation", "verterr"},
      {DITHER_HALFTONE, "Half-tone", "halftone"},
      {0, NULL, NULL}
    };

    gtype = g_enum_register_static ("GstColorspaceDitherMethod", values);
  }
  return gtype;
}

/* copies the given caps */
static GstCaps *
gst_csp_caps_remove_format_info (GstCaps * caps)
{
  GstStructure *yuvst, *rgbst, *grayst;

  /* We know there's only one structure since we're given simple caps */
  caps = gst_caps_copy (caps);

  yuvst = gst_caps_get_structure (caps, 0);

  gst_structure_set_name (yuvst, "video/x-raw-yuv");
  gst_structure_remove_fields (yuvst, "format", "endianness", "depth",
      "bpp", "red_mask", "green_mask", "blue_mask", "alpha_mask",
      "palette_data", "color-matrix", NULL);

  rgbst = gst_structure_copy (yuvst);
  gst_structure_set_name (rgbst, "video/x-raw-rgb");
  gst_structure_remove_fields (rgbst, "color-matrix", "chroma-site", NULL);

  grayst = gst_structure_copy (rgbst);
  gst_structure_set_name (grayst, "video/x-raw-gray");

  gst_caps_append_structure (caps, rgbst);
  gst_caps_append_structure (caps, grayst);

  return caps;
}


static gboolean
gst_csp_structure_is_alpha (GstStructure * s)
{
  GQuark name;

  name = gst_structure_get_name_id (s);

  if (name == _QRAWRGB) {
    return gst_structure_id_has_field (s, _QALPHAMASK);
  } else if (name == _QRAWYUV) {
    guint32 fourcc;

    if (!gst_structure_get_fourcc (s, "format", &fourcc))
      return FALSE;

    return (fourcc == GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'));
  }

  return FALSE;
}

/* The caps can be transformed into any other caps with format info removed.
 * However, we should prefer passthrough, so if passthrough is possible,
 * put it first in the list. */
static GstCaps *
gst_csp_transform_caps (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps)
{
  GstCaps *template;
  GstCaps *tmp, *tmp2;
  GstCaps *result;
  GstStructure *s;
  GstCaps *alpha, *non_alpha;

  template = gst_static_pad_template_get_caps (&gst_csp_src_template);
  result = gst_caps_copy (caps);

  /* Get all possible caps that we can transform to */
  tmp = gst_csp_caps_remove_format_info (caps);
  tmp2 = gst_caps_intersect (tmp, template);
  gst_caps_unref (tmp);
  tmp = tmp2;

  /* Now move alpha formats to the beginning if caps is an alpha format
   * or at the end if caps is no alpha format */
  alpha = gst_caps_new_empty ();
  non_alpha = gst_caps_new_empty ();

  while ((s = gst_caps_steal_structure (tmp, 0))) {
    if (gst_csp_structure_is_alpha (s))
      gst_caps_append_structure (alpha, s);
    else
      gst_caps_append_structure (non_alpha, s);
  }

  s = gst_caps_get_structure (caps, 0);
  gst_caps_unref (tmp);

  if (gst_csp_structure_is_alpha (s)) {
    gst_caps_append (alpha, non_alpha);
    tmp = alpha;
  } else {
    gst_caps_append (non_alpha, alpha);
    tmp = non_alpha;
  }

  gst_caps_append (result, tmp);

  GST_DEBUG_OBJECT (btrans, "transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, result);

  return result;
}

static gboolean
gst_csp_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstCsp *space;
  GstVideoFormat in_format;
  GstVideoFormat out_format;
  gint in_height, in_width;
  gint out_height, out_width;
  gint in_fps_n, in_fps_d, in_par_n, in_par_d;
  gint out_fps_n, out_fps_d, out_par_n, out_par_d;
  gboolean have_in_par, have_out_par;
  gboolean have_in_interlaced, have_out_interlaced;
  gboolean in_interlaced, out_interlaced;
  gboolean ret;
  ColorSpaceColorSpec in_spec, out_spec;

  space = GST_CSP (btrans);

  if (space->convert) {
    colorspace_convert_free (space->convert);
  }

  /* input caps */

  ret = gst_video_format_parse_caps (incaps, &in_format, &in_width, &in_height);
  if (!ret)
    goto no_width_height;

  ret = gst_video_parse_caps_framerate (incaps, &in_fps_n, &in_fps_d);
  if (!ret)
    goto no_framerate;

  have_in_par = gst_video_parse_caps_pixel_aspect_ratio (incaps,
      &in_par_n, &in_par_d);
  have_in_interlaced = gst_video_format_parse_caps_interlaced (incaps,
      &in_interlaced);

  if (gst_video_format_is_rgb (in_format)) {
    in_spec = COLOR_SPEC_RGB;
  } else if (gst_video_format_is_yuv (in_format)) {
    const gchar *matrix = gst_video_parse_caps_color_matrix (incaps);

    if (matrix && g_str_equal (matrix, "hdtv"))
      in_spec = COLOR_SPEC_YUV_BT709;
    else if (matrix && g_str_equal (matrix, "jpeg"))
      in_spec = COLOR_SPEC_YUV_JPEG;
    else
      in_spec = COLOR_SPEC_YUV_BT470_6;
  } else {
    in_spec = COLOR_SPEC_GRAY;
  }

  /* output caps */

  ret =
      gst_video_format_parse_caps (outcaps, &out_format, &out_width,
      &out_height);
  if (!ret)
    goto no_width_height;

  ret = gst_video_parse_caps_framerate (outcaps, &out_fps_n, &out_fps_d);
  if (!ret)
    goto no_framerate;

  have_out_par = gst_video_parse_caps_pixel_aspect_ratio (outcaps,
      &out_par_n, &out_par_d);
  have_out_interlaced = gst_video_format_parse_caps_interlaced (incaps,
      &out_interlaced);

  if (gst_video_format_is_rgb (out_format)) {
    out_spec = COLOR_SPEC_RGB;
  } else if (gst_video_format_is_yuv (out_format)) {
    const gchar *matrix = gst_video_parse_caps_color_matrix (outcaps);

    if (matrix && g_str_equal (matrix, "hdtv"))
      out_spec = COLOR_SPEC_YUV_BT709;
    else if (matrix && g_str_equal (matrix, "jpeg"))
      out_spec = COLOR_SPEC_YUV_JPEG;
    else
      out_spec = COLOR_SPEC_YUV_BT470_6;
  } else {
    out_spec = COLOR_SPEC_GRAY;
  }

  /* these must match */
  if (in_width != out_width || in_height != out_height ||
      in_fps_n != out_fps_n || in_fps_d != out_fps_d)
    goto format_mismatch;

  /* if present, these must match too */
  if (have_in_par && have_out_par &&
      (in_par_n != out_par_n || in_par_d != out_par_d))
    goto format_mismatch;

  /* if present, these must match too */
  if (have_in_interlaced && have_out_interlaced &&
      in_interlaced != out_interlaced)
    goto format_mismatch;

  space->from_format = in_format;
  space->from_spec = in_spec;
  space->to_format = out_format;
  space->to_spec = out_spec;
  space->width = in_width;
  space->height = in_height;
  space->interlaced = in_interlaced;

  space->convert = colorspace_convert_new (out_format, out_spec, in_format,
      in_spec, in_width, in_height);
  if (space->convert) {
    colorspace_convert_set_interlaced (space->convert, in_interlaced);
  }
  /* palette, only for from data */
  if (space->from_format == GST_VIDEO_FORMAT_RGB8_PALETTED &&
      space->to_format == GST_VIDEO_FORMAT_RGB8_PALETTED) {
    goto format_mismatch;
  } else if (space->from_format == GST_VIDEO_FORMAT_RGB8_PALETTED) {
    GstBuffer *palette;

    palette = gst_video_parse_caps_palette (incaps);

    if (!palette || GST_BUFFER_SIZE (palette) < 256 * 4) {
      if (palette)
        gst_buffer_unref (palette);
      goto invalid_palette;
    }
    colorspace_convert_set_palette (space->convert,
        (const guint32 *) GST_BUFFER_DATA (palette));
    gst_buffer_unref (palette);
  } else if (space->to_format == GST_VIDEO_FORMAT_RGB8_PALETTED) {
    const guint32 *palette;
    GstBuffer *p_buf;

    palette = colorspace_convert_get_palette (space->convert);
    p_buf = gst_buffer_new_and_alloc (256 * 4);
    memcpy (GST_BUFFER_DATA (p_buf), palette, 256 * 4);
    gst_caps_set_simple (outcaps, "palette_data", GST_TYPE_BUFFER, p_buf, NULL);
    gst_buffer_unref (p_buf);
  }

  GST_DEBUG ("reconfigured %d %d", space->from_format, space->to_format);

  return TRUE;

  /* ERRORS */
no_width_height:
  {
    GST_ERROR_OBJECT (space, "did not specify width or height");
    space->from_format = GST_VIDEO_FORMAT_UNKNOWN;
    space->to_format = GST_VIDEO_FORMAT_UNKNOWN;
    return FALSE;
  }
no_framerate:
  {
    GST_ERROR_OBJECT (space, "did not specify framerate");
    space->from_format = GST_VIDEO_FORMAT_UNKNOWN;
    space->to_format = GST_VIDEO_FORMAT_UNKNOWN;
    return FALSE;
  }
format_mismatch:
  {
    GST_ERROR_OBJECT (space, "input and output formats do not match");
    space->from_format = GST_VIDEO_FORMAT_UNKNOWN;
    space->to_format = GST_VIDEO_FORMAT_UNKNOWN;
    return FALSE;
  }
invalid_palette:
  {
    GST_ERROR_OBJECT (space, "invalid palette");
    space->from_format = GST_VIDEO_FORMAT_UNKNOWN;
    space->to_format = GST_VIDEO_FORMAT_UNKNOWN;
    return FALSE;
  }
}

GST_BOILERPLATE (GstCsp, gst_csp, GstVideoFilter, GST_TYPE_VIDEO_FILTER);

static void
gst_csp_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_csp_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_csp_sink_template);

  gst_element_class_set_details_simple (element_class,
      " Colorspace converter", "Filter/Converter/Video",
      "Converts video from one colorspace to another",
      "GStreamer maintainers <gstreamer-devel@lists.sourceforge.net>");

  _QRAWRGB = g_quark_from_string ("video/x-raw-rgb");
  _QRAWYUV = g_quark_from_string ("video/x-raw-yuv");
  _QALPHAMASK = g_quark_from_string ("alpha_mask");
}

void
gst_csp_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_csp_finalize (GObject * obj)
{
  GstCsp *space = GST_CSP (obj);

  if (space->convert) {
    colorspace_convert_free (space->convert);
  }

  G_OBJECT_CLASS (parent_class)->finalize (obj);

}

static void
gst_csp_class_init (GstCspClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *gstbasetransform_class =
      (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_csp_set_property;
  gobject_class->get_property = gst_csp_get_property;
  gobject_class->dispose = gst_csp_dispose;
  gobject_class->finalize = gst_csp_finalize;

  gstbasetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_csp_transform_caps);
  gstbasetransform_class->set_caps = GST_DEBUG_FUNCPTR (gst_csp_set_caps);
  gstbasetransform_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_csp_get_unit_size);
  gstbasetransform_class->transform = GST_DEBUG_FUNCPTR (gst_csp_transform);

  gstbasetransform_class->passthrough_on_same_caps = TRUE;

  g_object_class_install_property (gobject_class, PROP_DITHER,
      g_param_spec_enum ("dither", "Dither", "Apply dithering while converting",
          dither_method_get_type (), DITHER_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

}

static void
gst_csp_init (GstCsp * space, GstCspClass * klass)
{
  space->from_format = GST_VIDEO_FORMAT_UNKNOWN;
  space->to_format = GST_VIDEO_FORMAT_UNKNOWN;
}

void
gst_csp_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCsp *csp;

  g_return_if_fail (GST_IS_CSP (object));
  csp = GST_CSP (object);

  switch (property_id) {
    case PROP_DITHER:
      csp->dither = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_csp_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstCsp *csp;

  g_return_if_fail (GST_IS_CSP (object));
  csp = GST_CSP (object);

  switch (property_id) {
    case PROP_DITHER:
      g_value_set_enum (value, csp->dither);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static gboolean
gst_csp_get_unit_size (GstBaseTransform * btrans, GstCaps * caps, guint * size)
{
  gboolean ret = TRUE;
  GstVideoFormat format;
  gint width, height;

  g_assert (size);

  ret = gst_video_format_parse_caps (caps, &format, &width, &height);
  if (ret) {
    *size = gst_video_format_get_size (format, width, height);
  }

  return ret;
}

static GstFlowReturn
gst_csp_transform (GstBaseTransform * btrans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstCsp *space;

  space = GST_CSP (btrans);

  GST_DEBUG ("from %d -> to %d", space->from_format, space->to_format);

  if (G_UNLIKELY (space->from_format == GST_VIDEO_FORMAT_UNKNOWN ||
          space->to_format == GST_VIDEO_FORMAT_UNKNOWN))
    goto unknown_format;

  colorspace_convert_set_dither (space->convert, space->dither);

  colorspace_convert_convert (space->convert, GST_BUFFER_DATA (outbuf),
      GST_BUFFER_DATA (inbuf));

  /* baseclass copies timestamps */
  GST_DEBUG ("from %d -> to %d done", space->from_format, space->to_format);

  return GST_FLOW_OK;

  /* ERRORS */
unknown_format:
  {
    GST_ELEMENT_ERROR (space, CORE, NOT_IMPLEMENTED, (NULL),
        ("attempting to convert colorspaces between unknown formats"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
#if 0
not_supported:
  {
    GST_ELEMENT_ERROR (space, CORE, NOT_IMPLEMENTED, (NULL),
        ("cannot convert between formats"));
    return GST_FLOW_NOT_SUPPORTED;
  }
#endif
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (colorspace_debug, "colorspace", 0,
      "Colorspace Converter");
  GST_DEBUG_CATEGORY_GET (colorspace_performance, "GST_PERFORMANCE");

  return gst_element_register (plugin, "colorspace",
      GST_RANK_NONE, GST_TYPE_CSP);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "colorspace", "Colorspace conversion", plugin_init, VERSION, "LGPL", "", "")
