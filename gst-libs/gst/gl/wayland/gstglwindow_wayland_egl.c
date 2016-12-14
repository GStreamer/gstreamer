/*
 * GStreamer
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
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

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <linux/input.h>

#include "wayland_event_source.h"

#include "../gstgl_fwd.h"
#include <gst/gl/gstglcontext.h>

#include "gstgldisplay_wayland.h"
#include "gstglwindow_wayland_egl.h"

const gchar *WlEGLErrorString ();

#define GST_CAT_DEFAULT gst_gl_window_debug

#define gst_gl_window_wayland_egl_parent_class parent_class
G_DEFINE_TYPE (GstGLWindowWaylandEGL, gst_gl_window_wayland_egl,
    GST_TYPE_GL_WINDOW);

static guintptr gst_gl_window_wayland_egl_get_window_handle (GstGLWindow *
    window);
static void gst_gl_window_wayland_egl_set_window_handle (GstGLWindow * window,
    guintptr handle);
static void gst_gl_window_wayland_egl_show (GstGLWindow * window);
static void gst_gl_window_wayland_egl_draw (GstGLWindow * window);
static void gst_gl_window_wayland_egl_close (GstGLWindow * window);
static gboolean gst_gl_window_wayland_egl_open (GstGLWindow * window,
    GError ** error);
static guintptr gst_gl_window_wayland_egl_get_display (GstGLWindow * window);
static gboolean gst_gl_window_wayland_egl_set_render_rectangle (GstGLWindow *
    window, gint x, gint y, gint width, gint height);

#if 0
static void
pointer_handle_enter (void *data, struct wl_pointer *pointer, uint32_t serial,
    struct wl_surface *surface, wl_fixed_t sx_w, wl_fixed_t sy_w)
{
  GstGLWindowWaylandEGL *window_egl = data;
  struct wl_buffer *buffer;
  struct wl_cursor_image *image = NULL;

  window_egl->display.serial = serial;

  if (window_egl->display.default_cursor) {
    image = window_egl->display.default_cursor->images[0];
    buffer = wl_cursor_image_get_buffer (image);
    wl_pointer_set_cursor (pointer, serial,
        window_egl->display.cursor_surface, image->hotspot_x, image->hotspot_y);
    wl_surface_attach (window_egl->display.cursor_surface, buffer, 0, 0);
    wl_surface_damage (window_egl->display.cursor_surface, 0, 0,
        image->width, image->height);
    wl_surface_commit (window_egl->display.cursor_surface);
  }
}

static void
pointer_handle_leave (void *data, struct wl_pointer *pointer, uint32_t serial,
    struct wl_surface *surface)
{
  GstGLWindowWaylandEGL *window_egl = data;
  window_egl->display.serial = serial;
}

static void
pointer_handle_motion (void *data, struct wl_pointer *pointer, uint32_t time,
    wl_fixed_t sx_w, wl_fixed_t sy_w)
{
  GstGLWindowWaylandEGL *window_egl = data;

  window_egl->display.pointer_x = wl_fixed_to_double (sx_w);
  window_egl->display.pointer_y = wl_fixed_to_double (sy_w);
}

enum wl_edges
{
  WL_EDGE_NONE = 0,
  WL_EDGE_TOP = 1,
  WL_EDGE_BOTTOM = 2,
  WL_EDGE_LEFT = 4,
  WL_EDGE_RIGHT = 8,
};

static guint
_get_closest_pointer_corner (GstGLWindowWaylandEGL * window_egl)
{
  guint edges = 0;
  gdouble win_width, win_height;
  gdouble p_x, p_y;

  win_width = (gdouble) window_egl->window.window_width;
  win_height = (gdouble) window_egl->window.window_height;
  p_x = window_egl->display.pointer_x;
  p_y = window_egl->display.pointer_y;

  if (win_width == 0.0 || win_height == 0.0)
    return WL_EDGE_NONE;

  edges |= win_width / 2.0 - p_x < 0.0 ? WL_EDGE_RIGHT : WL_EDGE_LEFT;
  edges |= win_height / 2.0 - p_y < 0.0 ? WL_EDGE_BOTTOM : WL_EDGE_TOP;

  return edges;
}

static void
pointer_handle_button (void *data, struct wl_pointer *pointer, uint32_t serial,
    uint32_t time, uint32_t button, uint32_t state_w)
{
  GstGLWindowWaylandEGL *window_egl = data;
  guint edges = _get_closest_pointer_corner (window_egl);
  window_egl->display.serial = serial;

  if (button == BTN_LEFT && state_w == WL_POINTER_BUTTON_STATE_PRESSED)
    wl_shell_surface_move (window_egl->window.shell_surface,
        window_egl->display.seat, serial);

  if (button == BTN_RIGHT && state_w == WL_POINTER_BUTTON_STATE_PRESSED)
    wl_shell_surface_resize (window_egl->window.shell_surface,
        window_egl->display.seat, serial, edges);
}

static void
pointer_handle_axis (void *data, struct wl_pointer *pointer, uint32_t time,
    uint32_t axis, wl_fixed_t value)
{
}

static const struct wl_pointer_listener pointer_listener = {
  pointer_handle_enter,
  pointer_handle_leave,
  pointer_handle_motion,
  pointer_handle_button,
  pointer_handle_axis,
};

static void
seat_handle_capabilities (void *data, struct wl_seat *seat,
    enum wl_seat_capability caps)
{
  GstGLWindowWaylandEGL *window_egl = data;
  struct display *display = &window_egl->display;

  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !display->pointer) {
    display->pointer = wl_seat_get_pointer (seat);
    wl_pointer_set_user_data (display->pointer, window_egl);
    wl_pointer_add_listener (display->pointer, &pointer_listener, window_egl);
  } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && display->pointer) {
    wl_pointer_destroy (display->pointer);
    display->pointer = NULL;
  }
#if 0
  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !input->keyboard) {
    input->keyboard = wl_seat_get_keyboard (seat);
    wl_keyboard_set_user_data (input->keyboard, input);
    wl_keyboard_add_listener (input->keyboard, &keyboard_listener, input);
  } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && input->keyboard) {
    wl_keyboard_destroy (input->keyboard);
    input->keyboard = NULL;
  }
#endif
}

static const struct wl_seat_listener seat_listener = {
  seat_handle_capabilities,
};
#endif
static void
handle_ping (void *data, struct wl_shell_surface *shell_surface,
    uint32_t serial)
{
  GstGLWindowWaylandEGL *window_egl = data;

  GST_TRACE_OBJECT (window_egl, "ping received serial %u", serial);

  wl_shell_surface_pong (shell_surface, serial);
}

static void window_resize (GstGLWindowWaylandEGL * window_egl, guint width,
    guint height);

static void
handle_configure (void *data, struct wl_shell_surface *shell_surface,
    uint32_t edges, int32_t width, int32_t height)
{
  GstGLWindowWaylandEGL *window_egl = data;

  GST_DEBUG ("configure event on surface %p, %ix%i", shell_surface, width,
      height);

  window_resize (window_egl, width, height);
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
destroy_surfaces (GstGLWindowWaylandEGL * window_egl)
{
  if (window_egl->window.subsurface) {
    wl_subsurface_destroy (window_egl->window.subsurface);
    window_egl->window.subsurface = NULL;
  }
  if (window_egl->window.shell_surface) {
    wl_shell_surface_destroy (window_egl->window.shell_surface);
    window_egl->window.shell_surface = NULL;
  }
  if (window_egl->window.surface) {
    wl_surface_destroy (window_egl->window.surface);
    window_egl->window.surface = NULL;
  }
  if (window_egl->window.native) {
    wl_egl_window_destroy (window_egl->window.native);
    window_egl->window.native = NULL;
  }
}

static void
create_surfaces (GstGLWindowWaylandEGL * window_egl)
{
  GstGLDisplayWayland *display =
      GST_GL_DISPLAY_WAYLAND (GST_GL_WINDOW (window_egl)->display);
  gint width, height;

  if (!window_egl->window.surface) {
    window_egl->window.surface =
        wl_compositor_create_surface (display->compositor);
    if (window_egl->window.queue)
      wl_proxy_set_queue ((struct wl_proxy *) window_egl->window.surface,
          window_egl->window.queue);
  }

  if (window_egl->window.foreign_surface) {
    /* (re)parent */
    if (!display->subcompositor) {
      GST_ERROR_OBJECT (window_egl,
          "Wayland server does not support subsurfaces");
      window_egl->window.foreign_surface = NULL;
      goto shell_window;
    }

    if (!window_egl->window.subsurface) {
      window_egl->window.subsurface =
          wl_subcompositor_get_subsurface (display->subcompositor,
          window_egl->window.surface, window_egl->window.foreign_surface);
      if (window_egl->window.queue)
        wl_proxy_set_queue ((struct wl_proxy *) window_egl->window.subsurface,
            window_egl->window.queue);

      wl_subsurface_set_position (window_egl->window.subsurface,
          window_egl->window.window_x, window_egl->window.window_y);
      wl_subsurface_set_desync (window_egl->window.subsurface);
    }
  } else {
  shell_window:
    if (!window_egl->window.shell_surface) {
      window_egl->window.shell_surface =
          wl_shell_get_shell_surface (display->shell,
          window_egl->window.surface);
      if (window_egl->window.queue)
        wl_proxy_set_queue ((struct wl_proxy *) window_egl->window.
            shell_surface, window_egl->window.queue);

      wl_shell_surface_add_listener (window_egl->window.shell_surface,
          &shell_surface_listener, window_egl);

      wl_shell_surface_set_title (window_egl->window.shell_surface,
          "OpenGL Renderer");
      wl_shell_surface_set_toplevel (window_egl->window.shell_surface);
    }
  }

  if (window_egl->window.window_width > 0)
    width = window_egl->window.window_width;
  else
    width = 320;
  window_egl->window.window_width = width;

  if (window_egl->window.window_height > 0)
    height = window_egl->window.window_height;
  else
    height = 240;
  window_egl->window.window_height = height;

  if (!window_egl->window.native) {
    gst_gl_window_resize (GST_GL_WINDOW (window_egl), width, height);

    window_egl->window.native =
        wl_egl_window_create (window_egl->window.surface, width, height);
    if (window_egl->window.queue)
      wl_proxy_set_queue ((struct wl_proxy *) window_egl->window.native,
          window_egl->window.queue);
  }
}

