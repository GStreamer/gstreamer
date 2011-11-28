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
#include "gstcogorc.h"

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

enum
{
  PROP_0,
  PROP_QUALITY
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

static void
gst_cogcolorspace_base_init (gpointer g_class)
{

  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_cogcolorspace_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_cogcolorspace_sink_template);

  gst_element_class_set_details_simple (element_class,
      "YCbCr/RGB format conversion", "Filter/Converter/Video",
      "YCbCr/RGB format conversion", "David Schleef <ds@schleef.org>");
}

static void
gst_cogcolorspace_class_init (GstCogcolorspaceClass * colorspace_class)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *base_transform_class;

  gobject_class = G_OBJECT_CLASS (colorspace_class);
  base_transform_class = GST_BASE_TRANSFORM_CLASS (colorspace_class);

  gobject_class->set_property = gst_cogcolorspace_set_property;
  gobject_class->get_property = gst_cogcolorspace_get_property;

  g_object_class_install_property (gobject_class, PROP_QUALITY,
      g_param_spec_int ("quality", "Quality", "Quality",
          0, 10, DEFAULT_QUALITY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
    gst_structure_remove_field (structure, "color-matrix");
    gst_structure_remove_field (structure, "chroma-site");
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

static CogColorMatrix
gst_cogcolorspace_caps_get_color_matrix (GstCaps * caps)
{
  const char *s;

  s = gst_video_parse_caps_color_matrix (caps);

  if (s == NULL)
    return COG_COLOR_MATRIX_SDTV;

  if (strcmp (s, "sdtv") == 0) {
    return COG_COLOR_MATRIX_SDTV;
  } else if (strcmp (s, "hdtv") == 0) {
    return COG_COLOR_MATRIX_HDTV;
  }

  return COG_COLOR_MATRIX_SDTV;
}

static CogChromaSite
gst_cogcolorspace_caps_get_chroma_site (GstCaps * caps)
{
  const char *s;

  s = gst_video_parse_caps_chroma_site (caps);

  if (s == NULL)
    return COG_COLOR_MATRIX_SDTV;

  if (strcmp (s, "jpeg") == 0) {
    return COG_CHROMA_SITE_JPEG;
  } else if (strcmp (s, "mpeg2") == 0) {
    return COG_CHROMA_SITE_MPEG2;
  }

  return COG_CHROMA_SITE_MPEG2;
}

static void
convert_I420_YUY2 (CogFrame * dest, CogFrame * src)
{
  int i;

  for (i = 0; i < dest->height; i += 2) {
    cogorc_convert_I420_YUY2 (COG_FRAME_DATA_GET_LINE (dest->components + 0, i),
        COG_FRAME_DATA_GET_LINE (dest->components + 0, i + 1),
        COG_FRAME_DATA_GET_LINE (src->components + 0, i),
        COG_FRAME_DATA_GET_LINE (src->components + 0, i + 1),
        COG_FRAME_DATA_GET_LINE (src->components + 1, i >> 1),
        COG_FRAME_DATA_GET_LINE (src->components + 2, i >> 1),
        (dest->width + 1) / 2);
  }
}

static void
convert_I420_UYVY (CogFrame * dest, CogFrame * src)
{
  int i;

  for (i = 0; i < dest->height; i += 2) {
    cogorc_convert_I420_UYVY (COG_FRAME_DATA_GET_LINE (dest->components + 0, i),
        COG_FRAME_DATA_GET_LINE (dest->components + 0, i + 1),
        COG_FRAME_DATA_GET_LINE (src->components + 0, i),
        COG_FRAME_DATA_GET_LINE (src->components + 0, i + 1),
        COG_FRAME_DATA_GET_LINE (src->components + 1, i >> 1),
        COG_FRAME_DATA_GET_LINE (src->components + 2, i >> 1),
        (dest->width + 1) / 2);
  }
}

static void
convert_I420_AYUV (CogFrame * dest, CogFrame * src)
{
  int i;

  for (i = 0; i < dest->height; i += 2) {
    cogorc_convert_I420_AYUV (COG_FRAME_DATA_GET_LINE (dest->components + 0, i),
        COG_FRAME_DATA_GET_LINE (dest->components + 0, i + 1),
        COG_FRAME_DATA_GET_LINE (src->components + 0, i),
        COG_FRAME_DATA_GET_LINE (src->components + 0, i + 1),
        COG_FRAME_DATA_GET_LINE (src->components + 1, i >> 1),
        COG_FRAME_DATA_GET_LINE (src->components + 2, i >> 1), dest->width);
  }
}

static void
convert_I420_Y42B (CogFrame * dest, CogFrame * src)
{
  cogorc_memcpy_2d (dest->components[0].data, dest->components[0].stride,
      src->components[0].data, src->components[0].stride,
      dest->width, dest->height);

  cogorc_planar_chroma_420_422 (dest->components[1].data,
      2 * dest->components[1].stride,
      COG_FRAME_DATA_GET_LINE (dest->components + 2, 1),
      2 * dest->components[1].stride, src->components[1].data,
      src->components[1].stride, (dest->width + 1) / 2, dest->height / 2);

  cogorc_planar_chroma_420_422 (dest->components[2].data,
      2 * dest->components[2].stride,
      COG_FRAME_DATA_GET_LINE (dest->components + 2, 1),
      2 * dest->components[2].stride, src->components[2].data,
      src->components[2].stride, (dest->width + 1) / 2, dest->height / 2);
}

static void
convert_I420_Y444 (CogFrame * dest, CogFrame * src)
{
  cogorc_memcpy_2d (dest->components[0].data, dest->components[0].stride,
      src->components[0].data, src->components[0].stride,
      dest->width, dest->height);

  cogorc_planar_chroma_420_444 (dest->components[1].data,
      2 * dest->components[1].stride,
      COG_FRAME_DATA_GET_LINE (dest->components + 1, 1),
      2 * dest->components[1].stride, src->components[1].data,
      src->components[1].stride, (dest->width + 1) / 2, (dest->height + 1) / 2);

  cogorc_planar_chroma_420_444 (dest->components[2].data,
      2 * dest->components[2].stride,
      COG_FRAME_DATA_GET_LINE (dest->components + 2, 1),
      2 * dest->components[2].stride, src->components[2].data,
      src->components[2].stride, (dest->width + 1) / 2, (dest->height + 1) / 2);
}

static void
convert_YUY2_I420 (CogFrame * dest, CogFrame * src)
{
  int i;

  for (i = 0; i < dest->height; i += 2) {
    cogorc_convert_YUY2_I420 (COG_FRAME_DATA_GET_LINE (dest->components + 0, i),
        COG_FRAME_DATA_GET_LINE (dest->components + 0, i + 1),
        COG_FRAME_DATA_GET_LINE (dest->components + 1, i >> 1),
        COG_FRAME_DATA_GET_LINE (dest->components + 2, i >> 1),
        COG_FRAME_DATA_GET_LINE (src->components + 0, i),
        COG_FRAME_DATA_GET_LINE (src->components + 0, i + 1),
        (dest->width + 1) / 2);
  }
}

static void
convert_YUY2_AYUV (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_YUY2_AYUV (dest->components[0].data,
      dest->components[0].stride, src->components[0].data,
      src->components[0].stride, (dest->width + 1) / 2, dest->height);
}

static void
convert_YUY2_Y42B (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_YUY2_Y42B (dest->components[0].data,
      dest->components[0].stride, dest->components[1].data,
      dest->components[1].stride, dest->components[2].data,
      dest->components[2].stride, src->components[0].data,
      src->components[0].stride, (dest->width + 1) / 2, dest->height);
}

static void
convert_YUY2_Y444 (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_YUY2_Y444 (dest->components[0].data,
      dest->components[0].stride, dest->components[1].data,
      dest->components[1].stride, dest->components[2].data,
      dest->components[2].stride, src->components[0].data,
      src->components[0].stride, (dest->width + 1) / 2, dest->height);
}


static void
convert_UYVY_I420 (CogFrame * dest, CogFrame * src)
{
  int i;

  for (i = 0; i < dest->height; i += 2) {
    cogorc_convert_UYVY_I420 (COG_FRAME_DATA_GET_LINE (dest->components + 0, i),
        COG_FRAME_DATA_GET_LINE (dest->components + 0, i + 1),
        COG_FRAME_DATA_GET_LINE (dest->components + 1, i >> 1),
        COG_FRAME_DATA_GET_LINE (dest->components + 2, i >> 1),
        COG_FRAME_DATA_GET_LINE (src->components + 0, i),
        COG_FRAME_DATA_GET_LINE (src->components + 0, i + 1),
        (dest->width + 1) / 2);
  }
}

static void
convert_UYVY_AYUV (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_UYVY_AYUV (dest->components[0].data,
      dest->components[0].stride, src->components[0].data,
      src->components[0].stride, (dest->width + 1) / 2, dest->height);
}

static void
convert_UYVY_YUY2 (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_UYVY_YUY2 (dest->components[0].data,
      dest->components[0].stride, src->components[0].data,
      src->components[0].stride, (dest->width + 1) / 2, dest->height);
}

static void
convert_UYVY_Y42B (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_UYVY_Y42B (dest->components[0].data,
      dest->components[0].stride, dest->components[1].data,
      dest->components[1].stride, dest->components[2].data,
      dest->components[2].stride, src->components[0].data,
      src->components[0].stride, (dest->width + 1) / 2, dest->height);
}

static void
convert_UYVY_Y444 (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_UYVY_Y444 (dest->components[0].data,
      dest->components[0].stride, dest->components[1].data,
      dest->components[1].stride, dest->components[2].data,
      dest->components[2].stride, src->components[0].data,
      src->components[0].stride, (dest->width + 1) / 2, dest->height);
}

static void
convert_AYUV_I420 (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_AYUV_I420 (COG_FRAME_DATA_GET_LINE (dest->components + 0, 0),
      2 * dest->components[0].stride,
      COG_FRAME_DATA_GET_LINE (dest->components + 0, 1),
      2 * dest->components[0].stride,
      dest->components[1].data, dest->components[1].stride,
      dest->components[2].data, dest->components[2].stride,
      COG_FRAME_DATA_GET_LINE (src->components + 0, 0),
      /* FIXME why not 2* ? */
      src->components[0].stride,
      COG_FRAME_DATA_GET_LINE (src->components + 0, 1),
      src->components[0].stride, dest->width / 2, dest->height / 2);
}

static void
convert_AYUV_YUY2 (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_AYUV_YUY2 (dest->components[0].data,
      dest->components[0].stride, src->components[0].data,
      src->components[0].stride, dest->width / 2, dest->height);
}

static void
convert_AYUV_UYVY (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_AYUV_UYVY (dest->components[0].data,
      dest->components[0].stride, src->components[0].data,
      src->components[0].stride, dest->width / 2, dest->height);
}

static void
convert_AYUV_Y42B (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_AYUV_Y42B (dest->components[0].data,
      dest->components[0].stride, dest->components[1].data,
      dest->components[1].stride, dest->components[2].data,
      dest->components[2].stride, src->components[0].data,
      src->components[0].stride, (dest->width + 1) / 2, dest->height);
}

static void
convert_AYUV_Y444 (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_AYUV_Y444 (dest->components[0].data,
      dest->components[0].stride, dest->components[1].data,
      dest->components[1].stride, dest->components[2].data,
      dest->components[2].stride, src->components[0].data,
      src->components[0].stride, dest->width, dest->height);
}

static void
convert_Y42B_I420 (CogFrame * dest, CogFrame * src)
{
  cogorc_memcpy_2d (dest->components[0].data, dest->components[0].stride,
      src->components[0].data, src->components[0].stride,
      dest->width, dest->height);

  cogorc_planar_chroma_422_420 (dest->components[1].data,
      dest->components[1].stride, src->components[1].data,
      2 * src->components[1].stride,
      COG_FRAME_DATA_GET_LINE (src->components + 1, 1),
      2 * src->components[1].stride, (dest->width + 1) / 2,
      (dest->height + 1) / 2);

  cogorc_planar_chroma_422_420 (dest->components[2].data,
      dest->components[2].stride, src->components[2].data,
      2 * src->components[2].stride,
      COG_FRAME_DATA_GET_LINE (src->components + 2, 1),
      2 * src->components[2].stride, (dest->width + 1) / 2,
      (dest->height + 1) / 2);
}

static void
convert_Y42B_Y444 (CogFrame * dest, CogFrame * src)
{
  cogorc_memcpy_2d (dest->components[0].data, dest->components[0].stride,
      src->components[0].data, src->components[0].stride,
      dest->width, dest->height);

  cogorc_planar_chroma_422_444 (dest->components[1].data,
      dest->components[1].stride, src->components[1].data,
      src->components[1].stride, (dest->width + 1) / 2, dest->height);

  cogorc_planar_chroma_422_444 (dest->components[2].data,
      dest->components[2].stride, src->components[2].data,
      src->components[2].stride, (dest->width + 1) / 2, dest->height);
}

static void
convert_Y42B_YUY2 (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_Y42B_YUY2 (dest->components[0].data,
      dest->components[0].stride, src->components[0].data,
      src->components[0].stride, src->components[1].data,
      src->components[1].stride, src->components[2].data,
      src->components[2].stride, (dest->width + 1) / 2, dest->height);
}

static void
convert_Y42B_UYVY (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_Y42B_UYVY (dest->components[0].data,
      dest->components[0].stride, src->components[0].data,
      src->components[0].stride, src->components[1].data,
      src->components[1].stride, src->components[2].data,
      src->components[2].stride, (dest->width + 1) / 2, dest->height);
}

static void
convert_Y42B_AYUV (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_Y42B_AYUV (dest->components[0].data,
      dest->components[0].stride, src->components[0].data,
      src->components[0].stride, src->components[1].data,
      src->components[1].stride, src->components[2].data,
      src->components[2].stride, (dest->width) / 2, dest->height);
}

static void
convert_Y444_I420 (CogFrame * dest, CogFrame * src)
{
  cogorc_memcpy_2d (dest->components[0].data, dest->components[0].stride,
      src->components[0].data, src->components[0].stride,
      dest->width, dest->height);

  cogorc_planar_chroma_444_420 (dest->components[1].data,
      dest->components[1].stride, src->components[1].data,
      2 * src->components[1].stride,
      COG_FRAME_DATA_GET_LINE (src->components + 1, 1),
      2 * src->components[1].stride, (dest->width + 1) / 2,
      (dest->height + 1) / 2);

  cogorc_planar_chroma_444_420 (dest->components[2].data,
      dest->components[2].stride, src->components[2].data,
      2 * src->components[2].stride,
      COG_FRAME_DATA_GET_LINE (src->components + 2, 1),
      2 * src->components[2].stride, (dest->width + 1) / 2,
      (dest->height + 1) / 2);
}

static void
convert_Y444_Y42B (CogFrame * dest, CogFrame * src)
{
  cogorc_memcpy_2d (dest->components[0].data, dest->components[0].stride,
      src->components[0].data, src->components[0].stride,
      dest->width, dest->height);

  cogorc_planar_chroma_444_422 (dest->components[1].data,
      dest->components[1].stride, src->components[1].data,
      src->components[1].stride, (dest->width + 1) / 2, dest->height);

  cogorc_planar_chroma_444_422 (dest->components[2].data,
      dest->components[2].stride, src->components[2].data,
      src->components[2].stride, (dest->width + 1) / 2, dest->height);
}

static void
convert_Y444_YUY2 (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_Y444_YUY2 (dest->components[0].data,
      dest->components[0].stride, src->components[0].data,
      src->components[0].stride, src->components[1].data,
      src->components[1].stride, src->components[2].data,
      src->components[2].stride, (dest->width + 1) / 2, dest->height);
}

static void
convert_Y444_UYVY (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_Y444_UYVY (dest->components[0].data,
      dest->components[0].stride, src->components[0].data,
      src->components[0].stride, src->components[1].data,
      src->components[1].stride, src->components[2].data,
      src->components[2].stride, (dest->width + 1) / 2, dest->height);
}

static void
convert_Y444_AYUV (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_Y444_AYUV (dest->components[0].data,
      dest->components[0].stride, src->components[0].data,
      src->components[0].stride, src->components[1].data,
      src->components[1].stride, src->components[2].data,
      src->components[2].stride, dest->width, dest->height);
}

static void
convert_AYUV_ARGB (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_AYUV_ARGB (dest->components[0].data,
      dest->components[0].stride, src->components[0].data,
      src->components[0].stride, dest->width, dest->height);
}

static void
convert_AYUV_BGRA (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_AYUV_BGRA (dest->components[0].data,
      dest->components[0].stride, src->components[0].data,
      src->components[0].stride, dest->width, dest->height);
}

static void
convert_AYUV_ABGR (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_AYUV_ABGR (dest->components[0].data,
      dest->components[0].stride, src->components[0].data,
      src->components[0].stride, dest->width, dest->height);
}

static void
convert_AYUV_RGBA (CogFrame * dest, CogFrame * src)
{
  cogorc_convert_AYUV_RGBA (dest->components[0].data,
      dest->components[0].stride, src->components[0].data,
      src->components[0].stride, dest->width, dest->height);
}

static void
convert_I420_BGRA (CogFrame * dest, CogFrame * src)
{
  int i;
  int quality = 0;

  if (quality > 3) {
    for (i = 0; i < dest->height; i++) {
      if (i & 1) {
        cogorc_convert_I420_BGRA_avg (COG_FRAME_DATA_GET_LINE (dest->components
                + 0, i), COG_FRAME_DATA_GET_LINE (src->components + 0, i),
            COG_FRAME_DATA_GET_LINE (src->components + 1, i >> 1),
            COG_FRAME_DATA_GET_LINE (src->components + 1, (i >> 1) + 1),
            COG_FRAME_DATA_GET_LINE (src->components + 2, i >> 1),
            COG_FRAME_DATA_GET_LINE (src->components + 2, (i >> 1) + 1),
            dest->width);
      } else {
        cogorc_convert_I420_BGRA (COG_FRAME_DATA_GET_LINE (dest->components + 0,
                i), COG_FRAME_DATA_GET_LINE (src->components + 0, i),
            COG_FRAME_DATA_GET_LINE (src->components + 1, i >> 1),
            COG_FRAME_DATA_GET_LINE (src->components + 2, i >> 1), dest->width);
      }
    }
  } else {
    for (i = 0; i < dest->height; i++) {
      cogorc_convert_I420_BGRA (COG_FRAME_DATA_GET_LINE (dest->components + 0,
              i), COG_FRAME_DATA_GET_LINE (src->components + 0, i),
          COG_FRAME_DATA_GET_LINE (src->components + 1, i >> 1),
          COG_FRAME_DATA_GET_LINE (src->components + 2, i >> 1), dest->width);
    }
  }
}





typedef struct
{
  uint32_t in_format;
  uint32_t out_format;
  void (*convert) (CogFrame * dest, CogFrame * src);
} CogColorspaceTransform;
static CogColorspaceTransform transforms[] = {
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_YUY2, convert_I420_YUY2},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_UYVY, convert_I420_UYVY},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_AYUV, convert_I420_AYUV},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_Y42B, convert_I420_Y42B},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_Y444, convert_I420_Y444},

  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_I420, convert_YUY2_I420},
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_UYVY, convert_UYVY_YUY2},    /* alias */
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_AYUV, convert_YUY2_AYUV},
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_Y42B, convert_YUY2_Y42B},
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_Y444, convert_YUY2_Y444},

  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_I420, convert_UYVY_I420},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_YUY2, convert_UYVY_YUY2},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_AYUV, convert_UYVY_AYUV},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_Y42B, convert_UYVY_Y42B},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_FORMAT_Y444, convert_UYVY_Y444},

  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_I420, convert_AYUV_I420},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_YUY2, convert_AYUV_YUY2},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_UYVY, convert_AYUV_UYVY},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_Y42B, convert_AYUV_Y42B},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_Y444, convert_AYUV_Y444},

  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_I420, convert_Y42B_I420},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_YUY2, convert_Y42B_YUY2},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_UYVY, convert_Y42B_UYVY},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_AYUV, convert_Y42B_AYUV},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_Y444, convert_Y42B_Y444},

  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_I420, convert_Y444_I420},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_YUY2, convert_Y444_YUY2},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_UYVY, convert_Y444_UYVY},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_AYUV, convert_Y444_AYUV},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_Y42B, convert_Y444_Y42B},

  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_ARGB, convert_AYUV_ARGB},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_BGRA, convert_AYUV_BGRA},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_xRGB, convert_AYUV_ARGB},    /* alias */
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_BGRx, convert_AYUV_BGRA},    /* alias */
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_ABGR, convert_AYUV_ABGR},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_RGBA, convert_AYUV_RGBA},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_xBGR, convert_AYUV_ABGR},    /* alias */
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_RGBx, convert_AYUV_RGBA},    /* alias */

  {GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_BGRA, convert_I420_BGRA},
};

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
  CogColorMatrix in_color_matrix;
  CogColorMatrix out_color_matrix;
  CogChromaSite in_chroma_site;
  CogChromaSite out_chroma_site;

  g_return_val_if_fail (GST_IS_COGCOLORSPACE (base_transform), GST_FLOW_ERROR);
  compress = GST_COGCOLORSPACE (base_transform);

  ret = gst_video_format_parse_caps (inbuf->caps, &in_format, &width, &height);
  ret &=
      gst_video_format_parse_caps (outbuf->caps, &out_format, &width, &height);
  if (!ret) {
    return GST_FLOW_ERROR;
  }

  in_color_matrix = gst_cogcolorspace_caps_get_color_matrix (inbuf->caps);
  out_color_matrix = gst_cogcolorspace_caps_get_color_matrix (outbuf->caps);

  in_chroma_site = gst_cogcolorspace_caps_get_chroma_site (inbuf->caps);
  out_chroma_site = gst_cogcolorspace_caps_get_chroma_site (outbuf->caps);

  frame = gst_cog_buffer_wrap (gst_buffer_ref (inbuf),
      in_format, width, height);
  out_frame = gst_cog_buffer_wrap (gst_buffer_ref (outbuf),
      out_format, width, height);

  if (in_format == out_format) {
    memcpy (GST_BUFFER_DATA (outbuf), GST_BUFFER_DATA (inbuf),
        GST_BUFFER_SIZE (outbuf));
  }

  {
    int i;

    for (i = 0; i < sizeof (transforms) / sizeof (transforms[0]); i++) {
      if (transforms[i].in_format == in_format &&
          transforms[i].out_format == out_format) {
        transforms[i].convert (out_frame, frame);
        cog_frame_unref (frame);
        cog_frame_unref (out_frame);

        return GST_FLOW_OK;
      }
    }

    GST_DEBUG ("no fastpath match %d %d", in_format, out_format);
  }

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
        out_color_matrix, 8);
    frame = cog_virt_frame_new_subsample (frame, new_subsample,
        out_chroma_site, (compress->quality >= 3) ? 2 : 1);
  }

  if (gst_video_format_is_yuv (out_format) &&
      gst_video_format_is_yuv (in_format)) {
    if ((in_color_matrix != out_color_matrix ||
            in_chroma_site != out_chroma_site)) {
      frame = cog_virt_frame_new_subsample (frame, COG_FRAME_FORMAT_U8_444,
          in_chroma_site, (compress->quality >= 5) ? 8 : 6);
      frame = cog_virt_frame_new_color_matrix_YCbCr_to_YCbCr (frame,
          in_color_matrix, out_color_matrix, 8);
    }
    frame = cog_virt_frame_new_subsample (frame, new_subsample,
        in_chroma_site, (compress->quality >= 5) ? 8 : 6);
  }

  if (gst_video_format_is_rgb (out_format) &&
      gst_video_format_is_yuv (in_format)) {
    frame = cog_virt_frame_new_subsample (frame, new_subsample,
        in_chroma_site, (compress->quality >= 3) ? 2 : 1);
    frame = cog_virt_frame_new_color_matrix_YCbCr_to_RGB (frame,
        in_color_matrix, (compress->quality >= 5) ? 8 : 6);
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
