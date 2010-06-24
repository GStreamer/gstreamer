/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * This file:
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
 * SECTION:element-ffmpegcolorspace
 *
 * Convert video frames between a great variety of colorspace formats.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! video/x-raw-yuv,format=\(fourcc\)YUY2 ! ffmpegcolorspace ! ximagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstffmpegcolorspace.h"
#include "gstffmpegcodecmap.h"
#include <gst/video/video.h>

GST_DEBUG_CATEGORY (ffmpegcolorspace_debug);
#define GST_CAT_DEFAULT ffmpegcolorspace_debug
GST_DEBUG_CATEGORY (ffmpegcolorspace_performance);

#define FFMPEGCSP_VIDEO_CAPS						\
  "video/x-raw-yuv, width = "GST_VIDEO_SIZE_RANGE" , "			\
  "height="GST_VIDEO_SIZE_RANGE",framerate="GST_VIDEO_FPS_RANGE","	\
  "format= (fourcc) { I420 , NV12 , NV21 , YV12 , YUY2 , Y42B , Y444 , YUV9 , YVU9 , Y41B , Y800 , Y8 , GREY , Y16 , UYVY , YVYU , IYU1 , v308 , AYUV } ;" \
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
  GST_VIDEO_CAPS_RGB_15";"						\
  GST_VIDEO_CAPS_GRAY8";"						\
  GST_VIDEO_CAPS_GRAY16("BIG_ENDIAN")";"				\
  GST_VIDEO_CAPS_GRAY16("LITTLE_ENDIAN")";"

static GstStaticPadTemplate gst_ffmpegcsp_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (FFMPEGCSP_VIDEO_CAPS)
    );

static GstStaticPadTemplate gst_ffmpegcsp_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (FFMPEGCSP_VIDEO_CAPS)
    );

GType gst_ffmpegcsp_get_type (void);

static gboolean gst_ffmpegcsp_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_ffmpegcsp_get_unit_size (GstBaseTransform * btrans,
    GstCaps * caps, guint * size);
static GstFlowReturn gst_ffmpegcsp_transform (GstBaseTransform * btrans,
    GstBuffer * inbuf, GstBuffer * outbuf);

static GQuark _QRAWRGB;         /* "video/x-raw-rgb" */
static GQuark _QRAWYUV;         /* "video/x-raw-yuv" */
static GQuark _QALPHAMASK;      /* "alpha_mask" */

/* copies the given caps */
static GstCaps *
gst_ffmpegcsp_caps_remove_format_info (GstCaps * caps)
{
  GstStructure *yuvst, *rgbst, *grayst;

  /* We know there's only one structure since we're given simple caps */
  caps = gst_caps_copy (caps);

  yuvst = gst_caps_get_structure (caps, 0);

  gst_structure_set_name (yuvst, "video/x-raw-yuv");
  gst_structure_remove_fields (yuvst, "format", "endianness", "depth",
      "bpp", "red_mask", "green_mask", "blue_mask", "alpha_mask",
      "palette_data", NULL);

  rgbst = gst_structure_copy (yuvst);
  gst_structure_set_name (rgbst, "video/x-raw-rgb");

  grayst = gst_structure_copy (rgbst);
  gst_structure_set_name (grayst, "video/x-raw-gray");

  gst_caps_append_structure (caps, rgbst);
  gst_caps_append_structure (caps, grayst);

  return caps;
}


