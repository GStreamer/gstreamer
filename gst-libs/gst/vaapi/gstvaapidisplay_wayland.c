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
#include <string.h>
#include "gstvaapidisplay_priv.h"
#include "gstvaapidisplay_wayland.h"
#include "gstvaapidisplay_wayland_priv.h"
#include "gstvaapiwindow_wayland.h"

#define DEBUG_VAAPI_DISPLAY 1
#include "gstvaapidebug.h"

#define _do_init \
    G_ADD_PRIVATE (GstVaapiDisplayWayland);

G_DEFINE_TYPE_WITH_CODE (GstVaapiDisplayWayland, gst_vaapi_display_wayland,
    GST_TYPE_VAAPI_DISPLAY, _do_init);

static const guint g_display_types = 1U << GST_VAAPI_DISPLAY_TYPE_WAYLAND;

static inline const gchar *
get_default_display_name (void)
{
  static const gchar *g_display_name;

  if (!g_display_name)
    g_display_name = getenv ("WAYLAND_DISPLAY");
  return g_display_name;
}

static inline guint
get_display_name_length (const gchar * display_name)
{
  const gchar *str;

  str = strchr (display_name, '-');
  if (str)
    return str - display_name;
  return strlen (display_name);
}

static gint
compare_display_name (gconstpointer a, gconstpointer b)
{
  const GstVaapiDisplayInfo *const info = a;
  const gchar *cached_name = info->display_name;
  const gchar *tested_name = b;
  guint cached_name_length, tested_name_length;

  g_return_val_if_fail (cached_name, FALSE);
  g_return_val_if_fail (tested_name, FALSE);

  cached_name_length = get_display_name_length (cached_name);
  tested_name_length = get_display_name_length (tested_name);

  /* XXX: handle screen number and default WAYLAND_DISPLAY name */
  if (cached_name_length != tested_name_length)
    return FALSE;
  if (strncmp (cached_name, tested_name, cached_name_length) != 0)
    return FALSE;
  return TRUE;
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
registry_handle_global (void *data,
    struct wl_registry *registry,
    uint32_t id, const char *interface, uint32_t version)
{
  GstVaapiDisplayWaylandPrivate *const priv = data;

  if (strcmp (interface, "wl_compositor") == 0)
    priv->compositor =
        wl_registry_bind (registry, id, &wl_compositor_interface, 1);
  else if (strcmp (interface, "wl_shell") == 0)
    priv->shell = wl_registry_bind (registry, id, &wl_shell_interface, 1);
  else if (strcmp (interface, "wl_output") == 0) {
    priv->output = wl_registry_bind (registry, id, &wl_output_interface, 1);
    wl_output_add_listener (priv->output, &output_listener, priv);
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

  if (!priv->shell) {
    GST_ERROR ("failed to bind shell interface");
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
  GstVaapiDisplayCache *const cache = GST_VAAPI_DISPLAY_CACHE (display);
  const GstVaapiDisplayInfo *info;
  int dsp_error = 0;

  if (!set_display_name (display, name))
    return FALSE;

  info = gst_vaapi_display_cache_lookup_custom (cache, compare_display_name,
      priv->display_name, g_display_types);
  if (info) {
    wl_display_roundtrip (info->native_display);
    if ((dsp_error = wl_display_get_error (info->native_display)))
      GST_ERROR ("wayland display error detected: %d", dsp_error);
  }
  if (info && !dsp_error) {
    priv->wl_display = info->native_display;
    priv->use_foreign_display = TRUE;
  } else {
    priv->wl_display = wl_display_connect (name);
    if (!priv->wl_display)
      return FALSE;
    priv->use_foreign_display = FALSE;
  }
  return gst_vaapi_display_wayland_setup (display);
}

static void
gst_vaapi_display_wayland_close_display (GstVaapiDisplay * display)
{
  GstVaapiDisplayWaylandPrivate *const priv =
      GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE (display);

  if (priv->output) {
    wl_output_destroy (priv->output);
    priv->output = NULL;
  }

  if (priv->shell) {
    wl_shell_destroy (priv->shell);
    priv->shell = NULL;
  }

  if (priv->compositor) {
    wl_compositor_destroy (priv->compositor);
    priv->compositor = NULL;
  }

  if (priv->registry) {
    wl_registry_destroy (priv->registry);
    priv->registry = NULL;
  }

  if (priv->wl_display) {
    if (!priv->use_foreign_display)
      wl_display_disconnect (priv->wl_display);
    priv->wl_display = NULL;
  }

  if (priv->display_name) {
    g_free (priv->display_name);
    priv->display_name = NULL;
  }
}

static gboolean
gst_vaapi_display_wayland_get_display_info (GstVaapiDisplay * display,
    GstVaapiDisplayInfo * info)
{
  GstVaapiDisplayWaylandPrivate *const priv =
      GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE (display);
  GstVaapiDisplayCache *const cache = GST_VAAPI_DISPLAY_CACHE (display);
  const GstVaapiDisplayInfo *cached_info;

  /* Return any cached info even if child has its own VA display */
  cached_info = gst_vaapi_display_cache_lookup_by_native_display (cache,
      priv->wl_display, g_display_types);
  if (cached_info) {
    *info = *cached_info;
    return TRUE;
  }

  /* Otherwise, create VA display if there is none already */
  info->native_display = priv->wl_display;
  info->display_name = priv->display_name;
  if (!info->va_display) {
    info->va_display = vaGetDisplayWl (priv->wl_display);
    if (!info->va_display)
      return FALSE;
    info->display_type = GST_VAAPI_DISPLAY_TYPE_WAYLAND;
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
  return gst_vaapi_display_new (g_object_new (GST_TYPE_VAAPI_DISPLAY_WAYLAND,
          NULL), GST_VAAPI_DISPLAY_INIT_FROM_DISPLAY_NAME,
      (gpointer) display_name);
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
gst_vaapi_display_wayland_new_with_display (struct wl_display * wl_display)
{
  g_return_val_if_fail (wl_display, NULL);

  return gst_vaapi_display_new (g_object_new (GST_TYPE_VAAPI_DISPLAY_WAYLAND,
          NULL), GST_VAAPI_DISPLAY_INIT_FROM_NATIVE_DISPLAY, wl_display);
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
