/* GStreamer
 *
 * gstv4lsrc.h: BT8x8/V4L video source element
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

enum
{
  QUEUE_STATE_ERROR = -1,
  QUEUE_STATE_READY_FOR_QUEUE, /* the frame is ready to be queued for capture */
  QUEUE_STATE_QUEUED,          /* the frame is queued for capture */
  QUEUE_STATE_SYNCED           /* the frame is captured */
};

typedef enum
{
  GST_V4LSRC_SYNC_MODE_CLOCK,
  GST_V4LSRC_SYNC_MODE_PRIVATE_CLOCK,
  GST_V4LSRC_SYNC_MODE_FIXED_FPS,
  GST_V4LSRC_SYNC_MODE_NONE,
} GstV4lSrcSyncMode;

struct _GstV4lSrc
{
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
  gboolean need_discont;
  GstClockTime last_discont;    /* gst_element_get_time () of last discont send */

  /* clock */
  GstClock *clock;

  /* time to substract from clock time to get back to timestamp */
  GstClockTime substract_time;

  /* how often are we going to use each frame? */
  gint *use_num_times;

  /* list of supported colourspaces (as integers) */
  GList *colourspaces;

  /* how are we going to timestamp buffers? */
   GstV4lSrcSyncMode syncmode;

   gboolean copy_mode;
   gboolean autoprobe; /* probe on startup ? */
};

struct _GstV4lSrcClass
{
  GstV4lElementClass parent_class;

  void (*frame_capture) (GObject * object);
  void (*frame_drop) (GObject * object);
  void (*frame_insert) (GObject * object);
};

GType gst_v4lsrc_get_type (void);


G_END_DECLS
#endif /* __GST_V4LSRC_H__ */
