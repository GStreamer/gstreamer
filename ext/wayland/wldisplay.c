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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "wldisplay.h"

#include <errno.h>

GST_DEBUG_CATEGORY_EXTERN (gstwayland_debug);
#define GST_CAT_DEFAULT gstwayland_debug

G_DEFINE_TYPE (GstWlDisplay, gst_wl_display, G_TYPE_OBJECT);

static void gst_wl_display_finalize (GObject * gobject);

static void
gst_wl_display_class_init (GstWlDisplayClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = gst_wl_display_finalize;
}

static void
gst_wl_display_init (GstWlDisplay * self)
{
  self->formats = g_array_new (FALSE, FALSE, sizeof (uint32_t));
  self->wl_fd_poll = gst_poll_new (TRUE);
}

static void
gst_wl_display_finalize (GObject * gobject)
{
  GstWlDisplay *self = GST_WL_DISPLAY (gobject);

  gst_poll_set_flushing (self->wl_fd_poll, TRUE);

  if (self->thread)
    g_thread_join (self->thread);

  g_array_unref (self->formats);
  gst_poll_free (self->wl_fd_poll);

  if (self->shm)
    wl_shm_destroy (self->shm);

  if (self->shell)
    wl_shell_destroy (self->shell);

  if (self->compositor)
    wl_compositor_destroy (self->compositor);

  if (self->subcompositor)
    wl_subcompositor_destroy (self->subcompositor);

  if (self->registry)
    wl_registry_destroy (self->registry);

  if (self->queue)
    wl_event_queue_destroy (self->queue);

  if (self->own_display) {
    wl_display_flush (self->display);
    wl_display_disconnect (self->display);
  }

  G_OBJECT_CLASS (gst_wl_display_parent_class)->finalize (gobject);
}

static void
sync_callback (void *data, struct wl_callback *callback, uint32_t serial)
{
  gboolean *done = data;
  *done = TRUE;
}

static const struct wl_callback_listener sync_listener = {
  sync_callback
};

static gint
gst_wl_display_roundtrip (GstWlDisplay * self)
{
  struct wl_callback *callback;
  gint ret = 0;
  gboolean done = FALSE;

  g_return_val_if_fail (self != NULL, -1);

  /* We don't own the display, process only our queue */
  callback = wl_display_sync (self->display);
  wl_callback_add_listener (callback, &sync_listener, &done);
  wl_proxy_set_queue ((struct wl_proxy *) callback, self->queue);
  while (ret != -1 && !done)
    ret = wl_display_dispatch_queue (self->display, self->queue);
  wl_callback_destroy (callback);

  return ret;
}

static void
shm_format (void *data, struct wl_shm *wl_shm, uint32_t format)
{
  GstWlDisplay *self = data;

  g_array_append_val (self->formats, format);
}

static const struct wl_shm_listener shm_listener = {
  shm_format
};

static void
registry_handle_global (void *data, struct wl_registry *registry,
    uint32_t id, const char *interface, uint32_t version)
{
  GstWlDisplay *self = data;

  if (g_strcmp0 (interface, "wl_compositor") == 0) {
    self->compositor = wl_registry_bind (registry, id, &wl_compositor_interface,
        MIN (version, 3));
  } else if (g_strcmp0 (interface, "wl_subcompositor") == 0) {
    self->subcompositor =
        wl_registry_bind (registry, id, &wl_subcompositor_interface, 1);
  } else if (g_strcmp0 (interface, "wl_shell") == 0) {
    self->shell = wl_registry_bind (registry, id, &wl_shell_interface, 1);
  } else if (g_strcmp0 (interface, "wl_shm") == 0) {
    self->shm = wl_registry_bind (registry, id, &wl_shm_interface, 1);
    wl_shm_add_listener (self->shm, &shm_listener, self);
  } else if (g_strcmp0 (interface, "wl_scaler") == 0) {
    self->scaler = wl_registry_bind (registry, id, &wl_scaler_interface, 2);
  }
}

