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
 * SECTION:element-videoconvert
 *
 * Convert video frames between a great variety of video formats.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! video/x-raw,format=\(fourcc\)YUY2 ! videoconvert ! ximagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstvideoconvert.h"
#include <gst/video/video.h>
#include <gst/video/gstmetavideo.h>
#include <gst/video/gstvideopool.h>

#include <string.h>

GST_DEBUG_CATEGORY (videoconvert_debug);
#define GST_CAT_DEFAULT videoconvert_debug
GST_DEBUG_CATEGORY (videoconvert_performance);

enum
{
  PROP_0,
  PROP_DITHER
};

#define CSP_VIDEO_CAPS GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL)

static GstStaticPadTemplate gst_video_convert_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CSP_VIDEO_CAPS)
    );

static GstStaticPadTemplate gst_video_convert_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CSP_VIDEO_CAPS)
    );

GType gst_video_convert_get_type (void);

static void gst_video_convert_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_video_convert_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);

static gboolean gst_video_convert_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_video_convert_get_unit_size (GstBaseTransform * btrans,
    GstCaps * caps, gsize * size);
static GstFlowReturn gst_video_convert_transform (GstBaseTransform * btrans,
    GstBuffer * inbuf, GstBuffer * outbuf);

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
gst_video_convert_caps_remove_format_info (GstCaps * caps)
{
  GstStructure *st;
  gint i, n;
  GstCaps *res;

  res = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    st = gst_caps_get_structure (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure (res, st))
      continue;

    st = gst_structure_copy (st);
    gst_structure_remove_fields (st, "format", "palette_data",
        "colorimetry", "chroma-site", NULL);

    gst_caps_append_structure (res, st);
  }

  return res;
}

/* The caps can be transformed into any other caps with format info removed.
 * However, we should prefer passthrough, so if passthrough is possible,
 * put it first in the list. */
static GstCaps *
gst_video_convert_transform_caps (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *tmp, *tmp2;
  GstCaps *result;

  result = gst_caps_copy (caps);

  /* Get all possible caps that we can transform to */
  tmp = gst_video_convert_caps_remove_format_info (caps);

  if (filter) {
    tmp2 = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
    tmp = tmp2;
  }

  result = tmp;

  GST_DEBUG_OBJECT (btrans, "transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, result);

  return result;
}

static gboolean
gst_video_convert_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstBufferPool *pool = NULL;
  guint size, min, max, prefix, alignment;

  gst_query_parse_allocation_params (query, &size, &min, &max, &prefix,
      &alignment, &pool);

  if (pool) {
    GstStructure *config;

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_META_VIDEO);
    gst_buffer_pool_set_config (pool, config);
  }
  return TRUE;
}

