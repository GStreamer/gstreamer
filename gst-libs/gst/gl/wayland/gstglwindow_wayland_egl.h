/*
 * GStreamer
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

#ifndef __GST_GL_WINDOW_WAYLAND_EGL_H__
#define __GST_GL_WINDOW_WAYLAND_EGL_H__

#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-cursor.h>

#include <gst/gl/gl.h>

G_BEGIN_DECLS

#define GST_GL_TYPE_WINDOW_WAYLAND_EGL         (gst_gl_window_wayland_egl_get_type())
#define GST_GL_WINDOW_WAYLAND_EGL(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_GL_TYPE_WINDOW_WAYLAND_EGL, GstGLWindowWaylandEGL))
#define GST_GL_WINDOW_WAYLAND_EGL_CLASS(k)     (G_TYPE_CHECK_CLASS((k), GST_GL_TYPE_WINDOW_WAYLAND_EGL, GstGLWindowWaylandEGLClass))
#define GST_GL_IS_WINDOW_WAYLAND_EGL(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_GL_TYPE_WINDOW_WAYLAND_EGL))
#define GST_GL_IS_WINDOW_WAYLAND_EGL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_GL_TYPE_WINDOW_WAYLAND_EGL))
#define GST_GL_WINDOW_WAYLAND_EGL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_GL_TYPE_WINDOW_WAYLAND_EGL, GstGLWindowWaylandEGL_Class))

typedef struct _GstGLWindowWaylandEGL        GstGLWindowWaylandEGL;
typedef struct _GstGLWindowWaylandEGLClass   GstGLWindowWaylandEGLClass;

struct window;

struct display {
  struct wl_display      *display;
  struct wl_registry     *registry;
  struct wl_compositor   *compositor;
  struct wl_shell        *shell;
  struct wl_seat         *seat;
  struct wl_pointer      *pointer;
  struct wl_keyboard     *keyboard;
  struct wl_shm          *shm;
  struct wl_cursor_theme *cursor_theme;
  struct wl_cursor       *default_cursor;
  struct wl_surface      *cursor_surface;
  struct window          *window;
  guint32                 serial;
};

struct window {
  struct display *display;

  struct wl_egl_window      *native;
  struct wl_surface         *surface;
  struct wl_shell_surface   *shell_surface;
  struct wl_callback        *callback;
  int fullscreen, configured;
  int window_width, window_height;
};

struct _GstGLWindowWaylandEGL {
  /*< private >*/
  GstGLWindow parent;

  struct display display;
  struct window  window;

  GSource *wl_source;
  GMainContext *main_context;
  GMainLoop *loop;

  gpointer _reserved[GST_PADDING];
};

struct _GstGLWindowWaylandEGLClass {
  /*< private >*/
  GstGLWindowClass parent_class;

  /*< private >*/
  gpointer _reserved[GST_PADDING];
};

GType gst_gl_window_wayland_egl_get_type     (void);

GstGLWindowWaylandEGL * gst_gl_window_wayland_egl_new  (void);

G_END_DECLS

#endif /* __GST_GL_WINDOW_X11_H__ */
