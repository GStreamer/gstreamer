/* GStreamer X-based overlay interface implementation
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstv4l2xoverlay.c: X-based overlay interface implementation for V4L2
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
#include <gst/gst.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>
#include <sys/stat.h>

#include "gstv4l2xoverlay.h"
#include "gstv4l2element.h"
#include "v4l2_calls.h"

GST_DEBUG_CATEGORY_STATIC (v4l2xv_debug);
#define GST_CAT_DEFAULT v4l2xv_debug

struct _GstV4l2Xv
{
  Display *dpy;
  gint port, idle_id;
  GMutex *mutex;
};

static void gst_v4l2_xoverlay_set_xwindow_id (GstXOverlay * overlay,
    XID xwindow_id);

void
gst_v4l2_xoverlay_interface_init (GstXOverlayClass * klass)
{
  /* default virtual functions */
  klass->set_xwindow_id = gst_v4l2_xoverlay_set_xwindow_id;

  GST_DEBUG_CATEGORY_INIT (v4l2xv_debug, "v4l2xv", 0,
      "V4L2 XOverlay interface debugging");
}

static void
gst_v4l2_xoverlay_open (GstV4l2Element * v4l2element)
{
  struct stat s;
  GstV4l2Xv *v4l2xv;
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
  if (fstat (v4l2element->video_fd, &s) < 0) {
    GST_ERROR ("Failed to stat() file descriptor: %s", g_strerror (errno));
    XCloseDisplay (dpy);
    return;
  }
  min = s.st_rdev & 0xff;
  for (i = 0; i < anum; i++) {
    if (!strcmp (ai[i].name, "video4linux2")) {
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

  v4l2xv = g_new0 (GstV4l2Xv, 1);
  v4l2xv->dpy = dpy;
  v4l2xv->port = id;
  v4l2xv->mutex = g_mutex_new ();
  v4l2xv->idle_id = 0;
  v4l2element->xv = v4l2xv;

  if (v4l2element->xwindow_id) {
    gst_v4l2_xoverlay_set_xwindow_id (GST_X_OVERLAY (v4l2element),
        v4l2element->xwindow_id);
  }
}

static void
gst_v4l2_xoverlay_close (GstV4l2Element * v4l2element)
{
  GstV4l2Xv *v4l2xv = v4l2element->xv;

  if (!v4l2element->xv)
    return;

  if (v4l2element->xwindow_id) {
    gst_v4l2_xoverlay_set_xwindow_id (GST_X_OVERLAY (v4l2element), 0);
  }

  XCloseDisplay (v4l2xv->dpy);
  g_mutex_free (v4l2xv->mutex);
  if (v4l2xv->idle_id)
    g_source_remove (v4l2xv->idle_id);
  g_free (v4l2xv);
  v4l2element->xv = NULL;
}

void
gst_v4l2_xoverlay_start (GstV4l2Element * v4l2element)
{
  if (v4l2element->xwindow_id) {
    gst_v4l2_xoverlay_open (v4l2element);
  }
}

void
gst_v4l2_xoverlay_stop (GstV4l2Element * v4l2element)
{
  gst_v4l2_xoverlay_close (v4l2element);
}

static gboolean
idle_refresh (gpointer data)
{
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT (data);
  GstV4l2Xv *v4l2xv = v4l2element->xv;
  XWindowAttributes attr;

  if (v4l2xv) {
    g_mutex_lock (v4l2xv->mutex);

    XGetWindowAttributes (v4l2xv->dpy, v4l2element->xwindow_id, &attr);
    XvPutVideo (v4l2xv->dpy, v4l2xv->port, v4l2element->xwindow_id,
        DefaultGC (v4l2xv->dpy, DefaultScreen (v4l2xv->dpy)),
        0, 0, attr.width, attr.height, 0, 0, attr.width, attr.height);

    v4l2xv->idle_id = 0;
    g_mutex_unlock (v4l2xv->mutex);
  }

  /* once */
  return FALSE;
}

static void
gst_v4l2_xoverlay_set_xwindow_id (GstXOverlay * overlay, XID xwindow_id)
{
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT (overlay);
  GstV4l2Xv *v4l2xv;
  XWindowAttributes attr;
  gboolean change = (v4l2element->xwindow_id != xwindow_id);

  GST_LOG_OBJECT (v4l2element, "Setting XID to %lx", (gulong) xwindow_id);

  if (!v4l2element->xv && GST_V4L2_IS_OPEN (v4l2element))
    gst_v4l2_xoverlay_open (v4l2element);

  v4l2xv = v4l2element->xv;

  if (v4l2xv)
    g_mutex_lock (v4l2xv->mutex);

  if (change) {
    if (v4l2element->xwindow_id && v4l2xv) {
      GST_DEBUG_OBJECT (v4l2element,
          "Deactivating old port %lx", v4l2element->xwindow_id);

      XvSelectPortNotify (v4l2xv->dpy, v4l2xv->port, 0);
      XvSelectVideoNotify (v4l2xv->dpy, v4l2element->xwindow_id, 0);
      XvStopVideo (v4l2xv->dpy, v4l2xv->port, v4l2element->xwindow_id);
    }

    v4l2element->xwindow_id = xwindow_id;
  }

  if (!v4l2xv || xwindow_id == 0) {
    if (v4l2xv)
      g_mutex_unlock (v4l2xv->mutex);
    return;
  }

  if (change) {
    GST_DEBUG_OBJECT (v4l2element, "Activating new port %lx", xwindow_id);

    /* draw */
    XvSelectPortNotify (v4l2xv->dpy, v4l2xv->port, 1);
    XvSelectVideoNotify (v4l2xv->dpy, v4l2element->xwindow_id, 1);
  }

  XGetWindowAttributes (v4l2xv->dpy, v4l2element->xwindow_id, &attr);
  XvPutVideo (v4l2xv->dpy, v4l2xv->port, v4l2element->xwindow_id,
      DefaultGC (v4l2xv->dpy, DefaultScreen (v4l2xv->dpy)),
      0, 0, attr.width, attr.height, 0, 0, attr.width, attr.height);

  if (v4l2xv->idle_id)
    g_source_remove (v4l2xv->idle_id);
  v4l2xv->idle_id = g_idle_add (idle_refresh, v4l2element);
  g_mutex_unlock (v4l2xv->mutex);
}
