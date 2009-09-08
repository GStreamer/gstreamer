/*  GStreamer video sink base class
 *  Copyright (C) <2003> Julien Moutte <julien@moutte.net>
 *  Copyright (C) <2009> Tim-Philipp Müller <tim centricular net>
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
 * SECTION:gstvideosink
 * @short_description: Base class for video sinks
 * 
 * <refsect2>
 * <para>
 * Provides useful functions and a base class for video sinks. 
 * </para>
 * <para>
 * GstVideoSink will configure the default base sink to drop frames that
 * arrive later than 20ms as this is considered the default threshold for
 * observing out-of-sync frames.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideosink.h"

GST_DEBUG_CATEGORY_STATIC (video_sink_debug);
#define GST_CAT_DEFAULT video_sink_debug

static GstBaseSinkClass *parent_class = NULL;

static GstFlowReturn gst_video_sink_show_preroll_frame (GstBaseSink * bsink,
    GstBuffer * buf);
static GstFlowReturn gst_video_sink_show_frame (GstBaseSink * bsink,
    GstBuffer * buf);

/**
 * gst_video_sink_center_rect:
 * @src: the #GstVideoRectangle describing the source area
 * @dst: the #GstVideoRectangle describing the destination area
 * @result: a pointer to a #GstVideoRectangle which will receive the result area
 * @scaling: a #gboolean indicating if scaling should be applied or not
 * 
 * Takes @src rectangle and position it at the center of @dst rectangle with or
 * without @scaling. It handles clipping if the @src rectangle is bigger than
 * the @dst one and @scaling is set to FALSE.
 */
void
gst_video_sink_center_rect (GstVideoRectangle src, GstVideoRectangle dst,
    GstVideoRectangle * result, gboolean scaling)
{
  g_return_if_fail (result != NULL);

  if (!scaling) {
    result->w = MIN (src.w, dst.w);
    result->h = MIN (src.h, dst.h);
    result->x = (dst.w - result->w) / 2;
    result->y = (dst.h - result->h) / 2;
  } else {
    gdouble src_ratio, dst_ratio;

    src_ratio = (gdouble) src.w / src.h;
    dst_ratio = (gdouble) dst.w / dst.h;

    if (src_ratio > dst_ratio) {
      result->w = dst.w;
      result->h = dst.w / src_ratio;
      result->x = 0;
      result->y = (dst.h - result->h) / 2;
    } else if (src_ratio < dst_ratio) {
      result->w = dst.h * src_ratio;
      result->h = dst.h;
      result->x = (dst.w - result->w) / 2;
      result->y = 0;
    } else {
      result->x = 0;
      result->y = 0;
      result->w = dst.w;
      result->h = dst.h;
    }
  }

  GST_DEBUG ("source is %dx%d dest is %dx%d, result is %dx%d with x,y %dx%d",
      src.w, src.h, dst.w, dst.h, result->w, result->h, result->x, result->y);
}

/* Initing stuff */

static void
gst_video_sink_init (GstVideoSink * videosink)
{
  videosink->width = 0;
  videosink->height = 0;

  /* 20ms is more than enough, 80-130ms is noticable */
  gst_base_sink_set_max_lateness (GST_BASE_SINK (videosink), 20 * GST_MSECOND);
  gst_base_sink_set_qos_enabled (GST_BASE_SINK (videosink), TRUE);
}

static void
gst_video_sink_class_init (GstVideoSinkClass * klass)
{
  GstBaseSinkClass *basesink_class = (GstBaseSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  basesink_class->render = GST_DEBUG_FUNCPTR (gst_video_sink_show_frame);
  basesink_class->preroll =
      GST_DEBUG_FUNCPTR (gst_video_sink_show_preroll_frame);
}

static void
gst_video_sink_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (video_sink_debug, "videosink", 0, "GstVideoSink");
}

static GstFlowReturn
gst_video_sink_show_preroll_frame (GstBaseSink * bsink, GstBuffer * buf)
{
  GstVideoSinkClass *klass;

  klass = GST_VIDEO_SINK_GET_CLASS (bsink);

  if (klass->show_frame == NULL) {
    if (parent_class->preroll != NULL)
      return parent_class->preroll (bsink, buf);
    else
      return GST_FLOW_OK;
  }

  GST_LOG_OBJECT (bsink, "rendering frame, ts=%" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  return klass->show_frame (GST_VIDEO_SINK_CAST (bsink), buf);
}

static GstFlowReturn
gst_video_sink_show_frame (GstBaseSink * bsink, GstBuffer * buf)
{
  GstVideoSinkClass *klass;

  klass = GST_VIDEO_SINK_GET_CLASS (bsink);

  if (klass->show_frame == NULL) {
    if (parent_class->render != NULL)
      return parent_class->render (bsink, buf);
    else
      return GST_FLOW_OK;
  }

  GST_LOG_OBJECT (bsink, "rendering frame, ts=%" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  return klass->show_frame (GST_VIDEO_SINK_CAST (bsink), buf);
}

/* Public methods */

GType
gst_video_sink_get_type (void)
{
  static GType videosink_type = 0;

  if (!videosink_type) {
    static const GTypeInfo videosink_info = {
      sizeof (GstVideoSinkClass),
      gst_video_sink_base_init,
      NULL,
      (GClassInitFunc) gst_video_sink_class_init,
      NULL,
      NULL,
      sizeof (GstVideoSink),
      0,
      (GInstanceInitFunc) gst_video_sink_init,
    };

    videosink_type = g_type_register_static (GST_TYPE_BASE_SINK,
        "GstVideoSink", &videosink_info, 0);
  }

  return videosink_type;
}
