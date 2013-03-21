/* GStreamer
 * Copyright (C) 2006 Edward Hervey <edward@fluendo.com>
 *
 * gstdataqueue.h:
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


#ifndef __EGL_GST_DATA_QUEUE_H__
#define __EGL_GST_DATA_QUEUE_H__

#include <gst/gst.h>

G_BEGIN_DECLS
#define EGL_GST_TYPE_DATA_QUEUE \
  (egl_gst_data_queue_get_type())
#define EGL_GST_DATA_QUEUE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),EGL_GST_TYPE_DATA_QUEUE,EGLGstDataQueue))
#define EGL_GST_DATA_QUEUE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),EGL_GST_TYPE_DATA_QUEUE,EGLGstDataQueueClass))
#define EGL_GST_IS_DATA_QUEUE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),EGL_GST_TYPE_DATA_QUEUE))
#define EGL_GST_IS_DATA_QUEUE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),EGL_GST_TYPE_DATA_QUEUE))
typedef struct _EGLGstDataQueue EGLGstDataQueue;
typedef struct _EGLGstDataQueueClass EGLGstDataQueueClass;
typedef struct _EGLGstDataQueueSize EGLGstDataQueueSize;
typedef struct _EGLGstDataQueueItem EGLGstDataQueueItem;
typedef struct _EGLGstDataQueuePrivate EGLGstDataQueuePrivate;

/**
 * EGLGstDataQueueItem:
 * @object: the #GstMiniObject to queue.
 * @size: the size in bytes of the miniobject.
 * @duration: the duration in #GstClockTime of the miniobject. Can not be
 * #GST_CLOCK_TIME_NONE.
 * @visible: #TRUE if @object should be considered as a visible object.
 * @destroy: The #GDestroyNotify function to use to free the #EGLGstDataQueueItem.
 * This function should also drop the reference to @object the owner of the
 * #EGLGstDataQueueItem is assumed to hold.
 *
 * Structure used by #EGLGstDataQueue. You can supply a different structure, as
 * long as the top of the structure is identical to this structure.
 */

struct _EGLGstDataQueueItem
{
  GstMiniObject *object;
  guint size;
  guint64 duration;
  gboolean visible;

  /* user supplied destroy function */
  GDestroyNotify destroy;

  /* < private > */
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * EGLGstDataQueueSize:
 * @visible: number of buffers
 * @bytes: number of bytes
 * @time: amount of time
 *
 * Structure describing the size of a queue.
 */
struct _EGLGstDataQueueSize
{
  guint visible;
  guint bytes;
  guint64 time;
};

/**
 * EGLGstDataQueueCheckFullFunction:
 * @queue: a #EGLGstDataQueue.
 * @visible: The number of visible items currently in the queue.
 * @bytes: The amount of bytes currently in the queue.
 * @time: The accumulated duration of the items currently in the queue.
 * @checkdata: The #gpointer registered when the #EGLGstDataQueue was created.
 * 
 * The prototype of the function used to inform the queue that it should be
 * considered as full.
 *
 * Returns: #TRUE if the queue should be considered full.
 */
typedef gboolean (*EGLGstDataQueueCheckFullFunction) (EGLGstDataQueue * queue,
    guint visible, guint bytes, guint64 time, gpointer checkdata);

typedef void (*EGLGstDataQueueFullCallback) (EGLGstDataQueue * queue, gpointer checkdata);
typedef void (*EGLGstDataQueueEmptyCallback) (EGLGstDataQueue * queue, gpointer checkdata);

/**
 * EGLGstDataQueue:
 * @object: the parent structure
 *
 * Opaque #EGLGstDataQueue structure.
 */
struct _EGLGstDataQueue
{
  GObject object;

  /*< private >*/
  EGLGstDataQueuePrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

struct _EGLGstDataQueueClass
{
  GObjectClass parent_class;

  /* signals */
  void (*empty) (EGLGstDataQueue * queue);
  void (*full) (EGLGstDataQueue * queue);

  gpointer _gst_reserved[GST_PADDING];
};

G_GNUC_INTERNAL
GType egl_gst_data_queue_get_type (void);

G_GNUC_INTERNAL
EGLGstDataQueue * egl_gst_data_queue_new            (EGLGstDataQueueCheckFullFunction checkfull,
					      EGLGstDataQueueFullCallback fullcallback,
					      EGLGstDataQueueEmptyCallback emptycallback,
					      gpointer checkdata) G_GNUC_MALLOC;
G_GNUC_INTERNAL
gboolean       egl_gst_data_queue_push           (EGLGstDataQueue * queue, EGLGstDataQueueItem * item);
G_GNUC_INTERNAL
gboolean       egl_gst_data_queue_pop            (EGLGstDataQueue * queue, EGLGstDataQueueItem ** item);
G_GNUC_INTERNAL
void           egl_gst_data_queue_flush          (EGLGstDataQueue * queue);
G_GNUC_INTERNAL
void           egl_gst_data_queue_set_flushing   (EGLGstDataQueue * queue, gboolean flushing);
G_GNUC_INTERNAL
gboolean       egl_gst_data_queue_drop_head      (EGLGstDataQueue * queue, GType type);
G_GNUC_INTERNAL
gboolean       egl_gst_data_queue_is_full        (EGLGstDataQueue * queue);
G_GNUC_INTERNAL
gboolean       egl_gst_data_queue_is_empty       (EGLGstDataQueue * queue);
G_GNUC_INTERNAL
void           egl_gst_data_queue_get_level      (EGLGstDataQueue * queue, EGLGstDataQueueSize *level);
G_GNUC_INTERNAL
void           egl_gst_data_queue_limits_changed (EGLGstDataQueue * queue);

G_END_DECLS

#endif /* __EGL_GST_DATA_QUEUE_H__ */