static gboolean
gst_ffmpegcsp_structure_is_alpha (GstStructure * s)
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
gst_ffmpegcsp_transform_caps (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps)
{
  GstCaps *template;
  GstCaps *tmp, *tmp2;
  GstCaps *result;
  GstStructure *s;
  GstCaps *alpha, *non_alpha;

  template = gst_static_pad_template_get_caps (&gst_ffmpegcsp_src_template);
  result = gst_caps_copy (caps);

  /* Get all possible caps that we can transform to */
  tmp = gst_ffmpegcsp_caps_remove_format_info (caps);
  tmp2 = gst_caps_intersect (tmp, template);
  gst_caps_unref (tmp);
  tmp = tmp2;

  /* Now move alpha formats to the beginning if caps is an alpha format
   * or at the end if caps is no alpha format */
  alpha = gst_caps_new_empty ();
  non_alpha = gst_caps_new_empty ();

  while ((s = gst_caps_steal_structure (tmp, 0))) {
    if (gst_ffmpegcsp_structure_is_alpha (s))
      gst_caps_append_structure (alpha, s);
    else
      gst_caps_append_structure (non_alpha, s);
  }

  s = gst_caps_get_structure (caps, 0);
  gst_caps_unref (tmp);

  if (gst_ffmpegcsp_structure_is_alpha (s)) {
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
gst_ffmpegcsp_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstFFMpegCsp *space;
  GstStructure *structure;
  gint in_height, in_width;
  gint out_height, out_width;
  const GValue *in_framerate = NULL;
  const GValue *out_framerate = NULL;
  const GValue *in_par = NULL;
  const GValue *out_par = NULL;
  AVCodecContext *ctx;
  gboolean res;

  space = GST_FFMPEGCSP (btrans);

  /* parse in and output values */
  structure = gst_caps_get_structure (incaps, 0);

  /* we have to have width and height */
  res = gst_structure_get_int (structure, "width", &in_width);
  res &= gst_structure_get_int (structure, "height", &in_height);
  if (!res)
    goto no_width_height;

  /* and framerate */
  in_framerate = gst_structure_get_value (structure, "framerate");
  if (in_framerate == NULL || !GST_VALUE_HOLDS_FRACTION (in_framerate))
    goto no_framerate;

  /* this is optional */
  in_par = gst_structure_get_value (structure, "pixel-aspect-ratio");

  structure = gst_caps_get_structure (outcaps, 0);

  /* we have to have width and height */
  res = gst_structure_get_int (structure, "width", &out_width);
  res &= gst_structure_get_int (structure, "height", &out_height);
  if (!res)
    goto no_width_height;

  /* and framerate */
  out_framerate = gst_structure_get_value (structure, "framerate");
  if (out_framerate == NULL || !GST_VALUE_HOLDS_FRACTION (out_framerate))
    goto no_framerate;

  /* this is optional */
  out_par = gst_structure_get_value (structure, "pixel-aspect-ratio");

  /* these must match */
  if (in_width != out_width || in_height != out_height ||
      gst_value_compare (in_framerate, out_framerate) != GST_VALUE_EQUAL)
    goto format_mismatch;

  /* if present, these must match too */
  if (in_par && out_par
      && gst_value_compare (in_par, out_par) != GST_VALUE_EQUAL)
    goto format_mismatch;

  ctx = avcodec_alloc_context ();

  space->width = ctx->width = in_width;
  space->height = ctx->height = in_height;

  space->interlaced = FALSE;
  gst_structure_get_boolean (structure, "interlaced", &space->interlaced);

  /* get from format */
  ctx->pix_fmt = PIX_FMT_NB;
  gst_ffmpegcsp_caps_with_codectype (CODEC_TYPE_VIDEO, incaps, ctx);
  if (ctx->pix_fmt == PIX_FMT_NB)
    goto invalid_in_caps;
  space->from_pixfmt = ctx->pix_fmt;

  /* palette, only for from data */
  if (space->palette)
    av_free (space->palette);
  space->palette = ctx->palctrl;
  ctx->palctrl = NULL;

  /* get to format */
  ctx->pix_fmt = PIX_FMT_NB;
  gst_ffmpegcsp_caps_with_codectype (CODEC_TYPE_VIDEO, outcaps, ctx);
  if (ctx->pix_fmt == PIX_FMT_NB)
    goto invalid_out_caps;
  space->to_pixfmt = ctx->pix_fmt;

  GST_DEBUG ("reconfigured %d %d", space->from_pixfmt, space->to_pixfmt);

  av_free (ctx);

  return TRUE;

  /* ERRORS */
no_width_height:
  {
    GST_DEBUG_OBJECT (space, "did not specify width or height");
    space->from_pixfmt = PIX_FMT_NB;
    space->to_pixfmt = PIX_FMT_NB;
    return FALSE;
  }
no_framerate:
  {
    GST_DEBUG_OBJECT (space, "did not specify framerate");
    space->from_pixfmt = PIX_FMT_NB;
    space->to_pixfmt = PIX_FMT_NB;
    return FALSE;
  }
format_mismatch:
  {
    GST_DEBUG_OBJECT (space, "input and output formats do not match");
    space->from_pixfmt = PIX_FMT_NB;
    space->to_pixfmt = PIX_FMT_NB;
    return FALSE;
  }
invalid_in_caps:
  {
    GST_DEBUG_OBJECT (space, "could not configure context for input format");
    av_free (ctx);
    space->from_pixfmt = PIX_FMT_NB;
    space->to_pixfmt = PIX_FMT_NB;
    return FALSE;
  }
invalid_out_caps:
  {
    GST_DEBUG_OBJECT (space, "could not configure context for output format");
    av_free (ctx);
    space->from_pixfmt = PIX_FMT_NB;
    space->to_pixfmt = PIX_FMT_NB;
    return FALSE;
  }
}

GST_BOILERPLATE (GstFFMpegCsp, gst_ffmpegcsp, GstVideoFilter,
    GST_TYPE_VIDEO_FILTER);

static void
gst_ffmpegcsp_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ffmpegcsp_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ffmpegcsp_sink_template));

  gst_element_class_set_details_simple (element_class,
      "FFMPEG Colorspace converter", "Filter/Converter/Video",
      "Converts video from one colorspace to another",
      "GStreamer maintainers <gstreamer-devel@lists.sourceforge.net>");

  _QRAWRGB = g_quark_from_string ("video/x-raw-rgb");
  _QRAWYUV = g_quark_from_string ("video/x-raw-yuv");
  _QALPHAMASK = g_quark_from_string ("alpha_mask");
}

