/* GStreamer
 *
 * gstv4lxoverlay.c: X-based overlay interface implementation for V4L
 *
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include <string.h>
#include <sys/stat.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>

#include "gstv4lxoverlay.h"
#include "gstv4lelement.h"
#include "v4l_calls.h"

GST_DEBUG_CATEGORY_STATIC (v4lxv_debug);
#define GST_CAT_DEFAULT v4lxv_debug

struct _GstV4lXv
{
  Display *dpy;
  gint port, idle_id;
  GMutex *mutex;
};

static void gst_v4l_xoverlay_set_window_handle (GstXOverlay * overlay,
    guintptr xwindow_id);

void
gst_v4l_xoverlay_interface_init (GstXOverlayClass * klass)
{
  /* default virtual functions */
  klass->set_window_handle = gst_v4l_xoverlay_set_window_handle;

  GST_DEBUG_CATEGORY_INIT (v4lxv_debug, "v4lxv", 0,
      "V4L XOverlay interface debugging");
}

static void
gst_v4l_xoverlay_open (GstV4lElement * v4lelement)
{
  struct stat s;
  GstV4lXv *v4lxv;
  const gchar *name = g_getenv ("DISPLAY");
  unsigned int ver, rel, req, ev, err, anum;
  int i, id = 0, first_id = 0, min;
  XvAdaptorInfo *ai;
  Display *dpy;

  /* we need a display, obviously */
  if (!name || !(dpy = XOpenDisplay (name))) {
    GST_WARNING ("No $DISPLAY set or failed to open - no overlay");
    return;
  }

  /* First let's check that XVideo extension is available */
  if (!XQueryExtension (dpy, "XVideo", &i, &i, &i)) {
    GST_WARNING ("Xv extension not available - no overlay");
    XCloseDisplay (dpy);
    return;
  }

  /* find port that belongs to this device */
  if (XvQueryExtension (dpy, &ver, &rel, &req, &ev, &err) != Success) {
    GST_WARNING ("Xv extension not supported - no overlay");
    XCloseDisplay (dpy);
    return;
  }
  if (XvQueryAdaptors (dpy, DefaultRootWindow (dpy), &anum, &ai) != Success) {
    GST_WARNING ("Failed to query Xv adaptors");
    XCloseDisplay (dpy);
    return;
  }
  if (fstat (v4lelement->video_fd, &s) < 0) {
    GST_ERROR ("Failed to stat() file descriptor: %s", g_strerror (errno));
    XCloseDisplay (dpy);
    return;
  }
  min = s.st_rdev & 0xff;
  for (i = 0; i < anum; i++) {
    if (!strcmp (ai[i].name, "video4linux")) {
      if (first_id == 0)
        first_id = ai[i].base_id;

      /* hmm... */
      if (first_id != 0 && ai[i].base_id == first_id + min)
        id = ai[i].base_id;
    }
  }
  XvFreeAdaptorInfo (ai);

  if (id == 0) {
    GST_WARNING ("Did not find XvPortID for device - no overlay");
    XCloseDisplay (dpy);
    return;
  }

  v4lxv = g_new0 (GstV4lXv, 1);
  v4lxv->dpy = dpy;
  v4lxv->port = id;
  v4lxv->mutex = g_mutex_new ();
  v4lxv->idle_id = 0;
  v4lelement->xv = v4lxv;

  if (v4lelement->xwindow_id) {
    gst_v4l_xoverlay_set_window_handle (GST_X_OVERLAY (v4lelement),
        v4lelement->xwindow_id);
  }
}

static void
gst_v4l_xoverlay_close (GstV4lElement * v4lelement)
{
  GstV4lXv *v4lxv = v4lelement->xv;

  if (!v4lelement->xv)
    return;

  if (v4lelement->xwindow_id) {
    gst_v4l_xoverlay_set_window_handle (GST_X_OVERLAY (v4lelement), 0);
  }

  XCloseDisplay (v4lxv->dpy);
  g_mutex_free (v4lxv->mutex);
  if (v4lxv->idle_id)
    g_source_remove (v4lxv->idle_id);
  g_free (v4lxv);
  v4lelement->xv = NULL;
}

void
gst_v4l_xoverlay_start (GstV4lElement * v4lelement)
{
  if (v4lelement->xwindow_id) {
    gst_v4l_xoverlay_open (v4lelement);
  }
}

void
gst_v4l_xoverlay_stop (GstV4lElement * v4lelement)
{
  gst_v4l_xoverlay_close (v4lelement);
}

static gboolean
idle_refresh (gpointer data)
{
  GstV4lElement *v4lelement = GST_V4LELEMENT (data);
  GstV4lXv *v4lxv = v4lelement->xv;
  XWindowAttributes attr;

  if (v4lxv) {
    g_mutex_lock (v4lxv->mutex);

    XGetWindowAttributes (v4lxv->dpy, v4lelement->xwindow_id, &attr);
    XvPutVideo (v4lxv->dpy, v4lxv->port, v4lelement->xwindow_id,
        DefaultGC (v4lxv->dpy, DefaultScreen (v4lxv->dpy)),
        0, 0, attr.width, attr.height, 0, 0, attr.width, attr.height);

    v4lxv->idle_id = 0;
    g_mutex_unlock (v4lxv->mutex);
  }

  /* once */
  return FALSE;
}

static void
gst_v4l_xoverlay_set_window_handle (GstXOverlay * overlay, guintptr id)
{
  XID xwindow_id = id;
  GstV4lElement *v4lelement = GST_V4LELEMENT (overlay);
  GstV4lXv *v4lxv;
  XWindowAttributes attr;
  gboolean change = (v4lelement->xwindow_id != xwindow_id);

  GST_LOG_OBJECT (v4lelement, "Changing port to %lx", xwindow_id);

  if (!v4lelement->xv && GST_V4L_IS_OPEN (v4lelement))
    gst_v4l_xoverlay_open (v4lelement);

  v4lxv = v4lelement->xv;

  if (v4lxv)
    g_mutex_lock (v4lxv->mutex);

  if (change) {
    if (v4lelement->xwindow_id && v4lxv) {
      GST_DEBUG_OBJECT (v4lelement,
          "Disabling port %lx", v4lelement->xwindow_id);

      XvSelectPortNotify (v4lxv->dpy, v4lxv->port, 0);
      XvSelectVideoNotify (v4lxv->dpy, v4lelement->xwindow_id, 0);
      XvStopVideo (v4lxv->dpy, v4lxv->port, v4lelement->xwindow_id);
    }

    v4lelement->xwindow_id = xwindow_id;
  }

  if (!v4lxv || xwindow_id == 0) {
    if (v4lxv)
      g_mutex_unlock (v4lxv->mutex);
    return;
  }

  if (change) {
    GST_DEBUG_OBJECT (v4lelement, "Enabling port %lx", xwindow_id);

    /* draw */
    XvSelectPortNotify (v4lxv->dpy, v4lxv->port, 1);
    XvSelectVideoNotify (v4lxv->dpy, v4lelement->xwindow_id, 1);
  }

  XGetWindowAttributes (v4lxv->dpy, v4lelement->xwindow_id, &attr);
  XvPutVideo (v4lxv->dpy, v4lxv->port, v4lelement->xwindow_id,
      DefaultGC (v4lxv->dpy, DefaultScreen (v4lxv->dpy)),
      0, 0, attr.width, attr.height, 0, 0, attr.width, attr.height);

  if (v4lxv->idle_id)
    g_source_remove (v4lxv->idle_id);
  v4lxv->idle_id = g_idle_add (idle_refresh, v4lelement);
  g_mutex_unlock (v4lxv->mutex);
}
