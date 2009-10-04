/* 
 * GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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
 
#ifndef __GST_VDP_VIDEO_YUV_H__
#define __GST_VDP_VIDEO_YUV_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/vdpau/gstvdpdevice.h>

G_BEGIN_DECLS

#define GST_TYPE_VDP_VIDEO_YUV            (gst_vdp_video_yuv_get_type())
#define GST_VDP_VIDEO_YUV(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VDP_VIDEO_YUV,GstVdpVideoYUV))
#define GST_VDP_VIDEO_YUV_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VDP_VIDEO_YUV,GstVdpVideoYUVClass))
#define GST_VDP_VIDEO_YUV_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VDP_VIDEO_YUV, GstVdpVideoYUVClass))
#define GST_IS_VDP_VIDEO_YUV(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VDP_VIDEO_YUV))
#define GST_IS_VDP_VIDEO_YUV_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VDP_VIDEO_YUV))

typedef struct _GstVdpVideoYUV      GstVdpVideoYUV;
typedef struct _GstVdpVideoYUVClass GstVdpVideoYUVClass;

struct _GstVdpVideoYUV {
  GstBaseTransform transform;

  gchar *display;
  GstVdpDevice *device;
  
  gint width, height;
  guint format;
};

struct _GstVdpVideoYUVClass {
  GstBaseTransformClass transform_class;
};

GType gst_vdp_video_yuv_get_type (void);

G_END_DECLS

#endif /* __GST_VDP_VIDEO_YUV_H__ */
