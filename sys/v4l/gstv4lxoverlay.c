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

#include <gst/gst.h>
#include <gst/xoverlay/xoverlay.h>
#include <gst/xwindowlistener/xwindowlistener.h>

#include "gstv4lxoverlay.h"
#include "gstv4lelement.h"
#include "v4l_calls.h"

static void gst_v4l_xoverlay_set_xwindow_id (GstXOverlay * overlay,
    XID xwindow_id);

void
gst_v4l_xoverlay_interface_init (GstXOverlayClass * klass)
{
  /* default virtual functions */
  klass->set_xwindow_id = gst_v4l_xoverlay_set_xwindow_id;
}

GstXWindowListener *
gst_v4l_xoverlay_new (GstV4lElement * v4lelement)
{
  GstXWindowListener *xwin = gst_x_window_listener_new (NULL,
      (MapWindowFunc) gst_v4l_enable_overlay,
      (SetWindowFunc) gst_v4l_set_window,
      (gpointer) v4lelement);

  v4lelement->overlay = xwin;
  v4lelement->xwindow_id = 0;

  return xwin;
}

void
gst_v4l_xoverlay_free (GstV4lElement * v4lelement)
{
  gst_v4l_xoverlay_close (v4lelement);
  g_object_unref (G_OBJECT (v4lelement->overlay));
  v4lelement->overlay = NULL;
}

void
gst_v4l_xoverlay_open (GstV4lElement * v4lelement)
{
  GstXWindowListener *xwin = v4lelement->overlay;

  if (xwin != NULL) {
    xwin->display_name = g_strdup (v4lelement->display);

    if (v4lelement->xwindow_id != 0 &&
        xwin->display_name && xwin->display_name[0] == ':') {
      gst_x_window_listener_set_xid (xwin, v4lelement->xwindow_id);
    }
  }
}

void
gst_v4l_xoverlay_close (GstV4lElement * v4lelement)
{
  GstXWindowListener *xwin = v4lelement->overlay;

  if (xwin != NULL) {
    if (v4lelement->xwindow_id != 0 &&
        xwin->display_name && xwin->display_name[0] == ':') {
      gst_x_window_listener_set_xid (xwin, 0);
    }

    g_free (xwin->display_name);
    xwin->display_name = NULL;
  }
}

static void
gst_v4l_xoverlay_set_xwindow_id (GstXOverlay * overlay, XID xwindow_id)
{
  GstV4lElement *v4lelement = GST_V4LELEMENT (overlay);
  GstXWindowListener *xwin = v4lelement->overlay;

  if (v4lelement->xwindow_id == xwindow_id) {
    return;
  }

  if (gst_element_get_state (GST_ELEMENT (v4lelement)) != GST_STATE_NULL &&
      v4lelement->xwindow_id != 0 &&
      xwin && xwin->display_name && xwin->display_name[0] == ':') {
    gst_x_window_listener_set_xid (xwin, 0);
  }

  v4lelement->xwindow_id = xwindow_id;

  if (gst_element_get_state (GST_ELEMENT (v4lelement)) != GST_STATE_NULL &&
      v4lelement->xwindow_id != 0 &&
      xwin && xwin->display_name && xwin->display_name[0] == ':') {
    gst_x_window_listener_set_xid (xwin, v4lelement->xwindow_id);
  }
}
