/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oleavr@gmail.com>
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

#ifndef __GST_IPHONE_CAMERA_SRC_H__
#define __GST_IPHONE_CAMERA_SRC_H__

#include <gst/base/gstpushsrc.h>

#include "coremediactx.h"

G_BEGIN_DECLS

#define GST_TYPE_IPHONE_CAMERA_SRC \
  (gst_iphone_camera_src_get_type ())
#define GST_IPHONE_CAMERA_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_IPHONE_CAMERA_SRC, GstIPhoneCameraSrc))
#define GST_IPHONE_CAMERA_SRC_CAST(obj) \
  ((GstIPhoneCameraSrc *) (obj))
#define GST_IPHONE_CAMERA_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_IPHONE_CAMERA_SRC, GstIPhoneCameraSrcClass))
#define GST_IS_IPHONE_CAMERA_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_IPHONE_CAMERA_SRC))
#define GST_IS_IPHONE_CAMERA_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_IPHONE_CAMERA_SRC))

typedef struct _GstIPhoneCameraSrc         GstIPhoneCameraSrc;
typedef struct _GstIPhoneCameraSrcClass    GstIPhoneCameraSrcClass;

struct _GstIPhoneCameraSrc
{
  GstPushSrc push_src;

  gboolean do_stats;

  GstCoreMediaCtx *ctx;

  FigCaptureDeviceRef device;
  FigBaseIface *device_iface_base;
  FigCaptureStreamRef stream;
  FigBaseIface *stream_iface_base;
  FigCaptureStreamIface *stream_iface;
  FigBufferQueueRef queue;
  GstCaps *device_caps;
  GArray *device_formats;
  GstClockTime duration;

  volatile gboolean running;
  guint64 offset;

  GCond *cond;
  volatile gboolean has_pending;
};

struct _GstIPhoneCameraSrcClass
{
  GstPushSrcClass parent_class;
};

GType gst_iphone_camera_src_get_type (void);

G_END_DECLS

#endif /* __GST_IPHONE_CAMERA_SRC_H__ */
