/* GStreamer
 *
 * gstv4lmjpegsink.h: hardware MJPEG video sink element
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_V4LMJPEGSINK_H__
#define __GST_V4LMJPEGSINK_H__

#include <gstv4lelement.h>
#include <sys/time.h>
#include <videodev_mjpeg.h>


G_BEGIN_DECLS


#define GST_TYPE_V4LMJPEGSINK \
  (gst_v4lmjpegsink_get_type())
#define GST_V4LMJPEGSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_V4LMJPEGSINK,GstV4lMjpegSink))
#define GST_V4LMJPEGSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_V4LMJPEGSINK,GstV4lMjpegSinkClass))
#define GST_IS_V4LMJPEGSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_V4LMJPEGSINK))
#define GST_IS_V4LMJPEGSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_V4LMJPEGSINK))

typedef struct _GstV4lMjpegSink GstV4lMjpegSink;
typedef struct _GstV4lMjpegSinkClass GstV4lMjpegSinkClass;

struct _GstV4lMjpegSink {
  GstV4lElement v4lelement;

  /* the sink pas */
  GstPad *sinkpad;

  /* frame properties for common players */
  gint frames_displayed;
  guint64 frame_time;

  /* system clock object */
  GstClock *clock;

  /* buffer/capture info */
  struct mjpeg_sync bsync;
  struct mjpeg_requestbuffers breq;

  /* thread to keep track of synced frames */
  gint8 *isqueued_queued_frames; /* 1 = queued, 0 = unqueued, -1 = error */
  GThread *thread_queued_frames;
  GMutex *mutex_queued_frames;
  GCond **cond_queued_frames;
  gint current_frame;

  /* width/height/norm of the jpeg stream */
  gint width;
  gint height;
  gint norm;

  /* cache values */
  gint x_offset;
  gint y_offset;

  gint numbufs;
  gint bufsize; /* in KB */
};

struct _GstV4lMjpegSinkClass {
  GstV4lElementClass parent_class;

  /* signals */
  void (*frame_displayed) (GstElement *element);
};

GType gst_v4lmjpegsink_get_type(void);


G_END_DECLS

#endif /* __GST_SDLVIDEOSINK_H__ */