static void
gst_gl_window_wayland_egl_class_init (GstGLWindowWaylandEGLClass * klass)
{
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;

  window_class->get_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_get_window_handle);
  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_set_window_handle);
  window_class->show = GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_show);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_draw);
  window_class->close = GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_close);
  window_class->open = GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_open);
  window_class->get_display =
      GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_get_display);
  window_class->set_render_rectangle =
      GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_set_render_rectangle);
}

static void
gst_gl_window_wayland_egl_init (GstGLWindowWaylandEGL * window)
{
}

/* Must be called in the gl thread */
GstGLWindowWaylandEGL *
gst_gl_window_wayland_egl_new (GstGLDisplay * display)
{
  if ((gst_gl_display_get_handle_type (display) & GST_GL_DISPLAY_TYPE_WAYLAND)
      == 0)
    /* we require a wayland display to create wayland surfaces */
    return NULL;

  GST_DEBUG ("creating Wayland EGL window");

  return g_object_new (GST_TYPE_GL_WINDOW_WAYLAND_EGL, NULL);
}

static void
gst_gl_window_wayland_egl_close (GstGLWindow * window)
{
  GstGLWindowWaylandEGL *window_egl;

  window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);

  destroy_surfaces (window_egl);

  g_source_destroy (window_egl->wl_source);
  g_source_unref (window_egl->wl_source);
  window_egl->wl_source = NULL;

  GST_GL_WINDOW_CLASS (parent_class)->close (window);
}

