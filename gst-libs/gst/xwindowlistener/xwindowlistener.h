/* G-Streamer X11 Window event/motion listener
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * xwindowlistener.h: object definition
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __X_WINDOW_LISTENER_H__
#define __X_WINDOW_LISTENER_H__

#include <gst/gst.h>
#include <X11/Xlib.h>

G_BEGIN_DECLS
#define GST_TYPE_X_WINDOW_LISTENER \
  (gst_x_window_listener_get_type())
#define GST_X_WINDOW_LISTENER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_X_WINDOW_LISTENER, \
			      GstXWindowListener))
#define GST_X_WINDOW_LISTENER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_X_WINDOW_LISTENER, \
			   GstXWindowListenerClass))
#define GST_IS_X_WINDOW_LISTENER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_X_WINDOW_LISTENER))
#define GST_IS_X_WINDOW_LISTENER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_X_WINDOW_LISTENER))
typedef struct _GstXWindowListener GstXWindowListener;
typedef struct _GstXWindowListenerClass GstXWindowListenerClass;
typedef struct _GstXWindowClip GstXWindowClip;
typedef void (*MapWindowFunc) (gpointer your_data, gboolean visible);
typedef void (*SetWindowFunc) (gpointer your_data,
    gint x, gint y, gint w, gint h, GstXWindowClip * clips, gint num_clips);

struct _GstXWindowClip
{
  gint32 x_offset, y_offset, width, height;
  gpointer data;
};

struct _GstXWindowListener
{
  GObject parent;

  /* "per-instance virtual functions" */
  MapWindowFunc map_window_func;
  SetWindowFunc set_window_func;

  /* private data with which we call the virtual functions */
  gpointer private_data;

  /* general information of what we're doing */
  gchar *display_name;
  XID xwindow_id;

  /* one extra... */
  Display *main_display;
  GMutex *main_lock;

  /* oh my g*d, this is going to be so horribly ugly */
  GThread *thread;
  gboolean cycle;

  /* the overlay window + own thread */
  Display *display;
  Drawable child;
  gboolean ov_conf, ov_map, ov_visible, ov_refresh, ov_move, ov_wmmap;
  gint ov_visibility;
  guint ov_conf_id, ov_refresh_id;
  gint x, y, w, h;
  GstXWindowClip *clips;
  gint num_clips;
};

struct _GstXWindowListenerClass
{
  GObjectClass parent;
};

GType gst_x_window_listener_get_type (void);
GstXWindowListener *gst_x_window_listener_new (gchar * display,
    MapWindowFunc map_window_func,
    SetWindowFunc set_window_func, gpointer private_data);
void gst_x_window_listener_set_xid (GstXWindowListener * xwin, XID id);

G_END_DECLS
#endif /* __X_WINDOW_LISTENER_H__ */
