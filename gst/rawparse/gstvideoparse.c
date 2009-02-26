/* GStreamer
 * Copyright (C) 2006 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * gstvideoparse.c:
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
 * SECTION:element-videoparse
 *
 * Converts a byte stream into video frames.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstvideoparse.h"

typedef enum
{
  GST_VIDEO_PARSE_FORMAT_I420,
  GST_VIDEO_PARSE_FORMAT_YV12,
  GST_VIDEO_PARSE_FORMAT_YUY2,
  GST_VIDEO_PARSE_FORMAT_UYVY,
  GST_VIDEO_PARSE_FORMAT_v210,
  GST_VIDEO_PARSE_FORMAT_RGB = 10,
  GST_VIDEO_PARSE_FORMAT_GRAY
} GstVideoParseFormat;

typedef enum
{
  GST_VIDEO_PARSE_ENDIANNESS_LITTLE = 1234,
  GST_VIDEO_PARSE_ENDIANNESS_BIG = 4321
} GstVideoParseEndianness;

static void gst_video_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_video_parse_get_caps (GstRawParse * rp);

static void gst_video_parse_update_frame_size (GstVideoParse * vp);

GST_DEBUG_CATEGORY_STATIC (gst_video_parse_debug);
#define GST_CAT_DEFAULT gst_video_parse_debug

static const GstElementDetails gst_video_parse_details =
GST_ELEMENT_DETAILS ("Video Parse",
    "Filter/Video",
    "Converts stream into video frames",
    "David Schleef <ds@schleef.org>, "
    "Sebastian Dröge <slomo@circular-chaos.org>");

enum
{
  ARG_0,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_FORMAT,
  ARG_PAR,
  ARG_FRAMERATE,
  ARG_BPP,
  ARG_DEPTH,
  ARG_ENDIANNESS,
  ARG_RED_MASK,
  ARG_GREEN_MASK,
  ARG_BLUE_MASK,
  ARG_ALPHA_MASK
};


#define GST_VIDEO_PARSE_FORMAT (gst_video_parse_format_get_type ())
static GType
gst_video_parse_format_get_type (void)
{
  static GType video_parse_format_type = 0;
  static const GEnumValue format_types[] = {
    {GST_VIDEO_PARSE_FORMAT_I420, "I420", "I420"},
    {GST_VIDEO_PARSE_FORMAT_YV12, "YV12", "YV12"},
    {GST_VIDEO_PARSE_FORMAT_YUY2, "YUY2", "YUY2"},
    {GST_VIDEO_PARSE_FORMAT_UYVY, "UYVY", "UYVY"},
    {GST_VIDEO_PARSE_FORMAT_v210, "v210", "v210"},
    {GST_VIDEO_PARSE_FORMAT_RGB, "RGB", "RGB"},
    {GST_VIDEO_PARSE_FORMAT_GRAY, "GRAY", "GRAY"},
    {0, NULL, NULL}
  };

  if (!video_parse_format_type) {
    video_parse_format_type =
        g_enum_register_static ("GstVideoParseFormat", format_types);
  }

  return video_parse_format_type;
}

#define GST_VIDEO_PARSE_ENDIANNESS (gst_video_parse_endianness_get_type ())
static GType
gst_video_parse_endianness_get_type (void)
{
  static GType video_parse_endianness_type = 0;
  static const GEnumValue endian_types[] = {
    {GST_VIDEO_PARSE_ENDIANNESS_LITTLE, "Little Endian", "little"},
    {GST_VIDEO_PARSE_ENDIANNESS_BIG, "Big Endian", "big"},
    {0, NULL, NULL}
  };

  if (!video_parse_endianness_type) {
    video_parse_endianness_type =
        g_enum_register_static ("GstVideoParseEndianness", endian_types);
  }

  return video_parse_endianness_type;
}

GST_BOILERPLATE (GstVideoParse, gst_video_parse, GstRawParse,
    GST_TYPE_RAW_PARSE);

static void
gst_video_parse_base_init (gpointer g_class)
{
  GstRawParseClass *rp_class = GST_RAW_PARSE_CLASS (g_class);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *caps;

  GST_DEBUG_CATEGORY_INIT (gst_video_parse_debug, "videoparse", 0,
      "videoparse element");

  gst_element_class_set_details (gstelement_class, &gst_video_parse_details);

  caps =
      gst_caps_from_string (GST_VIDEO_CAPS_YUV
      ("{ I420, YV12, YUY2, UYVY, v210 }") ";"
      "video/x-raw-rgb; video/x-raw-gray");

  gst_raw_parse_class_set_src_pad_template (rp_class, caps);
  gst_raw_parse_class_set_multiple_frames_per_buffer (rp_class, FALSE);
  gst_caps_unref (caps);
}

static void
gst_video_parse_class_init (GstVideoParseClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstRawParseClass *rp_class = GST_RAW_PARSE_CLASS (klass);

  gobject_class->set_property = gst_video_parse_set_property;
  gobject_class->get_property = gst_video_parse_get_property;

  rp_class->get_caps = gst_video_parse_get_caps;

  g_object_class_install_property (gobject_class, ARG_WIDTH,
      g_param_spec_int ("width", "Width", "Width of images in raw stream",
          0, INT_MAX, 320, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_HEIGHT,
      g_param_spec_int ("height", "Height", "Height of images in raw stream",
          0, INT_MAX, 240, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_FORMAT,
      g_param_spec_enum ("format", "Format", "Format of images in raw stream",
          GST_VIDEO_PARSE_FORMAT, GST_VIDEO_PARSE_FORMAT_I420,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_FRAMERATE,
      gst_param_spec_fraction ("framerate", "Frame Rate",
          "Frame rate of images in raw stream", 0, 1, 100, 1, 25, 1,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_PAR,
      gst_param_spec_fraction ("pixel_aspect_ratio", "Pixel Aspect Ratio",
          "Pixel aspect ratio of images in raw stream", 1, 100, 100, 1, 1, 1,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BPP,
      g_param_spec_int ("bpp", "Bpp", "Bits per pixel of images in raw stream",
          0, INT_MAX, 24, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_DEPTH,
      g_param_spec_int ("depth", "Depth", "Depth of images in raw stream",
          0, INT_MAX, 24, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_ENDIANNESS,
      g_param_spec_enum ("endianness", "Endianness",
          "Endianness of images in raw stream", GST_VIDEO_PARSE_ENDIANNESS,
          G_BYTE_ORDER, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_RED_MASK,
      g_param_spec_int ("red-mask", "Red mask",
          "Red mask of images in raw stream", INT_MIN, INT_MAX,
          GST_VIDEO_BYTE1_MASK_24_INT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_GREEN_MASK,
      g_param_spec_int ("green-mask", "Green mask",
          "Green mask of images in raw stream", INT_MIN, INT_MAX,
          GST_VIDEO_BYTE2_MASK_24_INT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BLUE_MASK,
      g_param_spec_int ("blue-mask", "Blue mask",
          "Blue mask of images in raw stream", INT_MIN, INT_MAX,
          GST_VIDEO_BYTE3_MASK_24_INT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_ALPHA_MASK,
      g_param_spec_int ("alpha-mask", "Alpha mask",
          "Alpha mask of images in raw stream", INT_MIN, INT_MAX, 0,
          G_PARAM_READWRITE));
}

static void
gst_video_parse_init (GstVideoParse * vp, GstVideoParseClass * g_class)
{
  vp->width = 320;
  vp->height = 240;
  vp->format = GST_VIDEO_PARSE_FORMAT_I420;
  vp->par_n = 1;
  vp->par_d = 1;
  vp->bpp = 24;
  vp->depth = 24;
  vp->endianness = G_BYTE_ORDER;
  vp->red_mask = GST_VIDEO_BYTE1_MASK_24_INT;
  vp->green_mask = GST_VIDEO_BYTE2_MASK_24_INT;
  vp->blue_mask = GST_VIDEO_BYTE3_MASK_24_INT;
  vp->alpha_mask = 0;

  gst_video_parse_update_frame_size (vp);
  gst_raw_parse_set_fps (GST_RAW_PARSE (vp), 25, 1);
}

static void
gst_video_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (object);

  g_return_if_fail (!gst_raw_parse_is_negotiated (GST_RAW_PARSE (vp)));

  switch (prop_id) {
    case ARG_WIDTH:
      vp->width = g_value_get_int (value);
      break;
    case ARG_HEIGHT:
      vp->height = g_value_get_int (value);
      break;
    case ARG_FORMAT:
      vp->format = g_value_get_enum (value);
      break;
    case ARG_FRAMERATE:
      gst_raw_parse_set_fps (GST_RAW_PARSE (vp),
          gst_value_get_fraction_numerator (value),
          gst_value_get_fraction_denominator (value));
      break;
    case ARG_PAR:
      vp->par_n = gst_value_get_fraction_numerator (value);
      vp->par_d = gst_value_get_fraction_denominator (value);
      break;
    case ARG_BPP:
      vp->bpp = g_value_get_int (value);
      break;
    case ARG_DEPTH:
      vp->depth = g_value_get_int (value);
      break;
    case ARG_ENDIANNESS:
      vp->endianness = g_value_get_enum (value);
      break;
    case ARG_RED_MASK:
      vp->red_mask = g_value_get_int (value);
      break;
    case ARG_GREEN_MASK:
      vp->green_mask = g_value_get_int (value);
      break;
    case ARG_BLUE_MASK:
      vp->blue_mask = g_value_get_int (value);
      break;
    case ARG_ALPHA_MASK:
      vp->alpha_mask = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  gst_video_parse_update_frame_size (vp);
}

static void
gst_video_parse_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (object);

  switch (prop_id) {
    case ARG_WIDTH:
      g_value_set_int (value, vp->width);
      break;
    case ARG_HEIGHT:
      g_value_set_int (value, vp->height);
      break;
    case ARG_FORMAT:
      g_value_set_enum (value, vp->format);
      break;
    case ARG_FRAMERATE:{
      gint fps_n, fps_d;

      gst_raw_parse_get_fps (GST_RAW_PARSE (vp), &fps_n, &fps_d);
      gst_value_set_fraction (value, fps_n, fps_d);
      break;
    }
    case ARG_PAR:
      gst_value_set_fraction (value, vp->par_n, vp->par_d);
      break;
    case ARG_BPP:
      g_value_set_int (value, vp->bpp);
      break;
    case ARG_DEPTH:
      g_value_set_int (value, vp->depth);
      break;
    case ARG_ENDIANNESS:
      g_value_set_enum (value, vp->endianness);
      break;
    case ARG_RED_MASK:
      g_value_set_int (value, vp->red_mask);
      break;
    case ARG_GREEN_MASK:
      g_value_set_int (value, vp->green_mask);
      break;
    case ARG_BLUE_MASK:
      g_value_set_int (value, vp->blue_mask);
      break;
    case ARG_ALPHA_MASK:
      g_value_set_int (value, vp->alpha_mask);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static guint32
gst_video_parse_format_to_fourcc (GstVideoParseFormat format)
{
  switch (format) {
    case GST_VIDEO_PARSE_FORMAT_I420:
      return GST_MAKE_FOURCC ('I', '4', '2', '0');
    case GST_VIDEO_PARSE_FORMAT_YV12:
      return GST_MAKE_FOURCC ('Y', 'V', '1', '2');
    case GST_VIDEO_PARSE_FORMAT_YUY2:
      return GST_MAKE_FOURCC ('Y', 'U', 'Y', '2');
    case GST_VIDEO_PARSE_FORMAT_UYVY:
      return GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y');
    case GST_VIDEO_PARSE_FORMAT_v210:
      return GST_MAKE_FOURCC ('v', '2', '1', '0');
    default:
      g_assert_not_reached ();
  }
  return 0;
}

void
gst_video_parse_update_frame_size (GstVideoParse * vp)
{
  gint framesize;

  if (vp->format == GST_VIDEO_PARSE_FORMAT_I420 ||
      vp->format == GST_VIDEO_PARSE_FORMAT_YV12) {
    framesize = GST_ROUND_UP_4 (vp->width) * GST_ROUND_UP_2 (vp->height)
        +
        2 * (GST_ROUND_UP_8 (vp->width) / 2) * (GST_ROUND_UP_2 (vp->height) /
        2);
  } else if (vp->format == GST_VIDEO_PARSE_FORMAT_YUY2
      || vp->format == GST_VIDEO_PARSE_FORMAT_UYVY) {
    framesize = GST_ROUND_UP_4 (vp->width * 2) * vp->height;
  } else if (vp->format == GST_VIDEO_PARSE_FORMAT_v210) {
    framesize = ((vp->width + 47) / 48) * 128 * vp->height;
  } else if (vp->format == GST_VIDEO_PARSE_FORMAT_RGB) {
    framesize = GST_ROUND_UP_4 (vp->width * vp->bpp / 8) * vp->height;
  } else {
    framesize = GST_ROUND_UP_4 (vp->width * vp->bpp / 8) * vp->height;
  }

  gst_raw_parse_set_framesize (GST_RAW_PARSE (vp), framesize);
}

static GstCaps *
gst_video_parse_get_caps (GstRawParse * rp)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (rp);
  GstCaps *caps;

  gint fps_n, fps_d;

  gst_raw_parse_get_fps (rp, &fps_n, &fps_d);

  if (vp->format < GST_VIDEO_PARSE_FORMAT_RGB) {
    caps = gst_caps_new_simple ("video/x-raw-yuv",
        "width", G_TYPE_INT, vp->width,
        "height", G_TYPE_INT, vp->height,
        "format", GST_TYPE_FOURCC,
        gst_video_parse_format_to_fourcc (vp->format), "framerate",
        GST_TYPE_FRACTION, fps_n, fps_d, "pixel-aspect-ratio",
        GST_TYPE_FRACTION, vp->par_n, vp->par_d, NULL);
  } else if (vp->format == GST_VIDEO_PARSE_FORMAT_RGB) {
    caps = gst_caps_new_simple ("video/x-raw-rgb",
        "width", G_TYPE_INT, vp->width,
        "height", G_TYPE_INT, vp->height,
        "bpp", G_TYPE_INT, vp->bpp,
        "depth", G_TYPE_INT, vp->depth,
        "framerate", GST_TYPE_FRACTION, fps_n, fps_d,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, vp->par_n, vp->par_d,
        "red_mask", G_TYPE_INT, vp->red_mask,
        "green_mask", G_TYPE_INT, vp->green_mask,
        "blue_mask", G_TYPE_INT, vp->blue_mask,
        "alpha_mask", G_TYPE_INT, vp->alpha_mask,
        "endianness", G_TYPE_INT, vp->endianness, NULL);
  } else {
    caps = gst_caps_new_simple ("video/x-raw-gray",
        "width", G_TYPE_INT, vp->width,
        "height", G_TYPE_INT, vp->height,
        "bpp", G_TYPE_INT, vp->bpp,
        "depth", G_TYPE_INT, vp->depth,
        "framerate", GST_TYPE_FRACTION, fps_n, fps_d,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, vp->par_n, vp->par_d, NULL);
  }
  return caps;
}
