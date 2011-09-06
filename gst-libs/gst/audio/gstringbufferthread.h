/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstaudioringbuffer.h:
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

#ifndef __GST_RING_BUFFER_THREAD_H__
#define __GST_RING_BUFFER_THREAD_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_RING_BUFFER_THREAD             (gst_ring_buffer_thread_get_type())
#define GST_RING_BUFFER_THREAD(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RING_BUFFER_THREAD,GstRingBufferThread))
#define GST_RING_BUFFER_THREAD_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RING_BUFFER_THREAD,GstRingBufferThreadClass))
#define GST_RING_BUFFER_THREAD_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_RING_BUFFER_THREAD,GstRingBufferThreadClass))
#define GST_IS_RING_BUFFER_THREAD(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RING_BUFFER_THREAD))
#define GST_IS_RING_BUFFER_THREAD_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RING_BUFFER_THREAD))
#define GST_RING_BUFFER_THREAD_CAST(obj)        ((GstRingBufferThread *)obj)

typedef struct _GstRingBufferThread GstRingBufferThread;
typedef struct _GstRingBufferThreadClass GstRingBufferThreadClass;

#include <gst/audio/gstaudioringbuffer.h>

#define GST_RING_BUFFER_THREAD_GET_COND(buf) (((GstRingBufferThread *)buf)->cond)
#define GST_RING_BUFFER_THREAD_WAIT(buf)     (g_cond_wait (GST_RING_BUFFER_THREAD_GET_COND (buf), GST_OBJECT_GET_LOCK (buf)))
#define GST_RING_BUFFER_THREAD_SIGNAL(buf)   (g_cond_signal (GST_RING_BUFFER_THREAD_GET_COND (buf)))
#define GST_RING_BUFFER_THREAD_BROADCAST(buf)(g_cond_broadcast (GST_RING_BUFFER_THREAD_GET_COND (buf)))

/**
 * GstRingBufferThread:
 *
 * Opaque #GstRingBufferThread.
 */
struct _GstRingBufferThread {
  GstObject       parent;

  gint       active_count;

  /*< private >*/ /* with LOCK */
  GThread   *thread;
  gboolean   running;
  GCond     *cond;

  GstAudioRingBuffer *playback;
  GstAudioRingBuffer *capture;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstRingBufferThreadClass:
 * @parent_class: the parent class structure.
 *
 * #GstRingBufferThread class. Override the vmethods to implement functionality.
 */
struct _GstRingBufferThreadClass {
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_ring_buffer_thread_get_type(void);

gboolean gst_ring_buffer_thread_set_ringbuffer (GstRingBufferThread *thread, GstAudioRingBuffer *buf);

gboolean gst_ring_buffer_thread_activate (GstRingBufferThread *thread, gboolean active);

gboolean gst_ring_buffer_thread_start (GstRingBufferThread *thread);

G_END_DECLS

#endif /* __GST_RING_BUFFER_THREAD_H__ */
