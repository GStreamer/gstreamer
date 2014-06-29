/* GStreamer
 * Copyright (C) 2013 Rdio, Inc. <ingestions@rd.io>
 * Copyright (C) 2013 David Schleef <ds@schleef.org>
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

#ifndef _GST_YADIF_H_
#define _GST_YADIF_H_

#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_YADIF   (gst_yadif_get_type())
#define GST_YADIF(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_YADIF,GstYadif))
#define GST_YADIF_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_YADIF,GstYadifClass))
#define GST_IS_YADIF(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_YADIF))
#define GST_IS_YADIF_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_YADIF))

typedef struct _GstYadif GstYadif;
typedef struct _GstYadifClass GstYadifClass;

typedef enum {
  GST_DEINTERLACE_MODE_AUTO,
  GST_DEINTERLACE_MODE_INTERLACED,
  GST_DEINTERLACE_MODE_DISABLED
} GstDeinterlaceMode;

struct _GstYadif
{
  GstBaseTransform base_yadif;

  GstDeinterlaceMode mode;

  GstVideoInfo video_info;

  GstVideoFrame prev_frame;
  GstVideoFrame cur_frame;
  GstVideoFrame next_frame;
  GstVideoFrame dest_frame;
};

struct _GstYadifClass
{
  GstBaseTransformClass base_yadif_class;
};

GType gst_yadif_get_type (void);

G_END_DECLS

#endif
