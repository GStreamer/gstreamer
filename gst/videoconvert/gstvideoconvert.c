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
 * gst-launch -v videotestsrc ! video/x-raw,format=\(string\)YUY2 ! videoconvert ! ximagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstvideoconvert.h"

#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include <string.h>

GST_DEBUG_CATEGORY (videoconvert_debug);
#define GST_CAT_DEFAULT videoconvert_debug
GST_DEBUG_CATEGORY_EXTERN (GST_CAT_PERFORMANCE);

GType gst_video_convert_get_type (void);

static GQuark _colorspace_quark;

#define gst_video_convert_parent_class parent_class
G_DEFINE_TYPE (GstVideoConvert, gst_video_convert, GST_TYPE_VIDEO_FILTER);

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

static void gst_video_convert_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_video_convert_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);

static gboolean gst_video_convert_set_info (GstVideoFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info);
static GstFlowReturn gst_video_convert_transform_frame (GstVideoFilter * filter,
    GstVideoFrame * in_frame, GstVideoFrame * out_frame);

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

    gtype = g_enum_register_static ("GstVideoConvertDitherMethod", values);
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
    gst_structure_remove_fields (st, "format",
        "colorimetry", "chroma-site", NULL);

    gst_caps_append_structure (res, st);
  }

  return res;
}

static GstCaps *
gst_video_convert_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstCaps *result;

  GST_DEBUG_OBJECT (trans, "fixating caps %" GST_PTR_FORMAT, othercaps);

  result = gst_caps_intersect (othercaps, caps);
  if (gst_caps_is_empty (result)) {
    gst_caps_unref (result);
    result = othercaps;
  } else {
    gst_caps_unref (othercaps);
  }

  /* fixate remaining fields */
  result = gst_caps_fixate (result);

  return result;
}

static gboolean
gst_video_convert_filter_meta (GstBaseTransform * trans, GstQuery * query,
    GType api, const GstStructure * params)
{
  /* propose all metadata upstream */
  return TRUE;
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
gst_video_convert_transform_meta (GstBaseTransform * trans, GstBuffer * outbuf,
    GstMeta * meta, GstBuffer * inbuf)
{
  const GstMetaInfo *info = meta->info;
  gboolean ret;

  if (gst_meta_api_type_has_tag (info->api, _colorspace_quark)) {
    /* don't copy colorspace specific metadata, FIXME, we need a MetaTransform
     * for the colorspace metadata. */
    ret = FALSE;
  } else {
    /* copy other metadata */
    ret = TRUE;
  }
  return ret;
}

static gboolean
gst_video_convert_set_info (GstVideoFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info)
{
  GstVideoConvert *space;

  space = GST_VIDEO_CONVERT_CAST (filter);

  if (space->convert) {
    videoconvert_convert_free (space->convert);
    space->convert = NULL;
  }

  /* these must match */
  if (in_info->width != out_info->width || in_info->height != out_info->height
      || in_info->fps_n != out_info->fps_n || in_info->fps_d != out_info->fps_d)
    goto format_mismatch;

  /* if present, these must match too */
  if (in_info->par_n != out_info->par_n || in_info->par_d != out_info->par_d)
    goto format_mismatch;

  /* if present, these must match too */
  if (in_info->interlace_mode != out_info->interlace_mode)
    goto format_mismatch;

  space->convert = videoconvert_convert_new (in_info, out_info);
  if (space->convert == NULL)
    goto no_convert;

  GST_DEBUG ("reconfigured %d %d", GST_VIDEO_INFO_FORMAT (in_info),
      GST_VIDEO_INFO_FORMAT (out_info));

  return TRUE;

  /* ERRORS */
format_mismatch:
  {
    GST_ERROR_OBJECT (space, "input and output formats do not match");
    return FALSE;
  }
no_convert:
  {
    GST_ERROR_OBJECT (space, "could not create converter");
    return FALSE;
  }
}

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
  GstVideoFilterClass *gstvideofilter_class = (GstVideoFilterClass *) klass;

  gobject_class->set_property = gst_video_convert_set_property;
  gobject_class->get_property = gst_video_convert_get_property;
  gobject_class->finalize = gst_video_convert_finalize;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_video_convert_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_video_convert_sink_template));

  gst_element_class_set_static_metadata (gstelement_class,
      "Colorspace converter", "Filter/Converter/Video",
      "Converts video from one colorspace to another",
      "GStreamer maintainers <gstreamer-devel@lists.sourceforge.net>");

  gstbasetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_video_convert_transform_caps);
  gstbasetransform_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_video_convert_fixate_caps);
  gstbasetransform_class->filter_meta =
      GST_DEBUG_FUNCPTR (gst_video_convert_filter_meta);
  gstbasetransform_class->transform_meta =
      GST_DEBUG_FUNCPTR (gst_video_convert_transform_meta);

  gstbasetransform_class->passthrough_on_same_caps = TRUE;

  gstvideofilter_class->set_info =
      GST_DEBUG_FUNCPTR (gst_video_convert_set_info);
  gstvideofilter_class->transform_frame =
      GST_DEBUG_FUNCPTR (gst_video_convert_transform_frame);

  g_object_class_install_property (gobject_class, PROP_DITHER,
      g_param_spec_enum ("dither", "Dither", "Apply dithering while converting",
          dither_method_get_type (), DITHER_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_video_convert_init (GstVideoConvert * space)
{
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

static GstFlowReturn
gst_video_convert_transform_frame (GstVideoFilter * filter,
    GstVideoFrame * in_frame, GstVideoFrame * out_frame)
{
  GstVideoConvert *space;

  space = GST_VIDEO_CONVERT_CAST (filter);

  GST_CAT_DEBUG_OBJECT (GST_CAT_PERFORMANCE, filter,
      "doing colorspace conversion from %s -> to %s",
      GST_VIDEO_INFO_NAME (&filter->in_info),
      GST_VIDEO_INFO_NAME (&filter->out_info));

  videoconvert_convert_set_dither (space->convert, space->dither);

  videoconvert_convert_convert (space->convert, out_frame, in_frame);

  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (videoconvert_debug, "videoconvert", 0,
      "Colorspace Converter");

  _colorspace_quark = g_quark_from_static_string ("colorspace");

  return gst_element_register (plugin, "videoconvert",
      GST_RANK_NONE, GST_TYPE_VIDEO_CONVERT);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    videoconvert, "Colorspace conversion", plugin_init, VERSION, GST_LICENSE,
    GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
