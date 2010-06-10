/* GStreamer
 *
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *               2006 Edgard Lima <edgard.lima@indt.org.br>
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
#include "gstv4l2object.h"
#include "v4l2_calls.h"

#include "gst/gst-i18n-plugin.h"

struct _GstV4l2Xv
{
  Display *dpy;
  gint port, idle_id, event_id;
  GMutex *mutex;
};

GST_DEBUG_CATEGORY_STATIC (v4l2xv_debug);
#define GST_CAT_DEFAULT v4l2xv_debug

void
gst_v4l2_xoverlay_interface_init (GstXOverlayClass * klass)
{
  GST_DEBUG_CATEGORY_INIT (v4l2xv_debug, "v4l2xv", 0,
      "V4L2 XOverlay interface debugging");
}

static void
gst_v4l2_xoverlay_open (GstV4l2Object * v4l2object)
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
    GST_WARNING_OBJECT (v4l2object->element,
        "No $DISPLAY set or failed to open - no overlay");
    return;
  }

  /* First let's check that XVideo extension is available */
  if (!XQueryExtension (dpy, "XVideo", &i, &i, &i)) {
    GST_WARNING_OBJECT (v4l2object->element,
        "Xv extension not available - no overlay");
    XCloseDisplay (dpy);
    return;
  }

  /* find port that belongs to this device */
  if (XvQueryExtension (dpy, &ver, &rel, &req, &ev, &err) != Success) {
    GST_WARNING_OBJECT (v4l2object->element,
        "Xv extension not supported - no overlay");
    XCloseDisplay (dpy);
    return;
  }
  if (XvQueryAdaptors (dpy, DefaultRootWindow (dpy), &anum, &ai) != Success) {
    GST_WARNING_OBJECT (v4l2object->element, "Failed to query Xv adaptors");
    XCloseDisplay (dpy);
    return;
  }
  if (fstat (v4l2object->video_fd, &s) < 0) {
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, NOT_FOUND,
        (_("Cannot identify device '%s'."), v4l2object->videodev),
        GST_ERROR_SYSTEM);
    XCloseDisplay (dpy);
    return;
  }
  min = s.st_rdev & 0xff;
  for (i = 0; i < anum; i++) {
    GST_DEBUG_OBJECT (v4l2object->element, "found adapter: %s", ai[i].name);
    if (!strcmp (ai[i].name, "video4linux2") ||
        !strcmp (ai[i].name, "video4linux")) {
      if (first_id == 0)
        first_id = ai[i].base_id;

      GST_DEBUG_OBJECT (v4l2object->element,
          "first_id=%d, base_id=%lu, min=%d", first_id, ai[i].base_id, min);

      /* hmm... */
      if (first_id != 0 && ai[i].base_id == first_id + min)
        id = ai[i].base_id;
    }
  }
  XvFreeAdaptorInfo (ai);

  if (id == 0) {
    GST_WARNING_OBJECT (v4l2object->element,
        "Did not find XvPortID for device - no overlay");
    XCloseDisplay (dpy);
    return;
  }

  v4l2xv = g_new0 (GstV4l2Xv, 1);
  v4l2xv->dpy = dpy;
  v4l2xv->port = id;
  v4l2xv->mutex = g_mutex_new ();
  v4l2xv->idle_id = 0;
  v4l2xv->event_id = 0;
  v4l2object->xv = v4l2xv;

  if (v4l2object->xwindow_id) {
    gst_v4l2_xoverlay_set_window_handle (v4l2object, v4l2object->xwindow_id);
  }
}

static void
gst_v4l2_xoverlay_close (GstV4l2Object * v4l2object)
{
  GstV4l2Xv *v4l2xv = v4l2object->xv;

  if (!v4l2object->xv)
    return;

  if (v4l2object->xwindow_id) {
    gst_v4l2_xoverlay_set_window_handle (v4l2object, 0);
  }

  XCloseDisplay (v4l2xv->dpy);
  g_mutex_free (v4l2xv->mutex);
  if (v4l2xv->idle_id)
    g_source_remove (v4l2xv->idle_id);
  if (v4l2xv->event_id)
    g_source_remove (v4l2xv->event_id);
  g_free (v4l2xv);
  v4l2object->xv = NULL;
}

void
gst_v4l2_xoverlay_start (GstV4l2Object * v4l2object)
{
  if (v4l2object->xwindow_id) {
    gst_v4l2_xoverlay_open (v4l2object);
  }
}

void
gst_v4l2_xoverlay_stop (GstV4l2Object * v4l2object)
{
  gst_v4l2_xoverlay_close (v4l2object);
}

static void
update_geometry (GstV4l2Object * v4l2object)
{
  GstV4l2Xv *v4l2xv = v4l2object->xv;
  XWindowAttributes attr;
  XGetWindowAttributes (v4l2xv->dpy, v4l2object->xwindow_id, &attr);
  XvPutVideo (v4l2xv->dpy, v4l2xv->port, v4l2object->xwindow_id,
      DefaultGC (v4l2xv->dpy, DefaultScreen (v4l2xv->dpy)),
      0, 0, attr.width, attr.height, 0, 0, attr.width, attr.height);
}

static gboolean
idle_refresh (gpointer data)
{
  GstV4l2Object *v4l2object = GST_V4L2_OBJECT (data);
  GstV4l2Xv *v4l2xv = v4l2object->xv;

  GST_LOG_OBJECT (v4l2object->element, "idle refresh");

  if (v4l2xv) {
    g_mutex_lock (v4l2xv->mutex);

    update_geometry (v4l2object);

    v4l2xv->idle_id = 0;
    g_mutex_unlock (v4l2xv->mutex);
  }

  /* once */
  return FALSE;
}


