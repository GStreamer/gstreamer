/* GStreamer Wayland Library
 *
 * Copyright (C) 2014 Collabora Ltd.
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
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

/* GstWlBuffer wraps wl_buffer and provides a mechanism for preventing
 * buffers from being re-used while the compositor is using them. This
 * is achieved by adding a reference to the GstBuffer as soon as its
 * associated wl_buffer is sent to the compositor and by removing this
 * reference as soon as the compositor sends a wl_buffer::release message.
 *
 * This mechanism is a bit complicated, though, because it adds cyclic
 * references that can be dangerous. The reference cycles looks like:
 *
 *   ----------------
 *   | GstWlDisplay | ---------------------------->
 *   ----------------                              |
 *                                                 |
 *                                                 V
 *   -----------------     -------------     ---------------
 *   | GstBufferPool | --> | GstBuffer | ==> | GstWlBuffer |
 *   |               | <-- |           | <-- |             |
 *   -----------------     -------------     ---------------
 *
 * A GstBufferPool normally holds references to its GstBuffers and  that first
 * memory of each buffer holds a reference to a GstWlBuffer (saved in the
 * GstMiniObject weak ref data). When a GstBuffer is in use, it holds a reference
 * back to the pool and the pool doesn't hold a reference to the GstBuffer. When
 * the GstBuffer is unrefed externally, it returns back to the pool and the pool
 * holds again a reference to the buffer.
 *
 * Now when the compositor is using a buffer, the GstWlBuffer also holds a ref
 * to the GstBuffer, which prevents it from returning to the pool. When the
 * last GstWlBuffer receives a release event and unrefs the last GstBuffer,
 * the GstBufferPool will be able to stop and if no-one is holding a strong
 * ref to it, it will be destroyed. This will destroy the pool's GstBuffers and
 * also the GstWlBuffers. This will all happen in the same context of the last
 * gst_buffer_unref, which will be called from the buffer_release() callback.
 *
 * The problem here lies in the fact that buffer_release() will be called
 * from the event loop thread of GstWlDisplay, so it's as if the display
 * holds a reference to the GstWlBuffer, but without having an actual reference.
 * When we kill the display, there is no way for the GstWlBuffer, the associated
 * GstBuffer and the GstBufferPool to get destroyed, so we are going to leak a
 * fair amount of memory.
 *
 * Normally, this rarely happens, because the compositor releases buffers
 * almost immediately and when waylandsink stops, they are already released.
 *
 * However, we want to be absolutely certain, so a solution is introduced
 * by registering all the GstWlBuffers with the window and explicitly
 * releasing all the buffer references as soon as the window is to be descroyed.
 * This has to be done before the final unref of the window, since GstWlBuffer
 * holds a reference on their parent GstWlWindow for thread safety.
 *
 * When the GstWlWindow is finalized, it  clears all the pending weak references
 * and unref all cached GstWlBuffer. This cached must be flushed in few
 * conditions to avoid holding on memory that are no longer in use.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstwlbuffer.h"

#define GST_CAT_DEFAULT gst_wl_buffer_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

typedef struct _GstWlBufferPrivate
{
  struct wl_buffer *wlbuffer;
  GstBuffer *current_gstbuffer;
  GstMemory *gstmem;

  GstWlWindow *window;

  gboolean used_by_compositor;

  /* Protects against concurrent update between the wayland thread and the
   * streaming thread */
  GMutex lock;
} GstWlBufferPrivate;

G_DEFINE_TYPE_WITH_CODE (GstWlBuffer, gst_wl_buffer, GST_TYPE_OBJECT,
    G_ADD_PRIVATE (GstWlBuffer)
    GST_DEBUG_CATEGORY_INIT (gst_wl_buffer_debug,
        "wlbuffer", 0, "wlbuffer library");
    );

static void
gst_wl_buffer_dispose (GObject * gobject)
{
  GstWlBuffer *self = GST_WL_BUFFER (gobject);
  GstWlBufferPrivate *priv = gst_wl_buffer_get_instance_private (self);

  GST_TRACE_OBJECT (self, "dispose");

  g_mutex_lock (&priv->lock);
  gst_clear_buffer (&priv->current_gstbuffer);
  g_mutex_unlock (&priv->lock);

  if (priv->window) {
    gst_wl_window_unregister_buffer (priv->window, self);
    gst_clear_object (&priv->window);
  }

  G_OBJECT_CLASS (gst_wl_buffer_parent_class)->dispose (gobject);
}

