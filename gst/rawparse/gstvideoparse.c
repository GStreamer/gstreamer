/* GStreamer
 * Copyright (C) 2006 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2007,2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
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

static void gst_video_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_video_parse_get_caps (GstRawParse * rp);
static void gst_video_parse_set_buffer_flags (GstRawParse * rp,
    GstBuffer * buffer);

static void gst_video_parse_update_frame_size (GstVideoParse * vp);

GST_DEBUG_CATEGORY_STATIC (gst_video_parse_debug);
#define GST_CAT_DEFAULT gst_video_parse_debug

enum
{
  PROP_0,
  PROP_FORMAT,
  PROP_WIDTH,
  PROP_HEIGHT,
  PROP_PAR,
  PROP_FRAMERATE,
  PROP_INTERLACED,
  PROP_TOP_FIELD_FIRST
};

#define gst_video_parse_parent_class parent_class
G_DEFINE_TYPE (GstVideoParse, gst_video_parse, GST_TYPE_RAW_PARSE);

static void
gst_video_parse_class_init (GstVideoParseClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstRawParseClass *rp_class = GST_RAW_PARSE_CLASS (klass);
  GstCaps *caps;

  gobject_class->set_property = gst_video_parse_set_property;
  gobject_class->get_property = gst_video_parse_get_property;

  rp_class->get_caps = gst_video_parse_get_caps;
  rp_class->set_buffer_flags = gst_video_parse_set_buffer_flags;

  g_object_class_install_property (gobject_class, PROP_FORMAT,
      g_param_spec_enum ("format", "Format", "Format of images in raw stream",
          GST_TYPE_VIDEO_FORMAT, GST_VIDEO_FORMAT_I420,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_WIDTH,
      g_param_spec_int ("width", "Width", "Width of images in raw stream",
          0, INT_MAX, 320, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_HEIGHT,
      g_param_spec_int ("height", "Height", "Height of images in raw stream",
          0, INT_MAX, 240, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FRAMERATE,
      gst_param_spec_fraction ("framerate", "Frame Rate",
          "Frame rate of images in raw stream", 0, 1, G_MAXINT, 1, 25, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAR,
      gst_param_spec_fraction ("pixel-aspect-ratio", "Pixel Aspect Ratio",
          "Pixel aspect ratio of images in raw stream", 1, 100, 100, 1, 1, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INTERLACED,
      g_param_spec_boolean ("interlaced", "Interlaced flag",
          "True if video is interlaced", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TOP_FIELD_FIRST,
      g_param_spec_boolean ("top-field-first", "Top field first",
          "True if top field is earlier than bottom field", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class, "Video Parse",
      "Filter/Video",
      "Converts stream into video frames",
      "David Schleef <ds@schleef.org>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  caps = gst_caps_from_string ("video/x-raw; video/x-bayer");

  gst_raw_parse_class_set_src_pad_template (rp_class, caps);
  gst_raw_parse_class_set_multiple_frames_per_buffer (rp_class, FALSE);
  gst_caps_unref (caps);

  GST_DEBUG_CATEGORY_INIT (gst_video_parse_debug, "videoparse", 0,
      "videoparse element");
}

static void
gst_video_parse_init (GstVideoParse * vp)
{
  vp->width = 320;
  vp->height = 240;
  vp->format = GST_VIDEO_FORMAT_I420;
  vp->par_n = 1;
  vp->par_d = 1;

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
    case PROP_FORMAT:
      vp->format = g_value_get_enum (value);
      break;
    case PROP_WIDTH:
      vp->width = g_value_get_int (value);
      break;
    case PROP_HEIGHT:
      vp->height = g_value_get_int (value);
      break;
    case PROP_FRAMERATE:
      gst_raw_parse_set_fps (GST_RAW_PARSE (vp),
          gst_value_get_fraction_numerator (value),
          gst_value_get_fraction_denominator (value));
      break;
    case PROP_PAR:
      vp->par_n = gst_value_get_fraction_numerator (value);
      vp->par_d = gst_value_get_fraction_denominator (value);
      break;
    case PROP_INTERLACED:
      vp->interlaced = g_value_get_boolean (value);
      break;
    case PROP_TOP_FIELD_FIRST:
      vp->top_field_first = g_value_get_boolean (value);
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
    case PROP_FORMAT:
      g_value_set_enum (value, vp->format);
      break;
    case PROP_WIDTH:
      g_value_set_int (value, vp->width);
      break;
    case PROP_HEIGHT:
      g_value_set_int (value, vp->height);
      break;
    case PROP_FRAMERATE:{
      gint fps_n, fps_d;

      gst_raw_parse_get_fps (GST_RAW_PARSE (vp), &fps_n, &fps_d);
      gst_value_set_fraction (value, fps_n, fps_d);
      break;
    }
    case PROP_PAR:
      gst_value_set_fraction (value, vp->par_n, vp->par_d);
      break;
    case PROP_INTERLACED:
      g_value_set_boolean (value, vp->interlaced);
      break;
    case PROP_TOP_FIELD_FIRST:
      g_value_set_boolean (value, vp->top_field_first);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

void
gst_video_parse_update_frame_size (GstVideoParse * vp)
{
  gint framesize;
  GstVideoInfo info;

  gst_video_info_init (&info);
  gst_video_info_set_format (&info, vp->format, vp->width, vp->height);
  framesize = GST_VIDEO_INFO_SIZE (&info);

  gst_raw_parse_set_framesize (GST_RAW_PARSE (vp), framesize);
}

static GstCaps *
gst_video_parse_get_caps (GstRawParse * rp)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (rp);
  GstVideoInfo info;
  GstCaps *caps;
  gint fps_n, fps_d;

  gst_raw_parse_get_fps (rp, &fps_n, &fps_d);

  gst_video_info_init (&info);
  gst_video_info_set_format (&info, vp->format, vp->width, vp->height);
  info.fps_n = fps_n;
  info.fps_d = fps_d;
  info.par_n = vp->par_n;
  info.par_d = vp->par_d;
  info.interlace_mode = vp->interlaced ?
      GST_VIDEO_INTERLACE_MODE_INTERLEAVED :
      GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;

  caps = gst_video_info_to_caps (&info);

  return caps;
}

static void
gst_video_parse_set_buffer_flags (GstRawParse * rp, GstBuffer * buffer)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (rp);

  if (vp->interlaced) {
    if (vp->top_field_first) {
      GST_BUFFER_FLAG_SET (buffer, GST_VIDEO_BUFFER_FLAG_TFF);
    } else {
      GST_BUFFER_FLAG_UNSET (buffer, GST_VIDEO_BUFFER_FLAG_TFF);
    }
  }
}
