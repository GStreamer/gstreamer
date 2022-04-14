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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-videoconvert
 * @title: videoconvert
 *
 * Convert video frames between a great variety of video formats.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -v videotestsrc ! video/x-raw,format=YUY2 ! videoconvert ! autovideosink
 * ]|
 *  This will output a test video (generated in YUY2 format) in a video
 * window. If the video sink selected does not support YUY2 videoconvert will
 * automatically convert the video to a format understood by the video sink.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideoconvert.h"

G_DEFINE_TYPE (GstVideoConvert, gst_video_convert,
    GST_TYPE_VIDEO_CONVERT_SCALE);
GST_ELEMENT_REGISTER_DEFINE (videoconvert, "videoconvert",
    GST_RANK_MARGINAL, gst_video_convert_get_type ());

enum
{
  PROP_0,
  PROP_DISABLE_SCALING,
};

#define DEFAULT_PROP_DISABLE_SCALING FALSE

static void
gst_video_convert_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_DISABLE_SCALING:
      g_value_set_boolean (value,
          !gst_video_convert_scale_get_scales (GST_VIDEO_CONVERT_SCALE
              (object)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_convert_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_DISABLE_SCALING:
      gst_video_convert_scale_set_scales (GST_VIDEO_CONVERT_SCALE (object),
          !g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_convert_class_init (GstVideoConvertClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_video_convert_set_property;
  gobject_class->get_property = gst_video_convert_get_property;

  ((GstVideoConvertScaleClass *) klass)->any_memory = TRUE;

  /**
   * videoconvert::disable-scaling:
   *
   * Since: 1.22
   */
  g_object_class_install_property (gobject_class, PROP_DISABLE_SCALING,
      g_param_spec_boolean ("disable-scaling", "Disable Scaling",
          "Disables frame scaling", DEFAULT_PROP_DISABLE_SCALING,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
}

static void
gst_video_convert_init (GstVideoConvert * self)
{

}
