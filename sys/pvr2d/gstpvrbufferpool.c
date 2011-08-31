/*
 * GStreamer
 * Copyright (c) 2010, Texas Instruments Incorporated
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "gstpvrbufferpool.h"

GST_DEBUG_CATEGORY_EXTERN (gst_debug_pvrvideosink);
#define GST_CAT_DEFAULT gst_debug_pvrvideosink

/*
 * GstDucatiBuffer
 */

static GstBufferClass *buffer_parent_class;

/* Get the original buffer, or whatever is the best output buffer.
 * Consumes the input reference, produces the output reference
 */
GstBuffer *
gst_ducati_buffer_get (GstDucatiBuffer * self)
{
  if (self->orig) {
    // TODO copy to orig buffer.. if needed.
    gst_buffer_unref (self->orig);
    self->orig = NULL;
  }
  return GST_BUFFER (self);
}

PVR2DMEMINFO *
gst_ducati_buffer_get_meminfo (GstDucatiBuffer * self)
{
  return self->src_mem;
}


static GstDucatiBuffer *
gst_ducati_buffer_new (GstPvrBufferPool * pool)
{
  PVR2DERROR pvr_error;
  GstDucatiBuffer *self = (GstDucatiBuffer *)
      gst_mini_object_new (GST_TYPE_DUCATIBUFFER);

  GST_LOG_OBJECT (pool->element, "creating buffer %p in pool %p", self, pool);

  self->pool = (GstPvrBufferPool *)
      gst_mini_object_ref (GST_MINI_OBJECT (pool));

  GST_BUFFER_DATA (self) = gst_ducati_alloc_1d (pool->size);
  GST_BUFFER_SIZE (self) = pool->size;
  GST_LOG_OBJECT (pool->element, "width=%d, height=%d and size=%d",
      pool->padded_width, pool->padded_height, pool->size);

  pvr_error =
      PVR2DMemWrap (pool->pvr_context, GST_BUFFER_DATA (self), 0, pool->size,
      NULL, &(self->src_mem));
  if (pvr_error != PVR2D_OK) {
    GST_LOG_OBJECT (pool->element, "Failed to Wrap buffer memory"
        "returned %d", pvr_error);
  } else {
    self->wrapped = TRUE;
  }

  gst_buffer_set_caps (GST_BUFFER (self), pool->caps);

  return self;
}


static void
gst_ducati_buffer_finalize (GstDucatiBuffer * self)
{
  PVR2DERROR pvr_error;
  GstPvrBufferPool *pool = self->pool;
  gboolean resuscitated = FALSE;

  GST_LOG_OBJECT (pool->element, "finalizing buffer %p", self);

  GST_PVR_BUFFERPOOL_LOCK (pool);
  g_queue_remove (pool->used_buffers, self);
  if (pool->running) {
    resuscitated = TRUE;

    GST_LOG_OBJECT (pool->element, "reviving buffer %p", self);

    g_queue_push_head (pool->free_buffers, self);
  } else {
    GST_LOG_OBJECT (pool->element, "the pool is shutting down");
  }
  GST_PVR_BUFFERPOOL_UNLOCK (pool);

  if (resuscitated) {
    GST_LOG_OBJECT (pool->element, "reviving buffer %p, %d", self, index);
    gst_buffer_ref (GST_BUFFER (self));
    GST_BUFFER_SIZE (self) = 0;
  }

  if (!resuscitated) {
    GST_LOG_OBJECT (pool->element,
        "buffer %p (data %p, len %u) not recovered, freeing",
        self, GST_BUFFER_DATA (self), GST_BUFFER_SIZE (self));

    if (self->wrapped) {
      pvr_error = PVR2DMemFree (pool->pvr_context, self->src_mem);
      if (pvr_error != PVR2D_OK) {
        GST_ERROR_OBJECT (pool->element, "Failed to Unwrap buffer memory"
            "returned %d", pvr_error);
      }
      self->wrapped = FALSE;
    }
    MemMgr_Free ((void *) GST_BUFFER_DATA (self));
    GST_BUFFER_DATA (self) = NULL;
    gst_mini_object_unref (GST_MINI_OBJECT (pool));
    GST_MINI_OBJECT_CLASS (buffer_parent_class)->finalize (GST_MINI_OBJECT
        (self));
  }
}

static void
gst_ducati_buffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  buffer_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      GST_DEBUG_FUNCPTR (gst_ducati_buffer_finalize);
}

GType
gst_ducati_buffer_get_type (void)
{
  static GType type;

  if (G_UNLIKELY (type == 0)) {
    static const GTypeInfo info = {
      .class_size = sizeof (GstBufferClass),
      .class_init = gst_ducati_buffer_class_init,
      .instance_size = sizeof (GstDucatiBuffer),
    };
    type = g_type_register_static (GST_TYPE_BUFFER,
        "GstDucatiBufferPvrsink", &info, 0);
  }
  return type;
}

/*
 * GstDucatiBufferPool
 */

static GstMiniObjectClass *bufferpool_parent_class = NULL;

