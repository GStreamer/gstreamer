/* G-Streamer X11 Window event/motion listener
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * xwindowlistener.c: implementation of the object
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "xwindowlistener.h"

#define NUM_CLIPS 1024

static void gst_x_window_listener_class_init (GstXWindowListenerClass *klass);
static void gst_x_window_listener_init       (GstXWindowListener *xwin);
static void gst_x_window_listener_dispose    (GObject            *object);

static void gst_xwin_start		     (GstXWindowListener *xwin);
static void gst_xwin_stop		     (GstXWindowListener *xwin);

static GObjectClass *parent_class = NULL;

GType
gst_x_window_listener_get_type (void)
{
  static GType x_window_listener_type = 0;

  if (!x_window_listener_type) {
    static const GTypeInfo x_window_listener_info = {
      sizeof (GstXWindowListenerClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_x_window_listener_class_init,
      NULL,
      NULL,
      sizeof (GstXWindowListener),
      0,
      (GInstanceInitFunc) gst_x_window_listener_init,
      NULL
    };

    x_window_listener_type =
	g_type_register_static (G_TYPE_OBJECT,
				"GstXWindowListener",
				&x_window_listener_info, 0);
  }

  return x_window_listener_type;
}

static void
gst_x_window_listener_class_init (GstXWindowListenerClass *klass)
{
  GObjectClass *object_klass = (GObjectClass *) klass;

  parent_class = g_type_class_ref (G_TYPE_OBJECT);

  object_klass->dispose = gst_x_window_listener_dispose;
}

static void
gst_x_window_listener_init (GstXWindowListener *xwin)
{
  xwin->xwindow_id = 0;
  xwin->display_name = NULL;

  xwin->map_window_func = NULL;
  xwin->set_window_func = NULL;

  xwin->thread = NULL;
}

static void
gst_x_window_listener_dispose (GObject *object)
{
  GstXWindowListener *xwin = GST_X_WINDOW_LISTENER (object);

  /* stop overlay */
  gst_x_window_listener_set_xid (xwin, 0);

  if (xwin->display_name) {
    g_free (xwin->display_name);
  }

  if (parent_class->dispose) {
    parent_class->dispose (object);
  }
}

GstXWindowListener *
gst_x_window_listener_new (gchar           *display,
			   MapWindowFunc    map_window_func,
			   SetWindowFunc    set_window_func,
			   gpointer         private_data)
{
  GstXWindowListener *xwin =
	g_object_new (GST_TYPE_X_WINDOW_LISTENER, NULL);

  xwin->display_name = g_strdup (display);
  xwin->map_window_func = map_window_func;
  xwin->set_window_func = set_window_func;
  xwin->private_data = private_data;

  return xwin;
}

void
gst_x_window_listener_set_xid (GstXWindowListener *xwin,
			       XID                 id)
{
  g_return_if_fail (xwin != NULL);

  if (id == xwin->xwindow_id) {
    return;
  }

  if (xwin->xwindow_id && xwin->thread) {
    gst_xwin_stop (xwin);
  }

  xwin->xwindow_id = id;

  if (xwin->xwindow_id &&
      xwin->display_name &&
      xwin->display_name[0] == ':') {
    g_return_if_fail (xwin->map_window_func != NULL);
    g_return_if_fail (xwin->set_window_func != NULL);

    gst_xwin_start (xwin);
  }
}

/*
 * The following code works as follows:
 *  - the "client" (the one who uses this object) sets an XID
 *  - we add a child XWindow to this XID, and follow motion/events
 *  - after each event, we determine the position, size and clips
 *  - next, we call the per-instance virtual functions set by the client
 *  - and we do all this in an endless cycle
 *
 * This code originates largely from xawtv. By permission of Gerd Knorr
 * <kraxel@bytesex.org>, it was relicensed to LGPL.
 */