static gboolean
gst_gl_window_wayland_egl_open (GstGLWindow * window, GError ** error)
{
  GstGLDisplayWayland *display;
  GstGLWindowWaylandEGL *window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);

  if (!GST_IS_GL_DISPLAY_WAYLAND (window->display)) {
    g_set_error (error, GST_GL_WINDOW_ERROR,
        GST_GL_WINDOW_ERROR_RESOURCE_UNAVAILABLE,
        "Failed to retrieve Wayland display (wrong type?)");
    return FALSE;
  }
  display = GST_GL_DISPLAY_WAYLAND (window->display);

  if (!display->display) {
    g_set_error (error, GST_GL_WINDOW_ERROR,
        GST_GL_WINDOW_ERROR_RESOURCE_UNAVAILABLE,
        "Failed to retrieve Wayland display");
    return FALSE;
  }

  window_egl->window.queue = wl_display_create_queue (display->display);

  window_egl->wl_source = wayland_event_source_new (display->display,
      window_egl->window.queue);

  if (!GST_GL_WINDOW_CLASS (parent_class)->open (window, error))
    return FALSE;

  g_source_attach (window_egl->wl_source, window->main_context);

  return TRUE;
}

void
gst_gl_window_wayland_egl_create_window (GstGLWindowWaylandEGL * window_egl)
{
  create_surfaces (window_egl);
}

static guintptr
gst_gl_window_wayland_egl_get_window_handle (GstGLWindow * window)
{
  return (guintptr) GST_GL_WINDOW_WAYLAND_EGL (window)->window.native;
}

