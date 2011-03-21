/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@schleef.org>
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

#ifndef _GST_VIDEO_FILTER2_H_
#define _GST_VIDEO_FILTER2_H_

#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_FILTER2   (gst_video_filter2_get_type())
#define GST_VIDEO_FILTER2(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_FILTER2,GstVideoFilter2))
#define GST_VIDEO_FILTER2_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_FILTER2,GstVideoFilter2Class))
#define GST_IS_VIDEO_FILTER2(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_FILTER2))
#define GST_IS_VIDEO_FILTER2_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_FILTER2))

typedef struct _GstVideoFilter2 GstVideoFilter2;
typedef struct _GstVideoFilter2Class GstVideoFilter2Class;
typedef struct _GstVideoFilter2Functions GstVideoFilter2Functions;

struct _GstVideoFilter2
{
  GstBaseTransform base_videofilter2;

  GstVideoFormat format;
  int width;
  int height;

  gpointer _gst_reserved[GST_PADDING_LARGE];
};

struct _GstVideoFilter2Class
{
  GstBaseTransformClass base_videofilter2_class;

  const GstVideoFilter2Functions *functions;

  GstFlowReturn (*prefilter) (GstVideoFilter2 *filter, GstBuffer *inbuf);

  gpointer _gst_reserved[GST_PADDING_LARGE];
};

struct _GstVideoFilter2Functions
{
  GstVideoFormat format;
  GstFlowReturn (*filter) (GstVideoFilter2 *filter, GstBuffer *inbuf,
      GstBuffer *outbuf, int start, int end);
  GstFlowReturn (*filter_ip) (GstVideoFilter2 *filter, GstBuffer *buffer,
      int start, int end);
  gpointer _gst_reserved[GST_PADDING_LARGE];
};

#define GST_VIDEO_FILTER2_FORMAT(vf) (((GstVideoFilter2 *)vf)->format)
#define GST_VIDEO_FILTER2_WIDTH(vf) (((GstVideoFilter2 *)vf)->width)
#define GST_VIDEO_FILTER2_HEIGHT(vf) (((GstVideoFilter2 *)vf)->height)

GType gst_video_filter2_get_type (void);

void gst_video_filter2_class_add_functions (GstVideoFilter2Class *klass,
    const GstVideoFilter2Functions *functions);

G_END_DECLS

#endif
