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

#ifndef __GST_WL_WINDOW_H__
#define __GST_WL_WINDOW_H__

#include "wldisplay.h"

#define GST_TYPE_WL_WINDOW                  (gst_wl_window_get_type ())
#define GST_WL_WINDOW(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_WL_WINDOW, GstWlWindow))
#define GST_IS_WL_WINDOW(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_WL_WINDOW))
#define GST_WL_WINDOW_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_WL_WINDOW, GstWlWindowClass))
#define GST_IS_WL_WINDOW_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_WL_WINDOW))
#define GST_WL_WINDOW_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_WL_WINDOW, GstWlWindowClass))

typedef struct _GstWlWindow GstWlWindow;
typedef struct _GstWlWindowClass GstWlWindowClass;

struct _GstWlWindow
{
  GObject parent_instance;

  GstWlDisplay *display;
  struct wl_surface *surface;
  struct wl_viewport *viewport;
  struct wl_shell_surface *shell_surface;
  gint width, height;
  gboolean own_surface;
};

struct _GstWlWindowClass
{
  GObjectClass parent_class;
};

GType gst_wl_window_get_type (void);

GstWlWindow *gst_wl_window_new_toplevel (GstWlDisplay * display,
        gint width, gint height);
GstWlWindow *gst_wl_window_new_from_surface (GstWlDisplay * display,
        struct wl_surface * surface, gint width, gint height);

GstWlDisplay *gst_wl_window_get_display (GstWlWindow * window);
struct wl_surface *gst_wl_window_get_wl_surface (GstWlWindow * window);
gboolean gst_wl_window_is_toplevel (GstWlWindow *window);

void gst_wl_window_set_size (GstWlWindow * window, gint w, gint h);

#endif /* __GST_WL_WINDOW_H__ */
