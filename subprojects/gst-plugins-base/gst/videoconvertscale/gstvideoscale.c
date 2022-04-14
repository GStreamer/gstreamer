/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2005-2012 David Schleef <ds@schleef.org>
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
 * SECTION:element-videoscale
 * @title: videoscale
 * @see_also: videorate, videoconvert
 *
 * This element resizes video frames. By default the element will try to
 * negotiate to the same size on the source and sinkpad so that no scaling
 * is needed. It is therefore safe to insert this element in a pipeline to
 * get more robust behaviour without any cost if no scaling is needed.
 *
 * This element supports a wide range of color spaces including various YUV and
 * RGB formats and is therefore generally able to operate anywhere in a
 * pipeline.
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 -v filesrc location=videotestsrc.ogg ! oggdemux ! theoradec ! videoconvert ! videoscale ! autovideosink
 * ]|
 *  Decode an Ogg/Theora and display the video. If the video sink chosen
 * cannot perform scaling, the video scaling will be performed by videoscale
 * when you resize the video window.
 * To create the test Ogg/Theora file refer to the documentation of theoraenc.
 * |[
 * gst-launch-1.0 -v filesrc location=videotestsrc.ogg ! oggdemux ! theoradec ! videoconvert ! videoscale ! video/x-raw,width=100 ! autovideosink
 * ]|
 *  Decode an Ogg/Theora and display the video with a width of 100.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define DEFAULT_PROP_GAMMA_DECODE FALSE
#define DEFAULT_PROP_DISABLE_CONVERSION FALSE

#include "gstvideoscale.h"

G_DEFINE_TYPE (GstVideoScale, gst_video_scale, GST_TYPE_VIDEO_CONVERT_SCALE);
GST_ELEMENT_REGISTER_DEFINE (videoscale, "videoscale",
    GST_RANK_MARGINAL, gst_video_scale_get_type ());

enum
{
  PROP_0,
  PROP_GAMMA_DECODE,
  PROP_DISABLE_CONVERSION,
};

static void
gst_video_scale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_GAMMA_DECODE:
    {
      gint mode;

      g_object_get (object, "gamma-mode", &mode, NULL);
      g_value_set_boolean (value, mode == GST_VIDEO_GAMMA_MODE_REMAP);

      break;
    }
    case PROP_DISABLE_CONVERSION:
      g_value_set_boolean (value,
          !gst_video_convert_scale_get_converts (GST_VIDEO_CONVERT_SCALE
              (object)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_GAMMA_DECODE:
    {
      if (g_value_get_boolean (value))
        g_object_set (object, "gamma-mode", GST_VIDEO_GAMMA_MODE_REMAP, NULL);
      else
        g_object_set (object, "gamma-mode", GST_VIDEO_GAMMA_MODE_NONE, NULL);

      break;
    }
    case PROP_DISABLE_CONVERSION:
      gst_video_convert_scale_set_converts (GST_VIDEO_CONVERT_SCALE (object),
          !g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_scale_class_init (GstVideoScaleClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_video_scale_set_property;
  gobject_class->get_property = gst_video_scale_get_property;

  ((GstVideoConvertScaleClass *) klass)->any_memory = TRUE;

  g_object_class_install_property (gobject_class, PROP_GAMMA_DECODE,
      g_param_spec_boolean ("gamma-decode", "Gamma Decode",
          "Decode gamma before scaling", DEFAULT_PROP_GAMMA_DECODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * videoscale::disable-conversion:
   *
   * Since: 1.22
   */
  g_object_class_install_property (gobject_class, PROP_DISABLE_CONVERSION,
      g_param_spec_boolean ("disable-conversion", "Disable Conversion",
          "Disables colorspace conversions", DEFAULT_PROP_DISABLE_CONVERSION,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
}

static void
gst_video_scale_init (GstVideoScale * self)
{

}
