/*
 * GStreamer
 * Copyright 2007 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
 * Copyright 2007 Ali Sabil <ali.sabil@tandberg.com>
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

#ifndef __GST_OSX_VIDEO_SRC_H__
#define __GST_OSX_VIDEO_SRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <Quicktime/Quicktime.h>

GST_DEBUG_CATEGORY_EXTERN (gst_debug_osx_video_src);

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_OSX_VIDEO_SRC \
  (gst_osx_video_src_get_type())
#define GST_OSX_VIDEO_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OSX_VIDEO_SRC,GstOSXVideoSrc))
#define GST_OSX_VIDEO_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OSX_VIDEO_SRC,GstOSXVideoSrcClass))
#define GST_IS_OSX_VIDEO_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OSX_VIDEO_SRC))
#define GST_IS_OSX_VIDEO_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OSX_VIDEO_SRC))

typedef struct _GstOSXVideoSrc GstOSXVideoSrc;
typedef struct _GstOSXVideoSrcClass GstOSXVideoSrcClass;

struct _GstOSXVideoSrc
{
  GstPushSrc pushsrc;

  gchar * device_id;
  gchar * device_name;
  SeqGrabComponent seq_grab;
  SGChannel video_chan;
  GWorldPtr world;
  Rect rect;
  ImageSequence dec_seq;

  GstBuffer * buffer;
  guint seq_num;
};

struct _GstOSXVideoSrcClass
{
  GstPushSrcClass parent_class;
  gboolean movies_enabled;
};

GType gst_osx_video_src_get_type (void);

G_END_DECLS

#endif /* __GST_OSX_VIDEO_SRC_H__ */