static gboolean
event_refresh (gpointer data)
{
  GstV4l2Object *v4l2object = GST_V4L2_OBJECT (data);
  GstV4l2Xv *v4l2xv = v4l2object->xv;

  GST_LOG_OBJECT (v4l2object->element, "event refresh");

  if (v4l2xv) {
    XEvent e;

    g_mutex_lock (v4l2xv->mutex);

    /* Handle ConfigureNotify */
    while (XCheckWindowEvent (v4l2xv->dpy, v4l2object->xwindow_id,
            StructureNotifyMask, &e)) {
      switch (e.type) {
        case ConfigureNotify:
          update_geometry (v4l2object);
          break;
        default:
          break;
      }
    }
    g_mutex_unlock (v4l2xv->mutex);
  }

  /* repeat */
  return TRUE;
}

void
gst_v4l2_xoverlay_set_window_handle (GstV4l2Object * v4l2object, guintptr id)
{
  GstV4l2Xv *v4l2xv;
  XID xwindow_id = id;
  gboolean change = (v4l2object->xwindow_id != xwindow_id);

  GST_LOG_OBJECT (v4l2object->element, "Setting XID to %lx",
      (gulong) xwindow_id);

  if (!v4l2object->xv && GST_V4L2_IS_OPEN (v4l2object))
    gst_v4l2_xoverlay_open (v4l2object);

  v4l2xv = v4l2object->xv;

  if (v4l2xv)
    g_mutex_lock (v4l2xv->mutex);

  if (change) {
    if (v4l2object->xwindow_id && v4l2xv) {
      GST_DEBUG_OBJECT (v4l2object->element,
          "Deactivating old port %lx", v4l2object->xwindow_id);

      XvSelectPortNotify (v4l2xv->dpy, v4l2xv->port, 0);
      XvSelectVideoNotify (v4l2xv->dpy, v4l2object->xwindow_id, 0);
      XvStopVideo (v4l2xv->dpy, v4l2xv->port, v4l2object->xwindow_id);
    }

    v4l2object->xwindow_id = xwindow_id;
  }

  if (!v4l2xv || xwindow_id == 0) {
    if (v4l2xv)
      g_mutex_unlock (v4l2xv->mutex);
    return;
  }

  if (change) {
    GST_DEBUG_OBJECT (v4l2object->element, "Activating new port %lx",
        xwindow_id);

    /* draw */
    XvSelectPortNotify (v4l2xv->dpy, v4l2xv->port, 1);
    XvSelectVideoNotify (v4l2xv->dpy, v4l2object->xwindow_id, 1);
  }

  update_geometry (v4l2object);

  if (v4l2xv->idle_id)
    g_source_remove (v4l2xv->idle_id);
  v4l2xv->idle_id = g_idle_add (idle_refresh, v4l2object);
  g_mutex_unlock (v4l2xv->mutex);
}

/**
 * gst_v4l2_xoverlay_prepare_xwindow_id:
 * @param v4l2object
 * @param required TRUE if display is required (ie. TRUE for v4l2sink, but
 *   FALSE for any other element with optional overlay capabilities)
 */
void
gst_v4l2_xoverlay_prepare_xwindow_id (GstV4l2Object * v4l2object,
    gboolean required)
{
  if (!GST_V4L2_IS_OVERLAY (v4l2object))
    return;

  gst_x_overlay_prepare_xwindow_id (GST_X_OVERLAY (v4l2object->element));

  if (required && !v4l2object->xwindow_id) {
    GstV4l2Xv *v4l2xv;
    Window win;
    int width, height;

    if (!v4l2object->xv && GST_V4L2_IS_OPEN (v4l2object))
      gst_v4l2_xoverlay_open (v4l2object);

    v4l2xv = v4l2object->xv;

    /* if xoverlay is not supported, just bail */
    if (!v4l2xv)
      return;

    /* xoverlay is supported, but we don't have a window.. so create one */
    GST_DEBUG_OBJECT (v4l2object->element, "creating window");

    g_mutex_lock (v4l2xv->mutex);

    width = XDisplayWidth (v4l2xv->dpy, DefaultScreen (v4l2xv->dpy));
    height = XDisplayHeight (v4l2xv->dpy, DefaultScreen (v4l2xv->dpy));
    GST_DEBUG_OBJECT (v4l2object->element, "dpy=%p", v4l2xv->dpy);

    win = XCreateSimpleWindow (v4l2xv->dpy,
        DefaultRootWindow (v4l2xv->dpy),
        0, 0, width, height, 0, 0,
        XBlackPixel (v4l2xv->dpy, DefaultScreen (v4l2xv->dpy)));

    GST_DEBUG_OBJECT (v4l2object->element, "win=%lu", win);

    /* @todo add mouse events for all windows, and button events for self
     * created windows, and hook up to navigation interface.. note that at
     * least some of the events we want to handle regardless of whether it
     * is a self created window or not.. such as mouse/button events in
     * order to implement navigation interface?
     */
    XSelectInput (v4l2xv->dpy, win, ExposureMask | StructureNotifyMask);
    v4l2xv->event_id = g_timeout_add (45, event_refresh, v4l2object);

    XMapRaised (v4l2xv->dpy, win);

    XSync (v4l2xv->dpy, FALSE);

    g_mutex_unlock (v4l2xv->mutex);

    GST_DEBUG_OBJECT (v4l2object->element, "got window");

    gst_v4l2_xoverlay_set_window_handle (v4l2object, win);
  }
}
