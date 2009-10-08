/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
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

/*
 * This file was (probably) generated from
 * gstvideotemplate.c,v 1.18 2005/11/14 02:13:34 thomasvs Exp 
 * and
 * $Id: make_filter,v 1.8 2004/04/19 22:51:57 ds Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <string.h>
#include <cog/cog.h>
#include <math.h>
#include <cog/cogvirtframe.h>
#include "gstcogutils.h"

#define GST_TYPE_COGCOLORSPACE \
  (gst_cogcolorspace_get_type())
#define GST_COGCOLORSPACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_COGCOLORSPACE,GstCogcolorspace))
#define GST_COGCOLORSPACE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_COGCOLORSPACE,GstCogcolorspaceClass))
#define GST_IS_COGCOLORSPACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_COGCOLORSPACE))
#define GST_IS_COGCOLORSPACE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_COGCOLORSPACE))

typedef struct _GstCogcolorspace GstCogcolorspace;
typedef struct _GstCogcolorspaceClass GstCogcolorspaceClass;

struct _GstCogcolorspace
{
  GstBaseTransform base_transform;

  int quality;
  CogColorMatrix color_matrix;
};

struct _GstCogcolorspaceClass
{
  GstBaseTransformClass parent_class;

};

GType gst_cogcolorspace_get_type (void);

/* GstCogcolorspace signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_QUALITY 5
#define DEFAULT_COLOR_MATRIX COG_COLOR_MATRIX_UNKNOWN

enum
{
  PROP_0,
  PROP_QUALITY,
  PROP_COLOR_MATRIX
};

static void gst_cogcolorspace_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cogcolorspace_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_cogcolorspace_transform_caps (GstBaseTransform *
    base_transform, GstPadDirection direction, GstCaps * caps);
static GstFlowReturn gst_cogcolorspace_transform (GstBaseTransform *
    base_transform, GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_cogcolorspace_get_unit_size (GstBaseTransform *
    base_transform, GstCaps * caps, guint * size);

static GstStaticPadTemplate gst_cogcolorspace_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV
        ("{ I420, YV12, YUY2, UYVY, AYUV, Y42B, Y444, v216, v210 }")
        ";" GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx ";" GST_VIDEO_CAPS_xRGB
        ";" GST_VIDEO_CAPS_xBGR ";" GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_BGRA
        ";" GST_VIDEO_CAPS_ARGB ";" GST_VIDEO_CAPS_ABGR)
    );

static GstStaticPadTemplate gst_cogcolorspace_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV
        ("{ I420, YV12, YUY2, UYVY, AYUV, Y42B, Y444, v216, v210 }")
        ";" GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx ";" GST_VIDEO_CAPS_xRGB
        ";" GST_VIDEO_CAPS_xBGR ";" GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_BGRA
        ";" GST_VIDEO_CAPS_ARGB ";" GST_VIDEO_CAPS_ABGR)
    );

GST_BOILERPLATE (GstCogcolorspace, gst_cogcolorspace, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM);

GType
gst_cog_color_matrix_get_type (void)
{
  static gsize id = 0;
  static const GEnumValue values[] = {
    {COG_COLOR_MATRIX_UNKNOWN, "unknown",
        "Unknown color matrix (works like sdtv)"},
    {COG_COLOR_MATRIX_HDTV, "hdtv", "High Definition TV color matrix (BT.709)"},
    {COG_COLOR_MATRIX_SDTV, "sdtv",
        "Standard Definition TV color matrix (BT.470)"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&id)) {
    GType tmp = g_enum_register_static ("CogColorMatrix", values);
    g_once_init_leave (&id, tmp);
  }

  return (GType) id;
}

static void
gst_cogcolorspace_base_init (gpointer g_class)
{
  static GstElementDetails compress_details =
      GST_ELEMENT_DETAILS ("YCbCr/RGB format conversion",
      "Filter/Effect/Video",
      "YCbCr/RGB format conversion",
      "David Schleef <ds@schleef.org>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_cogcolorspace_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_cogcolorspace_sink_template));

  gst_element_class_set_details (element_class, &compress_details);
}

static void
gst_cogcolorspace_class_init (GstCogcolorspaceClass * colorspace_class)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *base_transform_class;

  gobject_class = G_OBJECT_CLASS (colorspace_class);
  base_transform_class = GST_BASE_TRANSFORM_CLASS (colorspace_class);
  colorspace_class = GST_COGCOLORSPACE_CLASS (colorspace_class);

  gobject_class->set_property = gst_cogcolorspace_set_property;
  gobject_class->get_property = gst_cogcolorspace_get_property;

  g_object_class_install_property (gobject_class, PROP_QUALITY,
      g_param_spec_int ("quality", "Quality", "Quality",
          0, 10, DEFAULT_QUALITY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_COLOR_MATRIX,
      g_param_spec_enum ("color-matrix", "Color Matrix",
          "Color matrix for YCbCr <-> RGB conversion",
          gst_cog_color_matrix_get_type (),
          DEFAULT_COLOR_MATRIX, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  base_transform_class->transform = gst_cogcolorspace_transform;
  base_transform_class->transform_caps = gst_cogcolorspace_transform_caps;
  base_transform_class->get_unit_size = gst_cogcolorspace_get_unit_size;

  base_transform_class->passthrough_on_same_caps = TRUE;
}

static void
gst_cogcolorspace_init (GstCogcolorspace * colorspace,
    GstCogcolorspaceClass * klass)
{
  GST_DEBUG ("gst_cogcolorspace_init");

  colorspace->quality = DEFAULT_QUALITY;
  colorspace->color_matrix = DEFAULT_COLOR_MATRIX;
}

static void
gst_cogcolorspace_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCogcolorspace *colorspace;

  g_return_if_fail (GST_IS_COGCOLORSPACE (object));
  colorspace = GST_COGCOLORSPACE (object);

  GST_DEBUG ("gst_cogcolorspace_set_property");
  switch (prop_id) {
    case PROP_QUALITY:
      GST_OBJECT_LOCK (colorspace);
      colorspace->quality = g_value_get_int (value);
      GST_OBJECT_UNLOCK (colorspace);
      break;
    case PROP_COLOR_MATRIX:
      GST_OBJECT_LOCK (colorspace);
      colorspace->color_matrix = g_value_get_enum (value);
      GST_OBJECT_UNLOCK (colorspace);
      break;
    default:
      break;
  }
}

static void
gst_cogcolorspace_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstCogcolorspace *colorspace;

  g_return_if_fail (GST_IS_COGCOLORSPACE (object));
  colorspace = GST_COGCOLORSPACE (object);

  switch (prop_id) {
    case PROP_QUALITY:
      GST_OBJECT_LOCK (colorspace);
      g_value_set_int (value, colorspace->quality);
      GST_OBJECT_UNLOCK (colorspace);
      break;
    case PROP_COLOR_MATRIX:
      GST_OBJECT_LOCK (colorspace);
      g_value_set_enum (value, colorspace->color_matrix);
      GST_OBJECT_UNLOCK (colorspace);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#if 0
static void
transform_value (GValue * dest)
{
  GValue fourcc = { 0 };

  g_value_init (dest, GST_TYPE_LIST);
  g_value_init (&fourcc, GST_TYPE_FOURCC);

  gst_value_set_fourcc (&fourcc, GST_MAKE_FOURCC ('I', '4', '2', '0'));
  gst_value_list_append_value (dest, &fourcc);

  gst_value_set_fourcc (&fourcc, GST_MAKE_FOURCC ('Y', 'V', '1', '2'));
  gst_value_list_append_value (dest, &fourcc);

  gst_value_set_fourcc (&fourcc, GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'));
  gst_value_list_append_value (dest, &fourcc);

  gst_value_set_fourcc (&fourcc, GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'));
  gst_value_list_append_value (dest, &fourcc);

  gst_value_set_fourcc (&fourcc, GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'));
  gst_value_list_append_value (dest, &fourcc);

  gst_value_set_fourcc (&fourcc, GST_MAKE_FOURCC ('Y', '4', '2', 'B'));
  gst_value_list_append_value (dest, &fourcc);

  gst_value_set_fourcc (&fourcc, GST_MAKE_FOURCC ('Y', '4', '4', '4'));
  gst_value_list_append_value (dest, &fourcc);

  gst_value_set_fourcc (&fourcc, GST_MAKE_FOURCC ('v', '2', '1', '0'));
  gst_value_list_append_value (dest, &fourcc);

  gst_value_set_fourcc (&fourcc, GST_MAKE_FOURCC ('v', '2', '1', '6'));
  gst_value_list_append_value (dest, &fourcc);

  g_value_unset (&fourcc);
}
#endif

static GstCaps *
gst_cogcolorspace_caps_remove_format_info (GstCaps * caps)
{
  int i;
  GstStructure *structure;
  GstCaps *rgbcaps;

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

  gst_caps_do_simplify (caps);
  rgbcaps = gst_caps_copy (caps);

  for (i = 0; i < gst_caps_get_size (rgbcaps); i++) {
    structure = gst_caps_get_structure (rgbcaps, i);

    gst_structure_set_name (structure, "video/x-raw-rgb");
  }

  gst_caps_append (caps, rgbcaps);

  return caps;
}

static GstCaps *
gst_cogcolorspace_transform_caps (GstBaseTransform * base_transform,
    GstPadDirection direction, GstCaps * caps)
{
#if 0
  int i;
  GstStructure *structure;
  GValue new_value = { 0 };
  const GValue *value;

  caps = gst_caps_copy (caps);

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);

    value = gst_structure_get_value (structure, "format");
    transform_value (&new_value);
    gst_structure_set_value (structure, "format", &new_value);
    g_value_unset (&new_value);
  }

  return caps;
#endif
#if 0
  GstCaps *template;
  GstCaps *result;

  template = gst_ffmpegcsp_codectype_to_caps (CODEC_TYPE_VIDEO, NULL);
  result = gst_caps_intersect (caps, template);
  gst_caps_unref (template);

  gst_caps_append (result, gst_ffmpegcsp_caps_remove_format_info (caps));

  return result;
#endif
  return gst_cogcolorspace_caps_remove_format_info (caps);
}

static gboolean
gst_cogcolorspace_get_unit_size (GstBaseTransform * base_transform,
    GstCaps * caps, guint * size)
{
  int width, height;
  GstVideoFormat format;
  gboolean ret;

  ret = gst_video_format_parse_caps (caps, &format, &width, &height);
  if (!ret)
    return FALSE;

  *size = gst_video_format_get_size (format, width, height);

  return TRUE;
}

static GstFlowReturn
gst_cogcolorspace_transform (GstBaseTransform * base_transform,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstCogcolorspace *compress;
  CogFrame *out_frame;
  CogFrame *frame;
  int width, height;
  uint32_t in_format;
  uint32_t out_format;
  CogFrameFormat new_subsample;
  gboolean ret;

  g_return_val_if_fail (GST_IS_COGCOLORSPACE (base_transform), GST_FLOW_ERROR);
  compress = GST_COGCOLORSPACE (base_transform);

  ret = gst_video_format_parse_caps (inbuf->caps, &in_format, &width, &height);
  ret |=
      gst_video_format_parse_caps (outbuf->caps, &out_format, &width, &height);
  if (!ret) {
    return GST_FLOW_ERROR;
  }

  frame = gst_cog_buffer_wrap (gst_buffer_ref (inbuf),
      in_format, width, height);
  out_frame = gst_cog_buffer_wrap (gst_buffer_ref (outbuf),
      out_format, width, height);

  switch (out_format) {
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_YVYU:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_v210:
    case GST_VIDEO_FORMAT_v216:
      new_subsample = COG_FRAME_FORMAT_U8_422;
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      new_subsample = COG_FRAME_FORMAT_U8_420;
      break;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ABGR:
    default:
      new_subsample = COG_FRAME_FORMAT_U8_444;
      break;
  }

  frame = cog_virt_frame_new_unpack (frame);

  if (gst_video_format_is_yuv (out_format) &&
      gst_video_format_is_rgb (in_format)) {
    frame = cog_virt_frame_new_color_matrix_RGB_to_YCbCr (frame,
        compress->color_matrix, 8);
  }

  frame = cog_virt_frame_new_subsample (frame, new_subsample);

  if (gst_video_format_is_rgb (out_format) &&
      gst_video_format_is_yuv (in_format)) {
    frame = cog_virt_frame_new_color_matrix_YCbCr_to_RGB (frame,
        compress->color_matrix, (compress->quality >= 5) ? 8 : 6);
  }

  switch (out_format) {
    case GST_VIDEO_FORMAT_YUY2:
      frame = cog_virt_frame_new_pack_YUY2 (frame);
      break;
    case GST_VIDEO_FORMAT_UYVY:
      frame = cog_virt_frame_new_pack_UYVY (frame);
      break;
    case GST_VIDEO_FORMAT_AYUV:
      frame = cog_virt_frame_new_pack_AYUV (frame);
      break;
    case GST_VIDEO_FORMAT_v216:
      frame = cog_virt_frame_new_pack_v216 (frame);
      break;
    case GST_VIDEO_FORMAT_v210:
      frame = cog_virt_frame_new_pack_v210 (frame);
      break;
    case GST_VIDEO_FORMAT_RGBx:
      frame = cog_virt_frame_new_pack_RGBx (frame);
      break;
    case GST_VIDEO_FORMAT_xRGB:
      frame = cog_virt_frame_new_pack_xRGB (frame);
      break;
    case GST_VIDEO_FORMAT_BGRx:
      frame = cog_virt_frame_new_pack_BGRx (frame);
      break;
    case GST_VIDEO_FORMAT_xBGR:
      frame = cog_virt_frame_new_pack_xBGR (frame);
      break;
    case GST_VIDEO_FORMAT_RGBA:
      frame = cog_virt_frame_new_pack_RGBA (frame);
      break;
    case GST_VIDEO_FORMAT_ARGB:
      frame = cog_virt_frame_new_pack_ARGB (frame);
      break;
    case GST_VIDEO_FORMAT_BGRA:
      frame = cog_virt_frame_new_pack_BGRA (frame);
      break;
    case GST_VIDEO_FORMAT_ABGR:
      frame = cog_virt_frame_new_pack_ABGR (frame);
      break;
    default:
      break;
  }

  cog_virt_frame_render (frame, out_frame);
  cog_frame_unref (frame);
  cog_frame_unref (out_frame);

  return GST_FLOW_OK;
}
