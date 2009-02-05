/*
 * GStreamer
 * Copyright (C) 2008 Nokia Corporation <multimedia@maemo.org>
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

/*
 * Includes
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstcamerabinxoverlay.h"
#include "gstcamerabin.h"

/*
 * static functions implementation
 */

static void
gst_camerabin_expose (GstXOverlay * overlay)
{
  if (overlay && GST_CAMERABIN (overlay)->view_sink) {
    GstXOverlay *xoverlay = GST_X_OVERLAY (GST_CAMERABIN (overlay)->view_sink);
    gst_x_overlay_expose (xoverlay);
  }
}

static void
gst_camerabin_set_xwindow_id (GstXOverlay * overlay, gulong xwindow_id)
{
  if (overlay && GST_CAMERABIN (overlay)->view_sink) {
    GstXOverlay *xoverlay = GST_X_OVERLAY (GST_CAMERABIN (overlay)->view_sink);
    gst_x_overlay_set_xwindow_id (xoverlay, xwindow_id);
  }
}

static void
gst_camerabin_set_event_handling (GstXOverlay * overlay, gboolean handle_events)
{
  if (overlay && GST_CAMERABIN (overlay)->view_sink) {
    GstXOverlay *xoverlay = GST_X_OVERLAY (GST_CAMERABIN (overlay)->view_sink);
    gst_x_overlay_handle_events (xoverlay, handle_events);
  }
}

/*
 * extern functions implementation
 */

void
gst_camerabin_xoverlay_init (GstXOverlayClass * iface)
{
  iface->set_xwindow_id = gst_camerabin_set_xwindow_id;
  iface->expose = gst_camerabin_expose;
  iface->handle_events = gst_camerabin_set_event_handling;
}
