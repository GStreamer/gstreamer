/*
 * Copyright (C) 2009 Ole André Vadla Ravnås <oleavr@soundrop.com>
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

#ifndef __GST_MIO_VIDEO_SRC_H__
#define __GST_MIO_VIDEO_SRC_H__

#include <gst/base/gstpushsrc.h>

#include "coremediactx.h"
#include "miovideodevice.h"

G_BEGIN_DECLS

#define GST_TYPE_MIO_VIDEO_SRC \
  (gst_mio_video_src_get_type ())
#define GST_MIO_VIDEO_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MIO_VIDEO_SRC, GstMIOVideoSrc))
#define GST_MIO_VIDEO_SRC_CAST(obj) \
  ((GstMIOVideoSrc *) (obj))
#define GST_MIO_VIDEO_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MIO_VIDEO_SRC, GstMIOVideoSrcClass))
#define GST_IS_MIO_VIDEO_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MIO_VIDEO_SRC))
#define GST_IS_MIO_VIDEO_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MIO_VIDEO_SRC))

typedef struct _GstMIOVideoSrc         GstMIOVideoSrc;
typedef struct _GstMIOVideoSrcClass    GstMIOVideoSrcClass;

struct _GstMIOVideoSrc
{
  GstPushSrc push_src;

  gint cv_ratio_n;
  gint cv_ratio_d;

  gchar *device_uid;
  gchar *device_name;
  gint device_index;

  GThread *dispatcher_thread;
  GMainLoop *dispatcher_loop;
  GMainContext *dispatcher_ctx;

  GstCoreMediaCtx *ctx;
  GstMIOVideoDevice *device;

  TundraGraph *graph;

  volatile gboolean running;
  GQueue *queue;
  GMutex *qlock;
  GCond *qcond;
  guint64 prev_offset;
  CMFormatDescriptionRef prev_format;
};

struct _GstMIOVideoSrcClass
{
  GstPushSrcClass parent_class;
};

GType gst_mio_video_src_get_type (void);

G_END_DECLS

#endif /* __GST_MIO_VIDEO_SRC_H__ */
