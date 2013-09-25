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

#ifndef __GST_GL_WINDOW_H__
#define __GST_GL_WINDOW_H__

#include <gst/gst.h>

#include <gst/gl/gstgl_fwd.h>

G_BEGIN_DECLS

#define GST_GL_TYPE_WINDOW         (gst_gl_window_get_type())
#define GST_GL_WINDOW(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_GL_TYPE_WINDOW, GstGLWindow))
#define GST_GL_WINDOW_CLASS(k)     (G_TYPE_CHECK_CLASS((k), GST_GL_TYPE_WINDOW, GstGLWindowClass))
#define GST_GL_IS_WINDOW(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_GL_TYPE_WINDOW))
#define GST_GL_IS_WINDOW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_GL_TYPE_WINDOW))
#define GST_GL_WINDOW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_GL_TYPE_WINDOW, GstGLWindowClass))

#define GST_GL_WINDOW_LOCK(w) \
  do { \
    if (GST_GL_WINDOW(w)->need_lock) \
      g_mutex_lock (&GST_GL_WINDOW(w)->lock); \
  } while (0)

#define GST_GL_WINDOW_UNLOCK(w) \
  do { \
    if (GST_GL_WINDOW(w)->need_lock) \
      g_mutex_unlock (&GST_GL_WINDOW(w)->lock); \
  } while (0)

#define GST_GL_WINDOW_GET_LOCK(w) (&GST_GL_WINDOW(w)->lock)

#define GST_GL_WINDOW_ERROR (gst_gl_window_error_quark ())

typedef enum
{
  GST_GL_WINDOW_ERROR_FAILED,
  GST_GL_WINDOW_ERROR_OLD_LIBS,
  GST_GL_WINDOW_ERROR_RESOURCE_UNAVAILABLE,
} GstGLWindowError;

typedef void (*GstGLWindowCB) (gpointer data);
typedef void (*GstGLWindowResizeCB) (gpointer data, guint width, guint height);

#define	GST_GL_WINDOW_CB(f)			 ((GstGLWindowCB) (f))
#define	GST_GL_WINDOW_RESIZE_CB(f)		 ((GstGLWindowResizeCB) (f))

struct _GstGLWindow {
  /*< private >*/
  GObject parent;

  /*< public >*/
  GMutex        lock;
  gboolean      need_lock;

  GWeakRef      context_ref;

  guintptr      external_gl_context;

  GstGLWindowCB         draw;
  gpointer              draw_data;
  GDestroyNotify        draw_notify;
  GstGLWindowCB         close;
  gpointer              close_data;
  GDestroyNotify        close_notify;
  GstGLWindowResizeCB   resize;
  gpointer              resize_data;
  GDestroyNotify        resize_notify;

  /*< private >*/
  gpointer _reserved[GST_PADDING];

  GstGLWindowPrivate *priv;
};

struct _GstGLWindowClass {
  /*< private >*/
  GObjectClass parent_class;

  guintptr (*get_display)        (GstGLWindow *window);
  void     (*set_window_handle)  (GstGLWindow *window, guintptr id);
  guintptr (*get_window_handle)  (GstGLWindow *window);
  void     (*draw_unlocked)      (GstGLWindow *window, guint width, guint height);
  void     (*draw)               (GstGLWindow *window, guint width, guint height);
  void     (*run)                (GstGLWindow *window);
  void     (*quit)               (GstGLWindow *window);
  void     (*send_message)       (GstGLWindow *window, GstGLWindowCB callback, gpointer data);
  void     (*send_message_async) (GstGLWindow *window, GstGLWindowCB callback, gpointer data, GDestroyNotify destroy);

  gboolean (*open)               (GstGLWindow *window, GError **error);
  void     (*close)              (GstGLWindow *window);

  /*< private >*/
  gpointer _reserved[GST_PADDING];
};

/* methods */

GQuark gst_gl_window_error_quark (void);
GType gst_gl_window_get_type     (void);

GstGLWindow * gst_gl_window_new  (GstGLDisplay *display);

void     gst_gl_window_set_draw_callback    (GstGLWindow *window, GstGLWindowCB callback, gpointer data, GDestroyNotify destroy_notify);
void     gst_gl_window_set_resize_callback  (GstGLWindow *window, GstGLWindowResizeCB callback, gpointer data, GDestroyNotify destroy_notify);
void     gst_gl_window_set_close_callback   (GstGLWindow *window, GstGLWindowCB callback, gpointer data, GDestroyNotify destroy_notify);
void     gst_gl_window_set_need_lock        (GstGLWindow *window, gboolean need_lock);

void     gst_gl_window_set_window_handle    (GstGLWindow *window, guintptr handle);
guintptr gst_gl_window_get_window_handle    (GstGLWindow *window);
void     gst_gl_window_draw_unlocked        (GstGLWindow *window, guint width, guint height);
void     gst_gl_window_draw                 (GstGLWindow *window, guint width, guint height);
void     gst_gl_window_run                  (GstGLWindow *window);
void     gst_gl_window_quit                 (GstGLWindow *window);
void     gst_gl_window_send_message         (GstGLWindow *window, GstGLWindowCB callback, gpointer data);
void     gst_gl_window_send_message_async   (GstGLWindow *window, GstGLWindowCB callback, gpointer data, GDestroyNotify destroy);
guintptr gst_gl_window_get_display          (GstGLWindow *window);

GstGLContext * gst_gl_window_get_context (GstGLWindow *window);

gboolean gst_gl_window_is_running (GstGLWindow *window);

GST_DEBUG_CATEGORY_EXTERN (gst_gl_window_debug);

G_END_DECLS

#endif /* __GST_GL_WINDOW_H__ */
