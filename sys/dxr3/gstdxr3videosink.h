/* GStreamer DXR3 Hardware MPEG video decoder plugin
 * Copyright (C) <2002> Rehan Khwaja <rehankhwaja@yahoo.com>
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


#ifndef __GST_DXR3_VIDEO_SINK_H__
#define __GST_DXR3_VIDEO_SINK_H__

#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


typedef struct _GstDxr3VideoSink GstDxr3VideoSink;
struct _GstDxr3VideoSink;

struct _GstDxr3VideoSink {
  GstElement element;

  /* board number */
  gint device_number;

  /* file handle for the video device */
  FILE *device;

  GstClock *clock;

  /* our only pad */
  GstPad *sinkpad;
};


typedef struct _GstDxr3VideoSinkClass GstDxr3VideoSinkClass;

struct _GstDxr3VideoSinkClass {
  GstElementClass parent_class;
};


#define GST_TYPE_DXR3_VIDEO_SINK \
  (gst_dxr3_video_sink_get_type())
#define GST_DXR3_VIDEO_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DXR3_VIDEO_SINK,GstDxr3VideoSink))
#define GST_DXR3_VIDEO_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DXR3_VIDEO_SINK,GstDxr3VideoSink))
#define GST_IS_DXR3_VIDEO_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DXR3_VIDEO_SINK))
#define GST_IS_DXR3_VIDEO_SINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DXR3_VIDEO_SINK))


GType gst_dxr3_video_sink_get_type(void);


GST_PADTEMPLATE_FACTORY (dxr3_video_sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  gst_caps_new ("video_sink", "video/mpeg", NULL)
);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_DXR3_VIDEO_SINK_H__ */