static gboolean
gst_video_convert_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVideoConvert *space;
  GstVideoInfo in_info;
  GstVideoInfo out_info;
  ColorSpaceColorSpec in_spec, out_spec;
  gboolean interlaced;

  space = GST_VIDEO_CONVERT_CAST (btrans);

  if (space->convert) {
    videoconvert_convert_free (space->convert);
  }

  /* input caps */
  if (!gst_video_info_from_caps (&in_info, incaps))
    goto invalid_caps;

  if (in_info.finfo->flags & GST_VIDEO_FORMAT_FLAG_RGB) {
    in_spec = COLOR_SPEC_RGB;
  } else if (in_info.finfo->flags & GST_VIDEO_FORMAT_FLAG_YUV) {
    if (in_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_BT709)
      in_spec = COLOR_SPEC_YUV_BT709;
    else
      in_spec = COLOR_SPEC_YUV_BT470_6;
  } else {
    in_spec = COLOR_SPEC_GRAY;
  }

  /* output caps */
  if (!gst_video_info_from_caps (&out_info, outcaps))
    goto invalid_caps;

  if (out_info.finfo->flags & GST_VIDEO_FORMAT_FLAG_RGB) {
    out_spec = COLOR_SPEC_RGB;
  } else if (out_info.finfo->flags & GST_VIDEO_FORMAT_FLAG_YUV) {
    if (out_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_BT709)
      out_spec = COLOR_SPEC_YUV_BT709;
    else
      out_spec = COLOR_SPEC_YUV_BT470_6;
  } else {
    out_spec = COLOR_SPEC_GRAY;
  }

  /* these must match */
  if (in_info.width != out_info.width || in_info.height != out_info.height ||
      in_info.fps_n != out_info.fps_n || in_info.fps_d != out_info.fps_d)
    goto format_mismatch;

  /* if present, these must match too */
  if (in_info.par_n != out_info.par_n || in_info.par_d != out_info.par_d)
    goto format_mismatch;

  /* if present, these must match too */
  if ((in_info.flags & GST_VIDEO_FLAG_INTERLACED) !=
      (out_info.flags & GST_VIDEO_FLAG_INTERLACED))
    goto format_mismatch;

  space->from_info = in_info;
  space->from_spec = in_spec;
  space->to_info = out_info;
  space->to_spec = out_spec;

  interlaced = (in_info.flags & GST_VIDEO_FLAG_INTERLACED) != 0;

  space->convert =
      videoconvert_convert_new (GST_VIDEO_INFO_FORMAT (&out_info), out_spec,
      GST_VIDEO_INFO_FORMAT (&in_info), in_spec, in_info.width, in_info.height);
  if (space->convert == NULL)
    goto no_convert;

  videoconvert_convert_set_interlaced (space->convert, interlaced);

  /* palette, only for from data */
  if (GST_VIDEO_INFO_FORMAT (&space->from_info) ==
      GST_VIDEO_FORMAT_RGB8_PALETTED
      && GST_VIDEO_INFO_FORMAT (&space->to_info) ==
      GST_VIDEO_FORMAT_RGB8_PALETTED) {
    goto format_mismatch;
  } else if (GST_VIDEO_INFO_FORMAT (&space->from_info) ==
      GST_VIDEO_FORMAT_RGB8_PALETTED) {
    GstBuffer *palette;
    guint32 *data;

    palette = gst_video_parse_caps_palette (incaps);

    if (!palette || gst_buffer_get_size (palette) < 256 * 4) {
      if (palette)
        gst_buffer_unref (palette);
      goto invalid_palette;
    }

    data = gst_buffer_map (palette, NULL, NULL, GST_MAP_READ);
    videoconvert_convert_set_palette (space->convert, data);
    gst_buffer_unmap (palette, data, -1);

    gst_buffer_unref (palette);
  } else if (GST_VIDEO_INFO_FORMAT (&space->to_info) ==
      GST_VIDEO_FORMAT_RGB8_PALETTED) {
    const guint32 *palette;
    GstBuffer *p_buf;

    palette = videoconvert_convert_get_palette (space->convert);

    p_buf = gst_buffer_new_and_alloc (256 * 4);
    gst_buffer_fill (p_buf, 0, palette, 256 * 4);
    gst_caps_set_simple (outcaps, "palette_data", GST_TYPE_BUFFER, p_buf, NULL);
    gst_buffer_unref (p_buf);
  }

  GST_DEBUG ("reconfigured %d %d", GST_VIDEO_INFO_FORMAT (&space->from_info),
      GST_VIDEO_INFO_FORMAT (&space->to_info));

  space->negotiated = TRUE;

  return TRUE;

  /* ERRORS */
invalid_caps:
  {
    GST_ERROR_OBJECT (space, "invalid caps");
    goto error_done;
  }
format_mismatch:
  {
    GST_ERROR_OBJECT (space, "input and output formats do not match");
    goto error_done;
  }
no_convert:
  {
    GST_ERROR_OBJECT (space, "could not create converter");
    goto error_done;
  }
invalid_palette:
  {
    GST_ERROR_OBJECT (space, "invalid palette");
    goto error_done;
  }
error_done:
  {
    space->negotiated = FALSE;
    return FALSE;
  }
}

#define gst_video_convert_parent_class parent_class
G_DEFINE_TYPE (GstVideoConvert, gst_video_convert, GST_TYPE_VIDEO_FILTER);