static void
gst_ffmpegcsp_finalize (GObject * obj)
{
  GstFFMpegCsp *space = GST_FFMPEGCSP (obj);

  if (space->palette)
    av_free (space->palette);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_ffmpegcsp_class_init (GstFFMpegCspClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *gstbasetransform_class =
      (GstBaseTransformClass *) klass;

  gobject_class->finalize = gst_ffmpegcsp_finalize;

  gstbasetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_ffmpegcsp_transform_caps);
  gstbasetransform_class->set_caps = GST_DEBUG_FUNCPTR (gst_ffmpegcsp_set_caps);
  gstbasetransform_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_ffmpegcsp_get_unit_size);
  gstbasetransform_class->transform =
      GST_DEBUG_FUNCPTR (gst_ffmpegcsp_transform);

  gstbasetransform_class->passthrough_on_same_caps = TRUE;
}

static void
gst_ffmpegcsp_init (GstFFMpegCsp * space, GstFFMpegCspClass * klass)
{
  space->from_pixfmt = space->to_pixfmt = PIX_FMT_NB;
  space->palette = NULL;
}

static gboolean
gst_ffmpegcsp_get_unit_size (GstBaseTransform * btrans, GstCaps * caps,
    guint * size)
{
  GstStructure *structure = NULL;
  AVCodecContext *ctx = NULL;
  gboolean ret = TRUE;
  gint width, height;

  g_assert (size);

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);

  ctx = avcodec_alloc_context ();

  g_assert (ctx != NULL);

  ctx->pix_fmt = PIX_FMT_NB;

  gst_ffmpegcsp_caps_with_codectype (CODEC_TYPE_VIDEO, caps, ctx);

  if (G_UNLIKELY (ctx->pix_fmt == PIX_FMT_NB)) {
    ret = FALSE;
    goto beach;
  }

  *size = avpicture_get_size (ctx->pix_fmt, width, height);

  /* ffmpeg frames have the palette after the frame data, whereas
   * GStreamer currently puts it into the caps as 'palette_data' field,
   * so for paletted data the frame size avpicture_get_size() returns is
   * 1024 bytes larger than what GStreamer expects. */
  if (gst_structure_has_field (structure, "palette_data") &&
      ctx->pix_fmt == PIX_FMT_PAL8) {
    *size -= 4 * 256;           /* = AVPALETTE_SIZE */
  }

beach:

  if (ctx->palctrl)
    av_free (ctx->palctrl);
  av_free (ctx);

  return ret;
}

static GstFlowReturn
gst_ffmpegcsp_transform (GstBaseTransform * btrans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstFFMpegCsp *space;
  gint result;

  space = GST_FFMPEGCSP (btrans);

  GST_DEBUG ("from %d -> to %d", space->from_pixfmt, space->to_pixfmt);

  if (G_UNLIKELY (space->from_pixfmt == PIX_FMT_NB ||
          space->to_pixfmt == PIX_FMT_NB))
    goto unknown_format;

  /* fill from with source data */
  gst_ffmpegcsp_avpicture_fill (&space->from_frame,
      GST_BUFFER_DATA (inbuf), space->from_pixfmt, space->width, space->height,
      space->interlaced);

  /* fill optional palette */
  if (space->palette)
    space->from_frame.data[1] = (uint8_t *) space->palette->palette;

  /* fill target frame */
  gst_ffmpegcsp_avpicture_fill (&space->to_frame,
      GST_BUFFER_DATA (outbuf), space->to_pixfmt, space->width, space->height,
      space->interlaced);

  /* and convert */
  result = img_convert (&space->to_frame, space->to_pixfmt,
      &space->from_frame, space->from_pixfmt, space->width, space->height);
  if (result == -1)
    goto not_supported;

  /* baseclass copies timestamps */
  GST_DEBUG ("from %d -> to %d done", space->from_pixfmt, space->to_pixfmt);

  return GST_FLOW_OK;

  /* ERRORS */
unknown_format:
  {
    GST_ELEMENT_ERROR (space, CORE, NOT_IMPLEMENTED, (NULL),
        ("attempting to convert colorspaces between unknown formats"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
not_supported:
  {
    GST_ELEMENT_ERROR (space, CORE, NOT_IMPLEMENTED, (NULL),
        ("cannot convert between formats"));
    return GST_FLOW_NOT_SUPPORTED;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (ffmpegcolorspace_debug, "ffmpegcolorspace", 0,
      "FFMPEG-based colorspace converter");
  GST_DEBUG_CATEGORY_GET (ffmpegcolorspace_performance, "GST_PERFORMANCE");

  avcodec_init ();

  return gst_element_register (plugin, "ffmpegcolorspace",
      GST_RANK_NONE, GST_TYPE_FFMPEGCSP);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "ffmpegcolorspace",
    "colorspace conversion copied from FFMpeg " FFMPEG_VERSION,
    plugin_init, VERSION, "LGPL", "FFMpeg", "http://ffmpeg.sourceforge.net/")