#define DEBUG(format, args...) \
  GST_DEBUG ("XWL: " format, ##args)

static void
gst_xwin_set_overlay (GstXWindowListener *xwin,
		      gboolean       on)
{
  xwin->map_window_func (xwin->private_data, on);

  /* remember me */
  xwin->ov_visible = on;
}

static gboolean
gst_xwin_refresh (gpointer data)
{
  GstXWindowListener *xwin = GST_X_WINDOW_LISTENER (data);
  Window win, tmp;
  XSetWindowAttributes xswa;
  XWindowAttributes attr;

  g_mutex_lock (xwin->main_lock);

  win = DefaultRootWindow (xwin->main_display);
  XGetWindowAttributes (xwin->main_display, win, &attr);

  xwin->ov_refresh_id = 0;

  if (!xwin->ov_move && xwin->ov_map &&
      xwin->ov_visibility == VisibilityUnobscured) {
    g_mutex_unlock (xwin->main_lock);
    return FALSE; /* skip */
  }

  if (xwin->ov_map &&
      xwin->ov_visibility != VisibilityFullyObscured) {
    xwin->ov_refresh = TRUE;
  }

  xswa.override_redirect = True;
  xswa.backing_store = NotUseful;
  xswa.save_under = False;
  tmp = XCreateWindow (xwin->main_display,win, 0, 0,
		       attr.width, attr.height, 0,
		       CopyFromParent, InputOutput, CopyFromParent,
		       (CWSaveUnder | CWBackingStore| CWOverrideRedirect ),
		       &xswa);
  XMapWindow (xwin->main_display, tmp);
  XUnmapWindow (xwin->main_display, tmp);
  XDestroyWindow (xwin->main_display, tmp);
  xwin->ov_move = FALSE;

  g_mutex_unlock (xwin->main_lock);

  /* once */
  return FALSE;
}

static int
x11_error_dev_null (Display     *display,
		    XErrorEvent *event)
{
    return 0;
}

#define ADD_CLIP(_x, _y, _w, _h) \
  do { \
    GstXWindowClip *clip = &xwin->clips[xwin->num_clips++]; \
    clip->x_offset = _x; \
    clip->y_offset = _y; \
    clip->width = _w; \
    clip->height = _h; \
    clip->data = NULL; \
  } while (0);

static void
gst_xwin_set_clips (GstXWindowListener *xwin)
{
  Window root, rroot, parent, *kids, me;
  XWindowAttributes attr;
  guint numkids;
  gint i;
  gint x1, y1, w1, h1;
  void *old_handler;

  old_handler = XSetErrorHandler (x11_error_dev_null);

  if (xwin->num_clips != 0)
    xwin->ov_conf = TRUE;
  xwin->num_clips = 0;

  root = DefaultRootWindow (xwin->display);
  XGetWindowAttributes (xwin->display, root, &attr);

  if (xwin->x < 0)
    ADD_CLIP (0, 0, -xwin->x, xwin->h);
  if (xwin->y < 0)
    ADD_CLIP (0, 0, xwin->w, -xwin->y);
  if ((xwin->x + xwin->w) > attr.width)
    ADD_CLIP (attr.width - xwin->x, 0, xwin->w, xwin->h);
  if ((xwin->y + xwin->h) > attr.height)
    ADD_CLIP (0, attr.height - xwin->y, xwin->w, xwin->h);

  me = xwin->child;
  while (1) {
    XQueryTree (xwin->display, me, &rroot, &parent, &kids, &numkids);
    if (numkids)
      XFree (kids);
    if (root == parent)
      break;
    me = parent;
  }

  XQueryTree (xwin->display, root, &rroot, &parent, &kids, &numkids);
  for (i = 0; i < numkids; i++)
    if (kids[i] == me)
      break;

  for (i++; i < numkids; i++) {
    XGetWindowAttributes (xwin->display, kids[i], &attr);
    if (attr.map_state != IsViewable)
      continue;

    x1 = attr.x - xwin->x;
    y1 = attr.y - xwin->y;
    w1 = attr.width + 2 * attr.border_width;
    h1 = attr.height + 2 * attr.border_width;
    if (((x1 + w1) < 0) || (x1 > xwin->w) ||
        ((y1 + h1) < 0) || (y1 > xwin->h))
      continue;
	
    if (x1 < 0)
      x1 = 0;
    if (y1 < 0)
      y1 = 0;
    ADD_CLIP (x1, y1, w1, h1);
  }
  XFree (kids);

  if (xwin->num_clips != 0)
    xwin->ov_conf = TRUE;

  XSetErrorHandler (old_handler);
}


static gboolean
gst_xwin_window (GstXWindowListener *xwin)
{
  if (xwin->ov_map && xwin->ov_wmmap &&
      xwin->ov_visibility != VisibilityFullyObscured) {
    /* visible */
    if (xwin->ov_visibility == VisibilityPartiallyObscured) {
      /* set clips */
      gst_xwin_set_clips (xwin);
    }

    if (xwin->ov_conf) {
      xwin->set_window_func (xwin->private_data,
			     xwin->x, xwin->y,
			     xwin->w, xwin->h,
			     xwin->clips, xwin->num_clips);

      if (!xwin->ov_visible)
        gst_xwin_set_overlay (xwin, TRUE);

      g_mutex_lock (xwin->main_lock);

      if (xwin->ov_refresh_id)
        g_source_remove (xwin->ov_refresh_id);
      xwin->ov_refresh_id =
	  g_timeout_add (200, (GSourceFunc) gst_xwin_refresh,
			 (gpointer) xwin);

      xwin->ov_conf = FALSE;

      g_mutex_unlock (xwin->main_lock);
    }
  } else {
    /* not visible */
    if (xwin->ov_conf && xwin->ov_visible) {
      gst_xwin_set_overlay (xwin, FALSE);

      g_mutex_lock (xwin->main_lock);

      if (xwin->ov_refresh_id)
        g_source_remove (xwin->ov_refresh_id);
      xwin->ov_refresh_id =
	  g_timeout_add (200, (GSourceFunc) gst_xwin_refresh,
			 (gpointer) xwin);

      xwin->ov_conf = FALSE;

      g_mutex_unlock (xwin->main_lock);
    }
  }

  xwin->ov_conf_id = 0;

  /* once is enough */
  return FALSE;
}

static void
gst_xwin_configure (GstXWindowListener *xwin)
{
#if 0
  /* This part is disabled, because the idle task will be done
   * in the main thread instead of here. */
  if (!xwin->ov_conf_id)
    xwin->ov_conf_id =
	g_idle_add ((GSourceFunc) gst_rec_xoverlay_window,
		    (gpointer) xwin);
#endif

  gst_xwin_window ((gpointer) xwin);
}

static void
gst_xwin_resize (GstXWindowListener *xwin)
{
  Drawable drawable, parent, *kids, root;
  guint numkids;
  XWindowAttributes attr;

  XGetWindowAttributes (xwin->display,
			xwin->xwindow_id, &attr);
  XMoveResizeWindow (xwin->display, xwin->child,
		     0, 0, attr.width, attr.height);

  /* set the video window - the first clip is our own window */
  xwin->x = 0;
  xwin->y = 0;
  xwin->w = attr.width;
  xwin->h = attr.height;

  drawable = xwin->child;
  while (1) {
    XQueryTree (xwin->display, drawable,
		&root, &parent, &kids, &numkids);
    if (numkids)
      XFree(kids);
    drawable = parent;
    XGetWindowAttributes (xwin->display, drawable, &attr);
    xwin->x += attr.x;
    xwin->y += attr.y;
    if (parent == attr.root)
      break;
  }

  xwin->ov_conf = TRUE;
  xwin->ov_move = TRUE;

  gst_xwin_configure (xwin);
}

static void
gst_xwin_init_window (GstXWindowListener *xwin)
{
  XWindowAttributes attr;

  /* start values */
  xwin->ov_conf = TRUE;
  xwin->ov_map = xwin->ov_wmmap = TRUE;
  xwin->ov_move = TRUE;
  xwin->ov_refresh = FALSE;
  g_mutex_lock (xwin->main_lock);
  xwin->ov_conf_id = xwin->ov_refresh_id = 0;
  g_mutex_unlock (xwin->main_lock);
  xwin->ov_visibility = VisibilityFullyObscured;

  /* start the memory that we'll use */
  xwin->clips = g_malloc (sizeof (GstXWindowClip) * NUM_CLIPS);
  xwin->num_clips = 0;

  /* open connection to X server */
  xwin->display = XOpenDisplay (xwin->display_name);

  /* window */
  XGetWindowAttributes (xwin->display,
			xwin->xwindow_id, &attr);
  xwin->child = XCreateSimpleWindow (xwin->display,
				     xwin->xwindow_id, 0, 0,
				     attr.width, attr.height, 0, 0, 0);

  /* listen to certain X events */
  XSelectInput (xwin->display, xwin->xwindow_id,
		StructureNotifyMask);
  XSelectInput (xwin->display, xwin->child,
		VisibilityChangeMask | StructureNotifyMask);
  XSelectInput (xwin->display, DefaultRootWindow (xwin->display),
		VisibilityChangeMask | StructureNotifyMask |
		SubstructureNotifyMask);

  /* show */
  XMapWindow (xwin->display, xwin->child);

  gst_xwin_resize (xwin);
}

static void
gst_xwin_exit_window (GstXWindowListener *xwin)
{
  /* disable overlay */
  gst_xwin_set_overlay (xwin, FALSE);

  /* delete idle funcs */
  if (xwin->ov_conf_id != 0)
    g_source_remove (xwin->ov_conf_id);

  g_mutex_lock (xwin->main_lock);
  if (xwin->ov_refresh_id != 0)
    g_source_remove (xwin->ov_refresh_id);
  g_mutex_unlock (xwin->main_lock);

  /* get away from X and free mem */
  XDestroyWindow (xwin->display, xwin->child);
  XCloseDisplay (xwin->display);
  g_free (xwin->clips);
}

static gpointer
gst_xwin_thread (gpointer data)
{
  GstXWindowListener *xwin = GST_X_WINDOW_LISTENER (data);
  XEvent event;

  /* Hi, I'm GStreamer. What's your name? */
  gst_xwin_init_window (xwin);

  while (xwin->cycle) {
    XNextEvent (xwin->display, &event);

    if (!xwin->cycle)
      break;

    if ((event.type == ConfigureNotify &&
         event.xconfigure.window == xwin->xwindow_id) ||
        (event.type == MapNotify &&
         event.xmap.window == xwin->xwindow_id) ||
        (event.type == UnmapNotify &&
         event.xunmap.window == xwin->xwindow_id)) {
      /* the 'parent' window, i.e. the widget provided by client */
      switch (event.type) {
        case MapNotify:
          xwin->ov_map = TRUE;
          xwin->ov_conf = TRUE;
          gst_xwin_configure (xwin);
          break;

        case UnmapNotify:
          xwin->ov_map = FALSE;
          xwin->ov_conf = TRUE;
          gst_xwin_configure (xwin);
          break;

        case ConfigureNotify:
          gst_xwin_resize (xwin);
          break;

        default:
          /* nothing */
          break;
      }
    } else if (event.xany.window == xwin->child) {
      /* our own private window */
      switch (event.type) {
        case Expose:
          if (!event.xexpose.count) {
            if (xwin->ov_refresh) {
              xwin->ov_refresh = FALSE;
            } else {
              xwin->ov_conf = TRUE;
              gst_xwin_configure (xwin);
            }
          }
          break;

        case VisibilityNotify:
          xwin->ov_visibility = event.xvisibility.state;
          if (xwin->ov_refresh) {
            if (event.xvisibility.state != VisibilityFullyObscured)
              xwin->ov_refresh = FALSE;
          } else {
            xwin->ov_conf = TRUE;
            gst_xwin_configure (xwin);
          }
          break;

        default:
          /* nothing */
          break;
      }
    } else {
      /* root window */
      switch (event.type) {
        case MapNotify:
        case UnmapNotify:
          /* are we still visible? */
          if (!xwin->ov_refresh) {
            XWindowAttributes attr;
            gboolean on;
            XGetWindowAttributes (xwin->display,
				  xwin->xwindow_id, &attr);
            on = (attr.map_state == IsViewable);
            xwin->ov_wmmap = on;
            xwin->ov_conf = TRUE;
            gst_xwin_configure (xwin);
          }
          break;

        case ConfigureNotify:
          if (!xwin->ov_refresh) {
            gst_xwin_resize (xwin);
          }
          break;

        default:
          /* nothing */
          break;
      }
    }
  }

  /* Nice to have met you, see you later */
  gst_xwin_exit_window (xwin);

  g_thread_exit (NULL);

  return NULL;
}

static void
gst_xwin_start (GstXWindowListener *xwin)
{
  DEBUG ("Starting XWindow listener");

  xwin->cycle = TRUE;
  /* we use this main_display for two things: first of all,
   * the window needs to be 'refreshed' to remove artifacts
   * after every move. Secondly, we use this to 'unhang' the
   * event handler after we've stopped it */
  xwin->main_lock = g_mutex_new ();
  xwin->main_display = XOpenDisplay (xwin->display_name);
  xwin->thread = g_thread_create (gst_xwin_thread,
					(gpointer) xwin,
					TRUE, NULL);

  DEBUG ("Started X-overlay");
}

static void
gst_xwin_stop (GstXWindowListener *xwin)
{
  DEBUG ("Stopping XWindow listener");

  xwin->cycle = FALSE;
  /* now, the event loop will hang. To prevent this from hanging
   * our app, app, we re-do our refresh hack. Oh man, this is
   * ugly. But it works. :). */
  g_mutex_lock (xwin->main_lock);
  if (xwin->ov_refresh_id)
    g_source_remove (xwin->ov_refresh_id);
  g_mutex_unlock (xwin->main_lock);

  gst_xwin_refresh ((gpointer) xwin);
  g_thread_join (xwin->thread);
  XCloseDisplay (xwin->main_display);
  g_mutex_free (xwin->main_lock);

  DEBUG ("Stopped X-overlay");
}

/*
 * End of code inspired by XawTV.
 */

static gboolean
plugin_init (GstPlugin *plugin)
{
  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "xwindowlistener",
  "X11-based XWindow event/motion listener",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
