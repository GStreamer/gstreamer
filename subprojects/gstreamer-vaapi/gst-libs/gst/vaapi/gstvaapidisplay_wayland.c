/*
 *  gstvaapidisplay_wayland.c - VA/Wayland display abstraction
 *
 *  Copyright (C) 2012-2013 Intel Corporation
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:gstvaapidisplay_wayland
 * @short_description: VA/Wayland display abstraction
 */

#include "sysdeps.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapidisplay_wayland.h"
#include "gstvaapidisplay_wayland_priv.h"
#include "gstvaapiwindow_wayland.h"

#define DEBUG_VAAPI_DISPLAY 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE_WITH_PRIVATE (GstVaapiDisplayWayland, gst_vaapi_display_wayland,
    GST_TYPE_VAAPI_DISPLAY);

static inline const gchar *
get_default_display_name (void)
{
  static const gchar *g_display_name;

  if (!g_display_name)
    g_display_name = getenv ("WAYLAND_DISPLAY");
  return g_display_name;
}

/* Mangle display name with our prefix */
static gboolean
set_display_name (GstVaapiDisplay * display, const gchar * display_name)
{
  GstVaapiDisplayWaylandPrivate *const priv =
      GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE (display);

  g_free (priv->display_name);

  if (!display_name) {
    display_name = get_default_display_name ();
    if (!display_name)
      display_name = "";
  }
  priv->display_name = g_strdup (display_name);
  return priv->display_name != NULL;
}

static void
output_handle_geometry (void *data, struct wl_output *output,
    int x, int y, int physical_width, int physical_height,
    int subpixel, const char *make, const char *model, int transform)
{
  GstVaapiDisplayWaylandPrivate *const priv = data;

  priv->phys_width = physical_width;
  priv->phys_height = physical_height;
}

static void
output_handle_mode (void *data, struct wl_output *wl_output,
    uint32_t flags, int width, int height, int refresh)
{
  GstVaapiDisplayWaylandPrivate *const priv = data;

  if (flags & WL_OUTPUT_MODE_CURRENT) {
    priv->width = width;
    priv->height = height;
  }
}

static const struct wl_output_listener output_listener = {
  output_handle_geometry,
  output_handle_mode,
};

static void
handle_xdg_wm_base_ping (void *user_data, struct xdg_wm_base *xdg_wm_base,
    uint32_t serial)
{
  xdg_wm_base_pong (xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
  handle_xdg_wm_base_ping
};

static void
dmabuf_format (void *data, struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf,
    uint32_t format)
{
}

static void
dmabuf_modifier (void *data, struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf,
    uint32_t format, uint32_t modifier_hi, uint32_t modifier_lo)
{
  GstVaapiDisplayWaylandPrivate *const priv = data;
  GstDRMFormat drm_format = {
    .format = format,
    .modifier = (guint64) modifier_hi << 32 | modifier_lo
  };

  if (gst_vaapi_video_format_from_drm_format (format) ==
      GST_VIDEO_FORMAT_UNKNOWN) {
    GST_LOG ("ignoring unknown format 0x%x with modifier 0x%" G_GINT64_MODIFIER
        "x", format, drm_format.modifier);
    return;
  }

  GST_LOG ("got format 0x%x (%s) with modifier 0x%" G_GINT64_MODIFIER "x",
      format, gst_video_format_to_string (gst_vaapi_video_format_from_drm_format
          (format)), drm_format.modifier);

  g_mutex_lock (&priv->dmabuf_formats_lock);
  g_array_append_val (priv->dmabuf_formats, drm_format);
  g_mutex_unlock (&priv->dmabuf_formats_lock);
}

static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
  dmabuf_format,
  dmabuf_modifier,
};


static void
registry_handle_global (void *data,
    struct wl_registry *registry,
    uint32_t id, const char *interface, uint32_t version)
{
  GstVaapiDisplayWaylandPrivate *const priv = data;

  if (strcmp (interface, "wl_compositor") == 0)
    priv->compositor =
        wl_registry_bind (registry, id, &wl_compositor_interface, 1);
  else if (strcmp (interface, "wl_subcompositor") == 0)
    priv->subcompositor =
        wl_registry_bind (registry, id, &wl_subcompositor_interface, 1);
  else if (strcmp (interface, "wl_shell") == 0)
    priv->wl_shell = wl_registry_bind (registry, id, &wl_shell_interface, 1);
  else if (strcmp (interface, "xdg_wm_base") == 0) {
    priv->xdg_wm_base =
        wl_registry_bind (registry, id, &xdg_wm_base_interface, 1);
    xdg_wm_base_add_listener (priv->xdg_wm_base, &xdg_wm_base_listener, priv);
  } else if (strcmp (interface, "wl_output") == 0) {
    if (!priv->output) {
      priv->output = wl_registry_bind (registry, id, &wl_output_interface, 1);
      wl_output_add_listener (priv->output, &output_listener, priv);
    }
  } else if (strcmp (interface, "zwp_linux_dmabuf_v1") == 0) {
    priv->dmabuf =
        wl_registry_bind (registry, id, &zwp_linux_dmabuf_v1_interface, 3);
    zwp_linux_dmabuf_v1_add_listener (priv->dmabuf, &dmabuf_listener, priv);
  }
}