static void
gst_wl_buffer_finalize (GObject * gobject)
{
  GstWlBuffer *self = GST_WL_BUFFER (gobject);
  GstWlBufferPrivate *priv = gst_wl_buffer_get_instance_private (self);

  GST_TRACE_OBJECT (self, "finalize");

  if (priv->wlbuffer)
    wl_buffer_destroy (priv->wlbuffer);

  G_OBJECT_CLASS (gst_wl_buffer_parent_class)->finalize (gobject);
}

static void
gst_wl_buffer_class_init (GstWlBufferClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->dispose = gst_wl_buffer_dispose;
  object_class->finalize = gst_wl_buffer_finalize;
}

static void
gst_wl_buffer_init (GstWlBuffer * self)
{
}

static void
buffer_release (void *data, struct wl_buffer *wl_buffer)
{
  GstWlBuffer *self = data;
  GstWlBufferPrivate *priv = gst_wl_buffer_get_instance_private (self);
  GstBuffer *buf;

  g_mutex_lock (&priv->lock);
  priv->used_by_compositor = FALSE;
  buf = priv->current_gstbuffer;
  priv->current_gstbuffer = NULL;
  g_mutex_unlock (&priv->lock);

  /* unref should be last, because it may end up destroying the GstWlBuffer */
  GST_LOG_OBJECT (self, "wl_buffer::release (GstWlbuffer: %p, GstBuffer: %p)",
      self, buf);
  gst_clear_buffer (&buf);
}

static const struct wl_buffer_listener buffer_listener = {
  buffer_release
};

GstWlBuffer *
gst_buffer_add_wl_buffer (GstBuffer * gstbuffer, struct wl_buffer *wlbuffer,
    GstWlWindow * window)
{
  GstWlBuffer *self;
  GstWlBufferPrivate *priv;

  self = g_object_new (GST_TYPE_WL_BUFFER, NULL);
  priv = gst_wl_buffer_get_instance_private (self);
  priv->current_gstbuffer = gst_buffer_ref (gstbuffer);
  priv->wlbuffer = wlbuffer;
  gst_object_ref (window);
  priv->window = window;
  priv->gstmem = gst_buffer_peek_memory (gstbuffer, 0);
  g_mutex_init (&priv->lock);

  wl_buffer_add_listener (priv->wlbuffer, &buffer_listener, self);
  gst_wl_window_register_buffer (priv->window, priv->gstmem, self);

  return self;
}

GstWlBuffer *
gst_buffer_get_wl_buffer (GstWlWindow * window, GstBuffer * gstbuffer)
{
  GstWlBufferPrivate *priv;
  GstWlBuffer *wlbuf;
  GstMemory *mem0;

  if (!gstbuffer)
    return NULL;

  mem0 = gst_buffer_peek_memory (gstbuffer, 0);
  wlbuf = gst_wl_window_lookup_buffer (window, mem0);

  if (wlbuf) {
    priv = gst_wl_buffer_get_instance_private (wlbuf);

    g_mutex_lock (&priv->lock);
    gst_buffer_replace (&priv->current_gstbuffer, gstbuffer);
    g_mutex_unlock (&priv->lock);
  }

  return wlbuf;
}

void
gst_wl_buffer_attach (GstWlBuffer * self, struct wl_surface *surface)
{
  GstWlBufferPrivate *priv = gst_wl_buffer_get_instance_private (self);

  g_mutex_lock (&priv->lock);

  if (priv->used_by_compositor) {
    GST_ERROR_OBJECT (self, "buffer used by compositor %p",
        priv->current_gstbuffer);
    g_mutex_unlock (&priv->lock);
    return;
  }

  wl_surface_attach (surface, priv->wlbuffer, 0, 0);
  priv->used_by_compositor = TRUE;

  g_mutex_unlock (&priv->lock);
}

void
gst_wl_buffer_detach (GstWlBuffer * self)
{
  GstWlBufferPrivate *priv;
  GstBuffer *buf;

  /* simplify cleanup code */
  if (!self)
    return;

  priv = gst_wl_buffer_get_instance_private (self);

  g_assert (!priv->used_by_compositor);

  g_mutex_lock (&priv->lock);
  buf = priv->current_gstbuffer;
  priv->current_gstbuffer = NULL;
  g_mutex_unlock (&priv->lock);

  gst_clear_buffer (&buf);
}

GstVideoMeta *
gst_wl_buffer_get_video_meta (GstWlBuffer * self)
{
  GstWlBufferPrivate *priv = gst_wl_buffer_get_instance_private (self);
  return gst_buffer_get_video_meta (priv->current_gstbuffer);
}

GstVideoCropMeta *
gst_wl_buffer_get_video_crop_meta (GstWlBuffer * self)
{
  GstWlBufferPrivate *priv = gst_wl_buffer_get_instance_private (self);
  return gst_buffer_get_video_crop_meta (priv->current_gstbuffer);
}