static void
gst_video_convert_finalize (GObject * obj)
{
  GstVideoConvert *space = GST_VIDEO_CONVERT (obj);

  if (space->convert) {
    videoconvert_convert_free (space->convert);
  }

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_video_convert_class_init (GstVideoConvertClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *gstbasetransform_class =
      (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_video_convert_set_property;
  gobject_class->get_property = gst_video_convert_get_property;
  gobject_class->finalize = gst_video_convert_finalize;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_video_convert_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_video_convert_sink_template));

  gst_element_class_set_details_simple (gstelement_class,
      " Colorspace converter", "Filter/Converter/Video",
      "Converts video from one colorspace to another",
      "GStreamer maintainers <gstreamer-devel@lists.sourceforge.net>");

  gstbasetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_video_convert_transform_caps);
  gstbasetransform_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_video_convert_set_caps);
  gstbasetransform_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_video_convert_get_unit_size);
  gstbasetransform_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_video_convert_decide_allocation);
  gstbasetransform_class->transform =
      GST_DEBUG_FUNCPTR (gst_video_convert_transform);

  gstbasetransform_class->passthrough_on_same_caps = TRUE;

  g_object_class_install_property (gobject_class, PROP_DITHER,
      g_param_spec_enum ("dither", "Dither", "Apply dithering while converting",
          dither_method_get_type (), DITHER_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_video_convert_init (GstVideoConvert * space)
{
  space->negotiated = FALSE;
}

void
gst_video_convert_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoConvert *csp;

  csp = GST_VIDEO_CONVERT (object);

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
gst_video_convert_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoConvert *csp;

  csp = GST_VIDEO_CONVERT (object);

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
gst_video_convert_get_unit_size (GstBaseTransform * btrans, GstCaps * caps,
    gsize * size)
{
  gboolean ret = TRUE;
  GstVideoInfo info;

  g_assert (size);

  ret = gst_video_info_from_caps (&info, caps);
  if (ret) {
    *size = info.size;
  }

  return ret;
}

static GstFlowReturn
gst_video_convert_transform (GstBaseTransform * btrans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVideoConvert *space;
  GstVideoFrame in_frame, out_frame;

  space = GST_VIDEO_CONVERT_CAST (btrans);

  GST_DEBUG ("from %s -> to %s", GST_VIDEO_INFO_NAME (&space->from_info),
      GST_VIDEO_INFO_NAME (&space->to_info));

  if (G_UNLIKELY (!space->negotiated))
    goto unknown_format;

  videoconvert_convert_set_dither (space->convert, space->dither);

  if (!gst_video_frame_map (&in_frame, &space->from_info, inbuf, GST_MAP_READ))
    goto invalid_buffer;

  if (!gst_video_frame_map (&out_frame, &space->to_info, outbuf, GST_MAP_WRITE))
    goto invalid_buffer;

  videoconvert_convert_convert (space->convert, &out_frame, &in_frame);

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  /* baseclass copies timestamps */
  GST_DEBUG ("from %s -> to %s done", GST_VIDEO_INFO_NAME (&space->from_info),
      GST_VIDEO_INFO_NAME (&space->to_info));

  return GST_FLOW_OK;

  /* ERRORS */
unknown_format:
  {
    GST_ELEMENT_ERROR (space, CORE, NOT_IMPLEMENTED, (NULL),
        ("attempting to convert colorspaces between unknown formats"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
invalid_buffer:
  {
    GST_ELEMENT_WARNING (space, CORE, NOT_IMPLEMENTED, (NULL),
        ("invalid video buffer received"));
    return GST_FLOW_OK;
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
  GST_DEBUG_CATEGORY_INIT (videoconvert_debug, "videoconvert", 0,
      "Colorspace Converter");
  GST_DEBUG_CATEGORY_GET (videoconvert_performance, "GST_PERFORMANCE");

  return gst_element_register (plugin, "videoconvert",
      GST_RANK_NONE, GST_TYPE_VIDEO_CONVERT);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videoconvert", "Colorspace conversion", plugin_init, VERSION, "LGPL", "",
    "")