/** create new bufferpool
 * @element : the element that owns this pool
 * @caps:  the caps to set on the buffer
 * @num_buffers:  the requested number of buffers in the pool
 */
GstPvrBufferPool *
gst_pvr_bufferpool_new (GstElement * element, GstCaps * caps, gint num_buffers,
    gint size, PVR2DCONTEXTHANDLE pvr_context)
{
  GstPvrBufferPool *self = (GstPvrBufferPool *)
      gst_mini_object_new (GST_TYPE_PVRBUFFERPOOL);
  GstStructure *s = gst_caps_get_structure (caps, 0);

  self->element = gst_object_ref (element);
  gst_structure_get_int (s, "width", &self->padded_width);
  gst_structure_get_int (s, "height", &self->padded_height);
  self->caps = gst_caps_ref (caps);
  self->size = size;
  self->pvr_context = pvr_context;

  self->free_buffers = g_queue_new ();
  self->used_buffers = g_queue_new ();
  self->lock = g_mutex_new ();
  self->running = TRUE;

  return self;
}

static void
unwrap_buffer (gpointer buffer, gpointer user_data)
{
  PVR2DERROR pvr_error;
  GstDucatiBuffer *buf = GST_DUCATIBUFFER (buffer);
  GstPvrBufferPool *pool = (GstPvrBufferPool *) user_data;

  if (buf->wrapped) {
    pvr_error = PVR2DMemFree (pool->pvr_context, buf->src_mem);
    if (pvr_error != PVR2D_OK) {
      GST_ERROR_OBJECT (pool->element, "Failed to Unwrap buffer memory"
          "returned %d", pvr_error);
    }
    buf->wrapped = FALSE;
  }
}

void
gst_pvr_bufferpool_stop_running (GstPvrBufferPool * self, gboolean unwrap)
{
  gboolean empty = FALSE;

  g_return_if_fail (self);

  GST_PVR_BUFFERPOOL_LOCK (self);
  self->running = FALSE;
  GST_PVR_BUFFERPOOL_UNLOCK (self);

  GST_DEBUG_OBJECT (self->element, "free available buffers");

  /* free all buffers on the freelist */
  while (!empty) {
    GstDucatiBuffer *buf;
    GST_PVR_BUFFERPOOL_LOCK (self);
    buf = g_queue_pop_head (self->free_buffers);
    GST_PVR_BUFFERPOOL_UNLOCK (self);
    if (buf)
      gst_buffer_unref (GST_BUFFER (buf));
    else
      empty = TRUE;
  }

  if (unwrap)
    g_queue_foreach (self->used_buffers, unwrap_buffer, self);

  gst_mini_object_unref (GST_MINI_OBJECT (self));
}

/** get buffer from bufferpool, allocate new buffer if needed */
GstDucatiBuffer *
gst_pvr_bufferpool_get (GstPvrBufferPool * self, GstBuffer * orig)
{
  GstDucatiBuffer *buf = NULL;

  g_return_val_if_fail (self, NULL);

  GST_PVR_BUFFERPOOL_LOCK (self);
  if (self->running) {
    /* re-use a buffer off the freelist if any are available
     */
    buf = g_queue_pop_head (self->free_buffers);
    if (!buf)
      buf = gst_ducati_buffer_new (self);
    buf->orig = orig;
    g_queue_push_head (self->used_buffers, buf);
  }
  GST_PVR_BUFFERPOOL_UNLOCK (self);

  if (buf && orig) {
    GST_BUFFER_TIMESTAMP (buf) = GST_BUFFER_TIMESTAMP (orig);
    GST_BUFFER_DURATION (buf) = GST_BUFFER_DURATION (orig);
  }
  GST_BUFFER_SIZE (buf) = self->size;

  return buf;
}

static void
gst_pvr_bufferpool_finalize (GstPvrBufferPool * self)
{
  GST_DEBUG_OBJECT (self->element, "destroy bufferpool");
  g_mutex_free (self->lock);
  self->lock = NULL;

  g_queue_free (self->free_buffers);
  self->free_buffers = NULL;
  g_queue_free (self->used_buffers);
  self->used_buffers = NULL;

  gst_caps_unref (self->caps);
  self->caps = NULL;
  gst_object_unref (self->element);
  self->element = NULL;

  GST_MINI_OBJECT_CLASS (bufferpool_parent_class)->finalize (GST_MINI_OBJECT
      (self));
}

static void
gst_pvr_bufferpool_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  bufferpool_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      GST_DEBUG_FUNCPTR (gst_pvr_bufferpool_finalize);
}

GType
gst_pvr_bufferpool_get_type (void)
{
  static GType type;

  if (G_UNLIKELY (type == 0)) {
    static const GTypeInfo info = {
      .class_size = sizeof (GstMiniObjectClass),
      .class_init = gst_pvr_bufferpool_class_init,
      .instance_size = sizeof (GstPvrBufferPool),
    };
    type = g_type_register_static (GST_TYPE_MINI_OBJECT,
        "GstPvrBufferPool", &info, 0);
  }
  return type;
}
