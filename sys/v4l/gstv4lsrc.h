/* G-Streamer BT8x8/V4L frame grabber plugin
 * Copyright (C) 2001 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

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

struct _GstV4lSrc {
  GstV4lElement v4lelement;

  /* pads */
  GstPad *srcpad;

  /* bufferpool for the buffers we're gonna use */
  GstBufferPool *bufferpool;

  /* whether we need to reset the GstPad */
  gboolean init;

  /* capture/buffer info */
  struct video_mmap mmap;
  struct video_mbuf mbuf;
  gint sync_frame;
  gboolean *frame_queued;
  guint buffer_size;

  /* a seperate pthread for the sync() thread (improves correctness of timestamps) */
  gint8 *isready_soft_sync; /* 1 = ok, 0 = waiting, -1 = error */
  struct timeval *timestamp_soft_sync;
  pthread_t thread_soft_sync;
  pthread_mutex_t mutex_soft_sync;
  pthread_cond_t *cond_soft_sync;

  /* num of queued frames and some pthread stuff to wait if there's not enough */
  guint16 num_queued_frames;
  pthread_mutex_t mutex_queued_frames;
  pthread_cond_t cond_queued_frames;

  /* caching values */
  gint width;
  gint height;
  guint16 palette;
};

struct _GstV4lSrcClass {
  GstV4lElementClass parent_class;
};

GType gst_v4lsrc_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_V4LSRC_H__ */