static void
gst_gl_window_wayland_egl_set_window_handle (GstGLWindow * window,
    guintptr handle)
{
  GstGLWindowWaylandEGL *window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);
  struct wl_surface *surface = (struct wl_surface *) handle;

  /* already set the NULL handle */
  if (surface == NULL && window_egl->window.foreign_surface == NULL)
    return;

  /* unparent */
  destroy_surfaces (window_egl);
  window_egl->window.foreign_surface = surface;
  create_surfaces (window_egl);
}

static void
gst_gl_window_wayland_egl_show (GstGLWindow * window)
{
  GstGLDisplayWayland *display_wayland =
      GST_GL_DISPLAY_WAYLAND (window->display);
  GstGLWindowWaylandEGL *window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);

  create_surfaces (window_egl);

  if (gst_gl_wl_display_roundtrip_queue (display_wayland->display,
          window_egl->window.queue) < 0)
    GST_WARNING_OBJECT (window, "failed a roundtrip");
}

static void
window_resize (GstGLWindowWaylandEGL * window_egl, guint width, guint height)
{
  GstGLWindow *window = GST_GL_WINDOW (window_egl);

  GST_DEBUG ("resizing window from %ux%u to %ux%u",
      window_egl->window.window_width, window_egl->window.window_height, width,
      height);

  if (window_egl->window.native) {
    wl_egl_window_resize (window_egl->window.native, width, height, 0, 0);
  }

  gst_gl_window_resize (window, width, height);

  window_egl->window.window_width = width;
  window_egl->window.window_height = height;
}

static void
draw_cb (gpointer data)
{
  GstGLWindowWaylandEGL *window_egl = data;
  GstGLWindow *window = GST_GL_WINDOW (window_egl);
  GstGLContext *context = gst_gl_window_get_context (window);
  GstGLContextClass *context_class = GST_GL_CONTEXT_GET_CLASS (context);

  create_surfaces (window_egl);

  if (window_egl->window.subsurface)
    wl_subsurface_set_desync (window_egl->window.subsurface);

  if (window->queue_resize) {
    guint width, height;

    gst_gl_window_get_surface_dimensions (window, &width, &height);
    gst_gl_window_resize (window, width, height);
  }

  if (window->draw)
    window->draw (window->draw_data);

  context_class->swap_buffers (context);

  if (window_egl->window.subsurface)
    wl_subsurface_set_desync (window_egl->window.subsurface);

  gst_object_unref (context);
}

static void
gst_gl_window_wayland_egl_draw (GstGLWindow * window)
{
  gst_gl_window_send_message (window, (GstGLWindowCB) draw_cb, window);
}

struct SetRenderRectangle
{
  GstGLWindowWaylandEGL *window_egl;
  GstVideoRectangle rect;
};

static void
_free_set_render_rectangle (struct SetRenderRectangle *render)
{
  if (render) {
    if (render->window_egl)
      gst_object_unref (render->window_egl);
    g_free (render);
  }
}

static void
_set_render_rectangle (gpointer data)
{
  struct SetRenderRectangle *render = data;

  GST_LOG_OBJECT (render->window_egl, "setting render rectangle %i,%i+%ix%i",
      render->rect.x, render->rect.y, render->rect.w, render->rect.h);

  if (render->window_egl->window.subsurface) {
    wl_subsurface_set_sync (render->window_egl->window.subsurface);
    wl_subsurface_set_position (render->window_egl->window.subsurface,
        render->rect.x, render->rect.y);
    render->window_egl->window.window_x = render->rect.x;
    render->window_egl->window.window_y = render->rect.y;
  }

  window_resize (render->window_egl, render->rect.w, render->rect.h);
}

static gboolean
gst_gl_window_wayland_egl_set_render_rectangle (GstGLWindow * window,
    gint x, gint y, gint width, gint height)
{
  GstGLWindowWaylandEGL *window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);
  struct SetRenderRectangle *render;

  render = g_new0 (struct SetRenderRectangle, 1);
  render->window_egl = gst_object_ref (window_egl);
  render->rect.x = x;
  render->rect.y = y;
  render->rect.w = width;
  render->rect.h = height;

  gst_gl_window_send_message_async (window,
      (GstGLWindowCB) _set_render_rectangle, render,
      (GDestroyNotify) _free_set_render_rectangle);

  return TRUE;
}

static guintptr
gst_gl_window_wayland_egl_get_display (GstGLWindow * window)
{
  GstGLDisplayWayland *display = GST_GL_DISPLAY_WAYLAND (window->display);

  return (guintptr) display->display;
}
