/* G-Streamer BT8x8/V4L frame grabber plugin
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

#ifndef __GST_V4LSRC_H__
#define __GST_V4LSRC_H__

#include <gstv4lelement.h>

G_BEGIN_DECLS

#define GST_TYPE_V4LSRC \
  (gst_v4lsrc_get_type())
#define GST_V4LSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_V4LSRC,GstV4lSrc))
#define GST_V4LSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_V4LSRC,GstV4lSrcClass))
#define GST_IS_V4LSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_V4LSRC))
#define GST_IS_V4LSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_V4LSRC))

typedef struct _GstV4lSrc GstV4lSrc;
typedef struct _GstV4lSrcClass GstV4lSrcClass;

enum {
  QUEUE_STATE_ERROR = -1,
  QUEUE_STATE_READY_FOR_QUEUE,
  QUEUE_STATE_QUEUED,
  QUEUE_STATE_SYNCED,
};

struct _GstV4lSrc {
  GstV4lElement v4lelement;

  /* pads */
  GstPad *srcpad;

  /* capture/buffer info */
  struct video_mmap mmap;
  struct video_mbuf mbuf;
  guint buffer_size;
  GstClockTime timestamp_sync;

  /* num of queued frames and some GThread stuff
   * to wait if there's not enough */
  gint8 *frame_queue_state;
  GMutex *mutex_queue_state;
  GCond *cond_queue_state;
  gint num_queued;
  gint sync_frame, queue_frame;
  gboolean is_capturing;

  /* True if we want to stop */
  gboolean quit;

  /* A/V sync... frame counter and internal cache */
  gulong handled;
  gint last_frame;
  gint need_writes;

  /* clock */
  GstClock *clock;

  /* time to substract from clock time to get back to timestamp */
  GstClockTime substract_time;

  /* how often are we going to use each frame? */
  gint *use_num_times;

  /* list of supported colourspaces (as integers) */
  GList *colourspaces;

  /* how are we going to push buffers? */
  gboolean use_fixed_fps;
};

struct _GstV4lSrcClass {
  GstV4lElementClass parent_class;

  void (*frame_capture) (GObject *object);
  void (*frame_drop)    (GObject *object);
  void (*frame_insert)  (GObject *object);
};

GType gst_v4lsrc_get_type(void);


G_END_DECLS

#endif /* __GST_V4LSRC_H__ */
