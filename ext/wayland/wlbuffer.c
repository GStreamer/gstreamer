/* GStreamer Wayland video sink
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
 *   | GstWlDisplay | ---------------------------------->
 *   ----------------                                    |
 *         ^                                             |
 *         |                                             V
 *   ------------------------     -------------     ---------------
 *   | GstWaylandBufferPool | --> | GstBuffer | ==> | GstWlBuffer |
 *   |                      | <-- |           | <-- |             |
 *   ------------------------     -------------     ---------------
 *
 * A GstBufferPool normally holds references to its GstBuffers and each buffer
 * holds a reference to a GstWlBuffer (saved in the GstMiniObject qdata).
 * When a GstBuffer is in use, it holds a reference back to the pool and the
 * pool doesn't hold a reference to the GstBuffer. When the GstBuffer is unrefed
 * externally, it returns back to the pool and the pool holds again a reference
 * to the buffer.
 *
 * Now when the compositor is using a buffer, the GstWlBuffer also holds a ref
 * to the GstBuffer, which prevents it from returning to the pool. When the
 * last GstWlBuffer receives a release event and unrefs the last GstBuffer,
 * the GstBufferPool will be able to stop and if no-one is holding a strong
 * ref to it, it will be destroyed. This will destroy that last GstBuffer and
 * also the GstWlBuffer. This will all happen in the same context of the
 * gst_buffer_unref, which will be called from the buffer_release() callback.
 *
 * The big problem here lies in the fact that buffer_release() will be called
 * from the event loop thread of GstWlDisplay and the second big problem is
 * that the GstWaylandBufferPool holds a strong ref to the GstWlDisplay.
 * Therefore, if the buffer_release() causes the pool to be destroyed, it may
 * also cause the GstWlDisplay to be destroyed and that will happen in the
 * context of the event loop thread that GstWlDisplay runs. Destroying the
 * GstWlDisplay will need to join the thread (from inside the thread!) and boom.
 *
 * Normally, this will never happen, even if we don't take special care for it,
 * because the compositor releases buffers almost immediately and when
 * waylandsink stops, they are already released.
 *
 * However, we want to be absolutely certain, so a solution is introduced
 * by explicitly releasing all the buffer references and destroying the
 * GstWlBuffers as soon as we know that we are not going to use them again.
 * All the GstWlBuffers are registered in a hash set inside GstWlDisplay
 * and there is gst_wl_display_stop(), which stops the event loop thread
 * and releases all the buffers explicitly. This gets called from GstWaylandSink
 * right before dropping its own reference to the GstWlDisplay, leaving
 * a possible last (but safe now!) reference to the pool, which may be
 * referenced by an upstream element.
 */

#include "wlbuffer.h"

GST_DEBUG_CATEGORY_EXTERN (gstwayland_debug);
#define GST_CAT_DEFAULT gstwayland_debug

G_DEFINE_TYPE (GstWlBuffer, gst_wl_buffer, G_TYPE_OBJECT);

static G_DEFINE_QUARK (GstWlBufferQDataQuark, gst_wl_buffer_qdata);

static void
gst_wl_buffer_finalize (GObject * gobject)
{
  GstWlBuffer *self = GST_WL_BUFFER (gobject);

  if (self->display)
    gst_wl_display_unregister_buffer (self->display, self);
  wl_buffer_destroy (self->wlbuffer);

  G_OBJECT_CLASS (gst_wl_buffer_parent_class)->finalize (gobject);
}

static void
gst_wl_buffer_class_init (GstWlBufferClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

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

  GST_LOG_OBJECT (self, "wl_buffer::release (GstBuffer: %p)", self->gstbuffer);

  self->used_by_compositor = FALSE;

  /* unref should be last, because it may end up destroying the GstWlBuffer */
  gst_buffer_unref (self->gstbuffer);
}

static const struct wl_buffer_listener buffer_listener = {
  buffer_release
};

void
gst_buffer_add_wl_buffer (GstBuffer * gstbuffer, struct wl_buffer *wlbuffer,
    GstWlDisplay * display)
{
  GstWlBuffer *self;

  self = g_object_new (GST_TYPE_WL_BUFFER, NULL);
  self->gstbuffer = gstbuffer;
  self->wlbuffer = wlbuffer;
  self->display = display;

  gst_wl_display_register_buffer (self->display, self);

  wl_buffer_add_listener (self->wlbuffer, &buffer_listener, self);

  gst_mini_object_set_qdata ((GstMiniObject *) gstbuffer,
      gst_wl_buffer_qdata_quark (), self, g_object_unref);
}

GstWlBuffer *
gst_buffer_get_wl_buffer (GstBuffer * gstbuffer)
{
  return gst_mini_object_get_qdata ((GstMiniObject *) gstbuffer,
      gst_wl_buffer_qdata_quark ());
}

void
gst_wl_buffer_force_release_and_unref (GstWlBuffer * self)
{
  /* detach from the GstBuffer */
  (void) gst_mini_object_steal_qdata ((GstMiniObject *) self->gstbuffer,
      gst_wl_buffer_qdata_quark ());

  /* force a buffer release
   * at this point, the GstWlDisplay has killed its event loop,
   * so we don't need to worry about buffer_release() being called
   * at the same time from the event loop thread */
  if (self->used_by_compositor) {
    GST_DEBUG_OBJECT (self, "forcing wl_buffer::release (GstBuffer: %p)",
        self->gstbuffer);
    gst_buffer_unref (self->gstbuffer);
    self->used_by_compositor = FALSE;
  }

  /* avoid unregistering from the display in finalize() because this
   * function is being called from a hash table foreach function,
   * which would be modified in gst_wl_display_unregister_buffer() */
  self->display = NULL;
  g_object_unref (self);
}

void
gst_wl_buffer_attach (GstWlBuffer * self, GstWlWindow * target)
{
  g_return_if_fail (self->used_by_compositor == FALSE);

  wl_surface_attach (target->surface, self->wlbuffer, 0, 0);

  /* Add a reference to the buffer. This represents the fact that
   * the compositor is using the buffer and it should not return
   * back to the pool and be re-used until the compositor releases it. */
  gst_buffer_ref (self->gstbuffer);
  self->used_by_compositor = TRUE;
}
