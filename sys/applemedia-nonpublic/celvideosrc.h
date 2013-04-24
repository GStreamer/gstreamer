/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oleavr@soundrop.com>
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

#ifndef __GST_CEL_VIDEO_SRC_H__
#define __GST_CEL_VIDEO_SRC_H__

#include <gst/base/gstpushsrc.h>

#include "coremediactx.h"

G_BEGIN_DECLS

#define GST_TYPE_CEL_VIDEO_SRC \
  (gst_cel_video_src_get_type ())
#define GST_CEL_VIDEO_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_CEL_VIDEO_SRC, GstCelVideoSrc))
#define GST_CEL_VIDEO_SRC_CAST(obj) \
  ((GstCelVideoSrc *) (obj))
#define GST_CEL_VIDEO_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_CEL_VIDEO_SRC, GstCelVideoSrcClass))
#define GST_IS_CEL_VIDEO_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_CEL_VIDEO_SRC))
#define GST_IS_CEL_VIDEO_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_CEL_VIDEO_SRC))

typedef struct _GstCelVideoSrc         GstCelVideoSrc;
typedef struct _GstCelVideoSrcClass    GstCelVideoSrcClass;

struct _GstCelVideoSrc
{
  GstPushSrc push_src;

  gint device_index;
  gboolean do_stats;

  GstCoreMediaCtx *ctx;

  FigCaptureDeviceRef device;
  FigCaptureDeviceIface *device_iface;
  FigBaseObjectRef device_base;
  FigBaseIface *device_base_iface;
  FigCaptureStreamRef stream;
  FigCaptureStreamIface *stream_iface;
  FigBaseObjectRef stream_base;
  FigBaseIface *stream_base_iface;

  CMBufferQueueRef queue;
  CMBufferQueueTriggerToken ready_trigger;
  GstCaps *device_caps;
  GArray *device_formats;
  GstClockTime duration;

  volatile gint is_running;
  guint64 offset;

  GCond *ready_cond;
  volatile gboolean queue_is_ready;

  GstClockTime last_sampling;
  guint count;
  gint fps;
};

struct _GstCelVideoSrcClass
{
  GstPushSrcClass parent_class;
};

GType gst_cel_video_src_get_type (void);

G_END_DECLS

#endif /* __GST_CEL_VIDEO_SRC_H__ */