static const struct wl_registry_listener registry_listener = {
  registry_handle_global
};

static gpointer
gst_wl_display_thread_run (gpointer data)
{
  GstWlDisplay *self = data;
  GstPollFD pollfd = GST_POLL_FD_INIT;

  pollfd.fd = wl_display_get_fd (self->display);
  gst_poll_add_fd (self->wl_fd_poll, &pollfd);
  gst_poll_fd_ctl_read (self->wl_fd_poll, &pollfd, TRUE);

  /* main loop */
  while (1) {
    while (wl_display_prepare_read_queue (self->display, self->queue) != 0)
      wl_display_dispatch_queue_pending (self->display, self->queue);
    wl_display_flush (self->display);

    if (gst_poll_wait (self->wl_fd_poll, GST_CLOCK_TIME_NONE) < 0) {
      gboolean normal = (errno == EBUSY);
      wl_display_cancel_read (self->display);
      if (normal)
        break;
      else
        goto error;
    } else {
      wl_display_read_events (self->display);
      wl_display_dispatch_queue_pending (self->display, self->queue);
    }
  }

  return NULL;

error:
  GST_ERROR ("Error communicating with the wayland server");
  return NULL;
}

GstWlDisplay *
gst_wl_display_new (const gchar * name, GError ** error)
{
  struct wl_display *display;

  display = wl_display_connect (name);

  if (!display) {
    *error = g_error_new (g_quark_from_static_string ("GstWlDisplay"), 0,
        "Failed to connect to the wayland display '%s'",
        name ? name : "(default)");
    return NULL;
  } else {
    return gst_wl_display_new_existing (display, TRUE, error);
  }
}

GstWlDisplay *
gst_wl_display_new_existing (struct wl_display * display,
    gboolean take_ownership, GError ** error)
{
  GstWlDisplay *self;
  GError *err = NULL;
  gint i;

  g_return_val_if_fail (display != NULL, NULL);

  self = g_object_new (GST_TYPE_WL_DISPLAY, NULL);
  self->display = display;
  self->own_display = take_ownership;

  self->queue = wl_display_create_queue (self->display);
  self->registry = wl_display_get_registry (self->display);
  wl_proxy_set_queue ((struct wl_proxy *) self->registry, self->queue);
  wl_registry_add_listener (self->registry, &registry_listener, self);

  /* we need exactly 2 roundtrips to discover global objects and their state */
  for (i = 0; i < 2; i++) {
    if (gst_wl_display_roundtrip (self) < 0) {
      *error = g_error_new (g_quark_from_static_string ("GstWlDisplay"), 0,
          "Error communicating with the wayland display");
      g_object_unref (self);
      return NULL;
    }
  }

  /* verify we got all the required interfaces */
#define VERIFY_INTERFACE_EXISTS(var, interface) \
  if (!self->var) { \
    g_set_error (error, g_quark_from_static_string ("GstWlDisplay"), 0, \
        "Could not bind to " interface ". Either it is not implemented in " \
        "the compositor, or the implemented version doesn't match"); \
    g_object_unref (self); \
    return NULL; \
  }

  VERIFY_INTERFACE_EXISTS (compositor, "wl_compositor");
  VERIFY_INTERFACE_EXISTS (subcompositor, "wl_subcompositor");
  VERIFY_INTERFACE_EXISTS (shell, "wl_shell");
  VERIFY_INTERFACE_EXISTS (shm, "wl_shm");
  VERIFY_INTERFACE_EXISTS (scaler, "wl_scaler");

#undef VERIFY_INTERFACE_EXISTS

  self->thread = g_thread_try_new ("GstWlDisplay", gst_wl_display_thread_run,
      self, &err);
  if (err) {
    g_propagate_prefixed_error (error, err,
        "Failed to start thread for the display's events");
    g_object_unref (self);
    return NULL;
  }

  return self;
}
