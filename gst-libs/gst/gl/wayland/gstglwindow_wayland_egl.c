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
    GST_GL_TYPE_WINDOW);
static void gst_gl_window_wayland_egl_finalize (GObject * object);

static guintptr gst_gl_window_wayland_egl_get_window_handle (GstGLWindow *
    window);
static void gst_gl_window_wayland_egl_set_window_handle (GstGLWindow * window,
    guintptr handle);
static void gst_gl_window_wayland_egl_draw (GstGLWindow * window);
static void gst_gl_window_wayland_egl_run (GstGLWindow * window);
static void gst_gl_window_wayland_egl_quit (GstGLWindow * window);
static void gst_gl_window_wayland_egl_send_message_async (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data, GDestroyNotify destroy);
static void gst_gl_window_wayland_egl_close (GstGLWindow * window);
static gboolean gst_gl_window_wayland_egl_open (GstGLWindow * window,
    GError ** error);
static guintptr gst_gl_window_wayland_egl_get_display (GstGLWindow * window);

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

static gboolean
create_surface (GstGLWindowWaylandEGL * window_egl)
{
  GstGLDisplayWayland *display =
      GST_GL_DISPLAY_WAYLAND (GST_GL_WINDOW (window_egl)->display);

  window_egl->window.surface =
      wl_compositor_create_surface (display->compositor);
  window_egl->window.shell_surface =
      wl_shell_get_shell_surface (display->shell, window_egl->window.surface);

  wl_shell_surface_add_listener (window_egl->window.shell_surface,
      &shell_surface_listener, window_egl);

  if (window_egl->window.window_width <= 0)
    window_egl->window.window_width = 320;
  if (window_egl->window.window_height <= 0)
    window_egl->window.window_height = 240;

  window_egl->window.native =
      wl_egl_window_create (window_egl->window.surface,
      window_egl->window.window_width, window_egl->window.window_height);

  wl_shell_surface_set_title (window_egl->window.shell_surface,
      "OpenGL Renderer");

  wl_shell_surface_set_toplevel (window_egl->window.shell_surface);

  return TRUE;
}

static void
destroy_surface (GstGLWindowWaylandEGL * window_egl)
{
  if (window_egl->window.native)
    wl_egl_window_destroy (window_egl->window.native);

  if (window_egl->window.shell_surface)
    wl_shell_surface_destroy (window_egl->window.shell_surface);

  if (window_egl->window.surface)
    wl_surface_destroy (window_egl->window.surface);

  if (window_egl->window.callback)
    wl_callback_destroy (window_egl->window.callback);
}

static void
gst_gl_window_wayland_egl_class_init (GstGLWindowWaylandEGLClass * klass)
{
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;
  GObjectClass *gobject_class = (GObjectClass *) klass;

  window_class->get_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_get_window_handle);
  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_set_window_handle);
  window_class->draw_unlocked =
      GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_draw);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_draw);
  window_class->run = GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_run);
  window_class->quit = GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_quit);
  window_class->send_message_async =
      GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_send_message_async);
  window_class->close = GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_close);
  window_class->open = GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_open);
  window_class->get_display =
      GST_DEBUG_FUNCPTR (gst_gl_window_wayland_egl_get_display);

  gobject_class->finalize = gst_gl_window_wayland_egl_finalize;
}

static void
gst_gl_window_wayland_egl_init (GstGLWindowWaylandEGL * window)
{
  window->main_context = g_main_context_new ();
  window->loop = g_main_loop_new (window->main_context, FALSE);
}

static void
gst_gl_window_wayland_egl_finalize (GObject * object)
{
  GstGLWindowWaylandEGL *window_egl = GST_GL_WINDOW_WAYLAND_EGL (object);

  g_main_loop_unref (window_egl->loop);
  g_main_context_unref (window_egl->main_context);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* Must be called in the gl thread */
GstGLWindowWaylandEGL *
gst_gl_window_wayland_egl_new (void)
{
  GstGLWindowWaylandEGL *window;

  GST_DEBUG ("creating Wayland EGL window");

  window = g_object_new (GST_GL_TYPE_WINDOW_WAYLAND_EGL, NULL);

  return window;
}

static void
gst_gl_window_wayland_egl_close (GstGLWindow * window)
{
  GstGLWindowWaylandEGL *window_egl;

  window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);

  destroy_surface (window_egl);

  g_source_destroy (window_egl->wl_source);
  g_source_unref (window_egl->wl_source);
  window_egl->wl_source = NULL;
}

static gboolean
gst_gl_window_wayland_egl_open (GstGLWindow * window, GError ** error)
{
  GstGLDisplayWayland *display = GST_GL_DISPLAY_WAYLAND (window->display);
  GstGLWindowWaylandEGL *window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);

  if (!display->display) {
    g_set_error (error, GST_GL_WINDOW_ERROR,
        GST_GL_WINDOW_ERROR_RESOURCE_UNAVAILABLE,
        "Failed to retreive Wayland display");
    return FALSE;
  }

  wl_display_roundtrip (display->display);

  create_surface (window_egl);

  window_egl->wl_source = wayland_event_source_new (display->display);

  g_source_attach (window_egl->wl_source, window_egl->main_context);

  return TRUE;
}

static void
gst_gl_window_wayland_egl_run (GstGLWindow * window)
{
  GstGLWindowWaylandEGL *window_egl;

  window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);

  GST_LOG ("starting main loop");
  g_main_loop_run (window_egl->loop);
  GST_LOG ("exiting main loop");
}

static void
gst_gl_window_wayland_egl_quit (GstGLWindow * window)
{
  GstGLWindowWaylandEGL *window_egl;

  window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);

  GST_LOG ("sending quit");

  g_main_loop_quit (window_egl->loop);

  GST_LOG ("quit sent");
}

typedef struct _GstGLMessage
{
  GstGLWindowCB callback;
  gpointer data;
  GDestroyNotify destroy;
} GstGLMessage;

static gboolean
_run_message (GstGLMessage * message)
{
  if (message->callback)
    message->callback (message->data);

  if (message->destroy)
    message->destroy (message->data);

  g_slice_free (GstGLMessage, message);

  return FALSE;
}

static void
gst_gl_window_wayland_egl_send_message_async (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data, GDestroyNotify destroy)
{
  GstGLWindowWaylandEGL *window_egl;
  GstGLMessage *message;

  window_egl = GST_GL_WINDOW_WAYLAND_EGL (window);
  message = g_slice_new (GstGLMessage);

  message->callback = callback;
  message->data = data;
  message->destroy = destroy;

  g_main_context_invoke (window_egl->main_context, (GSourceFunc) _run_message,
      message);
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

  if (window->resize)
    window->resize (window->resize_data, width, height);

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

  if (window->draw)
    window->draw (window->draw_data);

  context_class->swap_buffers (context);

  gst_object_unref (context);
}

static void
gst_gl_window_wayland_egl_draw (GstGLWindow * window)
{
  gst_gl_window_send_message (window, (GstGLWindowCB) draw_cb, window);
}

static guintptr
gst_gl_window_wayland_egl_get_display (GstGLWindow * window)
{
  GstGLDisplayWayland *display = GST_GL_DISPLAY_WAYLAND (window->display);

  return (guintptr) display->display;
}
