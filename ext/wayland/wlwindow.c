/* GStreamer Wayland video sink
 *
 * Copyright (C) 2011 Intel Corporation
 * Copyright (C) 2011 Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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

#include "wlwindow.h"

GST_DEBUG_CATEGORY_EXTERN (gstwayland_debug);
#define GST_CAT_DEFAULT gstwayland_debug

G_DEFINE_TYPE (GstWlWindow, gst_wl_window, G_TYPE_OBJECT);

static void gst_wl_window_finalize (GObject * gobject);

static void
handle_ping (void *data, struct wl_shell_surface *shell_surface,
    uint32_t serial)
{
  wl_shell_surface_pong (shell_surface, serial);
}

static void
handle_configure (void *data, struct wl_shell_surface *shell_surface,
    uint32_t edges, int32_t width, int32_t height)
{
}

static void
handle_popup_done (void *data, struct wl_shell_surface *shell_surface)
{
}

static const struct wl_shell_surface_listener shell_surface_listener = {
  handle_ping,
  handle_configure,
  handle_popup_done
};

static void
gst_wl_window_class_init (GstWlWindowClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = gst_wl_window_finalize;
}

static void
gst_wl_window_init (GstWlWindow * self)
{
}

static void
gst_wl_window_finalize (GObject * gobject)
{
  GstWlWindow *self = GST_WL_WINDOW (gobject);

  if (self->shell_surface) {
    wl_shell_surface_destroy (self->shell_surface);
  }

  if (self->subsurface) {
    wl_subsurface_destroy (self->subsurface);
  }

  wl_viewport_destroy (self->viewport);
  wl_surface_destroy (self->surface);

  g_clear_object (&self->display);

  G_OBJECT_CLASS (gst_wl_window_parent_class)->finalize (gobject);
}

static GstWlWindow *
gst_wl_window_new_internal (GstWlDisplay * display, struct wl_surface *surface)
{
  GstWlWindow *window;
  struct wl_region *region;

  g_return_val_if_fail (surface != NULL, NULL);

  window = g_object_new (GST_TYPE_WL_WINDOW, NULL);
  window->display = g_object_ref (display);
  window->surface = surface;

  /* make sure the surface runs on our local queue */
  wl_proxy_set_queue ((struct wl_proxy *) surface, display->queue);

  window->viewport = wl_scaler_get_viewport (display->scaler, window->surface);

  /* do not accept input */
  region = wl_compositor_create_region (display->compositor);
  wl_surface_set_input_region (surface, region);
  wl_region_destroy (region);

  return window;
}

GstWlWindow *
gst_wl_window_new_toplevel (GstWlDisplay * display, GstVideoInfo * video_info)
{
  GstWlWindow *window;

  window = gst_wl_window_new_internal (display,
      wl_compositor_create_surface (display->compositor));

  gst_wl_window_set_video_info (window, video_info);
  gst_wl_window_set_render_rectangle (window, 0, 0, window->video_width,
      window->video_height);

  window->shell_surface = wl_shell_get_shell_surface (display->shell,
      window->surface);

  if (window->shell_surface) {
    wl_shell_surface_add_listener (window->shell_surface,
        &shell_surface_listener, window);
    wl_shell_surface_set_toplevel (window->shell_surface);
  } else {
    GST_ERROR ("Unable to get wl_shell_surface");

    g_object_unref (window);
    return NULL;
  }

  return window;
}

GstWlWindow *
gst_wl_window_new_in_surface (GstWlDisplay * display,
    struct wl_surface * parent)
{
  GstWlWindow *window;

  window = gst_wl_window_new_internal (display,
      wl_compositor_create_surface (display->compositor));

  window->subsurface = wl_subcompositor_get_subsurface (display->subcompositor,
      window->surface, parent);
  wl_subsurface_set_desync (window->subsurface);

  return window;
}

GstWlDisplay *
gst_wl_window_get_display (GstWlWindow * window)
{
  g_return_val_if_fail (window != NULL, NULL);

  return g_object_ref (window->display);
}

struct wl_surface *
gst_wl_window_get_wl_surface (GstWlWindow * window)
{
  g_return_val_if_fail (window != NULL, NULL);

  return window->surface;
}

gboolean
gst_wl_window_is_toplevel (GstWlWindow * window)
{
  g_return_val_if_fail (window != NULL, FALSE);

  return (window->shell_surface != NULL);
}

static void
gst_wl_window_resize_internal (GstWlWindow * window, gboolean commit)
{
  GstVideoRectangle src, res;

  src.w = window->video_width;
  src.h = window->video_height;
  gst_video_sink_center_rect (src, window->render_rectangle, &res, TRUE);

  if (window->subsurface)
    wl_subsurface_set_position (window->subsurface,
        window->render_rectangle.x + res.x, window->render_rectangle.y + res.y);
  wl_viewport_set_destination (window->viewport, res.w, res.h);

  if (commit) {
    wl_surface_damage (window->surface, 0, 0, res.w, res.h);
    wl_surface_commit (window->surface);
  }

  /* this is saved for use in wl_surface_damage */
  window->surface_width = res.w;
  window->surface_height = res.h;
}

void
gst_wl_window_set_video_info (GstWlWindow * window, GstVideoInfo * info)
{
  g_return_if_fail (window != NULL);

  window->video_width =
      gst_util_uint64_scale_int_round (info->width, info->par_n, info->par_d);
  window->video_height = info->height;

  if (window->render_rectangle.w != 0)
    gst_wl_window_resize_internal (window, FALSE);
}

void
gst_wl_window_set_render_rectangle (GstWlWindow * window, gint x, gint y,
    gint w, gint h)
{
  g_return_if_fail (window != NULL);

  window->render_rectangle.x = x;
  window->render_rectangle.y = y;
  window->render_rectangle.w = w;
  window->render_rectangle.h = h;

  if (window->video_width != 0)
    gst_wl_window_resize_internal (window, TRUE);
}
