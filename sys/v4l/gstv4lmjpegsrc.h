/* GStreamer
 *
 * gstv4lmjpegsrc.h: hardware MJPEG video source element
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

#ifndef __GST_V4LMJPEGSRC_H__
#define __GST_V4LMJPEGSRC_H__

#include <gstv4lelement.h>
#include <sys/time.h>
#include <videodev_mjpeg.h>

G_BEGIN_DECLS

#define GST_TYPE_V4LMJPEGSRC \
  (gst_v4lmjpegsrc_get_type())
#define GST_V4LMJPEGSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_V4LMJPEGSRC,GstV4lMjpegSrc))
#define GST_V4LMJPEGSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_V4LMJPEGSRC,GstV4lMjpegSrcClass))
#define GST_IS_V4LMJPEGSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_V4LMJPEGSRC))
#define GST_IS_V4LMJPEGSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_V4LMJPEGSRC))

typedef struct _GstV4lMjpegSrc GstV4lMjpegSrc;
typedef struct _GstV4lMjpegSrcClass GstV4lMjpegSrcClass;

struct _GstV4lMjpegSrc {
  GstV4lElement v4lelement;

  /* pads */
  GstPad *srcpad;

  /* buffer/capture info */
  struct mjpeg_sync bsync;
  struct mjpeg_requestbuffers breq;

  /* num of queued frames and some GThread stuff
   * to wait if there's not enough */
  gint8 *frame_queue_state;
  GMutex *mutex_queue_state;
  GCond *cond_queue_state;
  gint num_queued;
  gint queue_frame;

  /* True if we want to stop */
  gboolean quit;

  /* A/V sync... frame counter and internal cache */
  gulong handled;
  gint last_frame;
  gint last_size;
  gint need_writes;
  gulong last_seq;

  /* clock */
  GstClock *clock;

  /* time to substract from clock time to get back to timestamp */
  GstClockTime substract_time;

  /* how often are we going to use each frame? */
  gint *use_num_times;

  /* how are we going to push buffers? */
  gboolean use_fixed_fps;

  /* end size */
  gint end_width, end_height;

  /* caching values */
#if 0
  gint x_offset;
  gint y_offset;
  gint frame_width;
  gint frame_height;
#endif

  gint quality;
  gint numbufs;
};

struct _GstV4lMjpegSrcClass {
  GstV4lElementClass parent_class;

  void (*frame_capture) (GObject *object);
  void (*frame_drop)    (GObject *object);
  void (*frame_insert)  (GObject *object);
  void (*frame_lost)    (GObject *object,
                         gint     num_lost);
};

GType gst_v4lmjpegsrc_get_type(void);


G_END_DECLS

#endif /* __GST_V4LMJPEGSRC_H__ */