static const struct wl_registry_listener registry_listener = {
  registry_handle_global,
  NULL,
};

static gboolean
gst_vaapi_display_wayland_setup (GstVaapiDisplay * display)
{
  GstVaapiDisplayWaylandPrivate *const priv =
      GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE (display);

  wl_display_set_user_data (priv->wl_display, priv);
  priv->registry = wl_display_get_registry (priv->wl_display);
  wl_registry_add_listener (priv->registry, &registry_listener, priv);
  priv->event_fd = wl_display_get_fd (priv->wl_display);
  wl_display_roundtrip (priv->wl_display);

  if (!priv->width || !priv->height) {
    wl_display_roundtrip (priv->wl_display);
    if (!priv->width || !priv->height) {
      GST_ERROR ("failed to determine the display size");
      return FALSE;
    }
  }

  if (!priv->compositor) {
    GST_ERROR ("failed to bind compositor interface");
    return FALSE;
  }

  if (priv->xdg_wm_base)
    return TRUE;

  if (!priv->wl_shell) {
    GST_ERROR ("failed to bind wl_shell interface");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_vaapi_display_wayland_bind_display (GstVaapiDisplay * display,
    gpointer native_display)
{
  GstVaapiDisplayWaylandPrivate *const priv =
      GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE (display);

  priv->wl_display = native_display;
  priv->use_foreign_display = TRUE;

  /* XXX: how to get socket/display name? */
  GST_WARNING ("wayland: get display name");
  set_display_name (display, NULL);

  return gst_vaapi_display_wayland_setup (display);
}

static gboolean
gst_vaapi_display_wayland_open_display (GstVaapiDisplay * display,
    const gchar * name)
{
  GstVaapiDisplayWaylandPrivate *const priv =
      GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE (display);

  if (!set_display_name (display, name))
    return FALSE;

  priv->wl_display = wl_display_connect (name);
  if (!priv->wl_display)
    return FALSE;
  priv->use_foreign_display = FALSE;

  return gst_vaapi_display_wayland_setup (display);
}

static void
gst_vaapi_display_wayland_close_display (GstVaapiDisplay * display)
{
  GstVaapiDisplayWaylandPrivate *const priv =
      GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE (display);

  g_clear_pointer (&priv->output, wl_output_destroy);
  g_clear_pointer (&priv->wl_shell, wl_shell_destroy);
  g_clear_pointer (&priv->xdg_wm_base, xdg_wm_base_destroy);
  g_clear_pointer (&priv->subcompositor, wl_subcompositor_destroy);
  g_clear_pointer (&priv->compositor, wl_compositor_destroy);
  g_clear_pointer (&priv->registry, wl_registry_destroy);

  g_mutex_lock (&priv->dmabuf_formats_lock);
  g_array_unref (priv->dmabuf_formats);
  g_mutex_unlock (&priv->dmabuf_formats_lock);

  if (priv->wl_display) {
    if (!priv->use_foreign_display)
      wl_display_disconnect (priv->wl_display);
    priv->wl_display = NULL;
  }

  g_clear_pointer (&priv->display_name, g_free);
}

static gboolean
gst_vaapi_display_wayland_get_display_info (GstVaapiDisplay * display,
    GstVaapiDisplayInfo * info)
{
  GstVaapiDisplayWaylandPrivate *const priv =
      GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE (display);

  info->native_display = priv->wl_display;
  info->display_name = priv->display_name;
  if (!info->va_display) {
    info->va_display = vaGetDisplayWl (priv->wl_display);
    if (!info->va_display)
      return FALSE;
  }
  return TRUE;
}

static void
gst_vaapi_display_wayland_get_size (GstVaapiDisplay * display,
    guint * pwidth, guint * pheight)
{
  GstVaapiDisplayWaylandPrivate *const priv =
      GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE (display);

  if (!priv->output)
    return;

  if (pwidth)
    *pwidth = priv->width;

  if (pheight)
    *pheight = priv->height;
}

static void
gst_vaapi_display_wayland_get_size_mm (GstVaapiDisplay * display,
    guint * pwidth, guint * pheight)
{
  GstVaapiDisplayWaylandPrivate *const priv =
      GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE (display);

  if (!priv->output)
    return;

  if (pwidth)
    *pwidth = priv->phys_width;

  if (pheight)
    *pheight = priv->phys_height;
}

static GstVaapiWindow *
gst_vaapi_display_wayland_create_window (GstVaapiDisplay * display,
    GstVaapiID id, guint width, guint height)
{
  if (id != GST_VAAPI_ID_INVALID)
    return NULL;
  return gst_vaapi_window_wayland_new (display, width, height);
}

static void
gst_vaapi_display_wayland_init (GstVaapiDisplayWayland * display)
{
  GstVaapiDisplayWaylandPrivate *const priv =
      gst_vaapi_display_wayland_get_instance_private (display);

  display->priv = priv;
  priv->event_fd = -1;
  priv->dmabuf_formats = g_array_new (FALSE, FALSE, sizeof (GstDRMFormat));
  g_mutex_init (&priv->dmabuf_formats_lock);
}

static void
gst_vaapi_display_wayland_class_init (GstVaapiDisplayWaylandClass * klass)
{
  GstVaapiDisplayClass *const dpy_class = GST_VAAPI_DISPLAY_CLASS (klass);

  dpy_class->display_type = GST_VAAPI_DISPLAY_TYPE_WAYLAND;

  dpy_class->bind_display = gst_vaapi_display_wayland_bind_display;
  dpy_class->open_display = gst_vaapi_display_wayland_open_display;
  dpy_class->close_display = gst_vaapi_display_wayland_close_display;
  dpy_class->get_display = gst_vaapi_display_wayland_get_display_info;
  dpy_class->get_size = gst_vaapi_display_wayland_get_size;
  dpy_class->get_size_mm = gst_vaapi_display_wayland_get_size_mm;
  dpy_class->create_window = gst_vaapi_display_wayland_create_window;
}

/**
 * gst_vaapi_display_wayland_new:
 * @display_name: the Wayland display name
 *
 * Opens an Wayland #wl_display using @display_name and returns a
 * newly allocated #GstVaapiDisplay object. The Wayland display will
 * be cloed when the reference count of the object reaches zero.
 *
 * Return value: a newly allocated #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_display_wayland_new (const gchar * display_name)
{
  GstVaapiDisplay *display;

  display = g_object_new (GST_TYPE_VAAPI_DISPLAY_WAYLAND, NULL);
  return gst_vaapi_display_config (display,
      GST_VAAPI_DISPLAY_INIT_FROM_DISPLAY_NAME, (gpointer) display_name);
}

/**
 * gst_vaapi_display_wayland_new_with_display:
 * @wl_display: an Wayland #wl_display
 *
 * Creates a #GstVaapiDisplay based on the Wayland @wl_display
 * display. The caller still owns the display and must call
 * wl_display_disconnect() when all #GstVaapiDisplay references are
 * released. Doing so too early can yield undefined behaviour.
 *
 * Return value: a newly allocated #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_display_wayland_new_with_display (struct wl_display *wl_display)
{
  GstVaapiDisplay *display;

  g_return_val_if_fail (wl_display, NULL);

  display = g_object_new (GST_TYPE_VAAPI_DISPLAY_WAYLAND, NULL);
  return gst_vaapi_display_config (display,
      GST_VAAPI_DISPLAY_INIT_FROM_NATIVE_DISPLAY, wl_display);
}

/**
 * gst_vaapi_display_wayland_new_with_va_display:
 * @va_display: a VADisplay #va_display
 * @wl_display: an Wayland #wl_display
 *
 * Creates a #GstVaapiDisplay based on the VADisplay @va_display and
 * the Wayland @wl_display display.
 * The caller still owns the display and must call
 * wl_display_disconnect() when all #GstVaapiDisplay references are
 * released.
 *
 * Return value: a newly allocated #GstVaapiDisplay object
 */

GstVaapiDisplay *
gst_vaapi_display_wayland_new_with_va_display (VADisplay va_display,
    struct wl_display *wl_display)
{
  GstVaapiDisplay *display;
  GstVaapiDisplayInfo info = {
    .va_display = va_display,
    .native_display = wl_display,
  };

  g_return_val_if_fail (wl_display, NULL);

  display = g_object_new (GST_TYPE_VAAPI_DISPLAY_WAYLAND, NULL);
  if (!gst_vaapi_display_config (display,
          GST_VAAPI_DISPLAY_INIT_FROM_VA_DISPLAY, &info)) {
    gst_object_unref (display);
    return NULL;
  }

  return display;
}

/**
 * gst_vaapi_display_wayland_get_display:
 * @display: a #GstVaapiDisplayWayland
 *
 * Returns the underlying Wayland #wl_display that was created by
 * gst_vaapi_display_wayland_new() or that was bound from
 * gst_vaapi_display_wayland_new_with_display().
 *
 * Return value: the Wayland #wl_display attached to @display
 */
struct wl_display *
gst_vaapi_display_wayland_get_display (GstVaapiDisplayWayland * display)
{
  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_WAYLAND (display), NULL);

  return GST_VAAPI_DISPLAY_WL_DISPLAY (display);
}
