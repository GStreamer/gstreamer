/* GStreamer X-based Overlay
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * x-overlay.c: X-based overlay interface design
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

#include "xoverlay.h"

static void gst_x_overlay_base_init (gpointer g_class);

GType
gst_x_overlay_get_type (void)
{
  static GType gst_x_overlay_type = 0;

  if (!gst_x_overlay_type) {
    static const GTypeInfo gst_x_overlay_info = {
      sizeof (GstXOverlayClass),
      gst_x_overlay_base_init,
      NULL,
      NULL,
      NULL,
      NULL,
      0,
      0,
      NULL,
    };

    gst_x_overlay_type = g_type_register_static (G_TYPE_INTERFACE,
        "GstXOverlay", &gst_x_overlay_info, 0);
    g_type_interface_add_prerequisite (gst_x_overlay_type,
        GST_TYPE_IMPLEMENTS_INTERFACE);
  }

  return gst_x_overlay_type;
}

static void
gst_x_overlay_base_init (gpointer g_class)
{
  GstXOverlayClass *overlay_class = (GstXOverlayClass *) g_class;

  overlay_class->set_xwindow_id = NULL;
}

/**
 * gst_x_overlay_set_xwindow_id:
 * @overlay: a #GstXOverlay to set the XWindow on.
 * @xwindow_id: a #XID referencing the XWindow.
 *
 * This will call the video overlay's set_xwindow_id method. You should
 * use this method to tell to a XOverlay to display video output to a
 * specific XWindow. Passing 0 as the xwindow_id will tell the overlay to
 * stop using that window and create an internal one.
 */
void
gst_x_overlay_set_xwindow_id (GstXOverlay * overlay, gulong xwindow_id)
{
  GstXOverlayClass *klass = GST_X_OVERLAY_GET_CLASS (overlay);

  if (klass->set_xwindow_id) {
    klass->set_xwindow_id (overlay, xwindow_id);
  }
}

/**
 * gst_x_overlay_got_xwindow_id:
 * @overlay: a #GstXOverlay which got a XWindow.
 * @xwindow_id: a #XID referencing the XWindow.
 *
 * This will post a "have-xwindow-id" element message on the bus.
 *
 * This function should only be used by video overlay plugin developers.
 */
void
gst_x_overlay_got_xwindow_id (GstXOverlay * overlay, gulong xwindow_id)
{
  GstStructure *s;
  GstMessage *msg;

  g_return_if_fail (overlay != NULL);
  g_return_if_fail (GST_IS_X_OVERLAY (overlay));

  GST_LOG_OBJECT (GST_OBJECT (overlay), "xwindow_id = %lu", xwindow_id);
  s = gst_structure_new ("have-xwindow-id", "xwindow-id", G_TYPE_ULONG,
      xwindow_id, NULL);
  msg = gst_message_new_element (GST_OBJECT (overlay), s);
  gst_element_post_message (GST_ELEMENT (overlay), msg);
}

/**
 * gst_x_overlay_prepare_xwindow_id:
 * @overlay: a #GstXOverlay which does not yet have an XWindow.
 *
 * This will post a "prepare-xwindow-id" element message on the bus
 * to give applications an opportunity to call 
 * gst_x_overlay_set_xwindow_id() before a plugin creates its own
 * window.
 *
 * This function should only be used by video overlay plugin developers.
 */
void
gst_x_overlay_prepare_xwindow_id (GstXOverlay * overlay)
{
  GstStructure *s;
  GstMessage *msg;

  g_return_if_fail (overlay != NULL);
  g_return_if_fail (GST_IS_X_OVERLAY (overlay));

  GST_LOG_OBJECT (GST_OBJECT (overlay), "prepare xwindow_id");
  s = gst_structure_new ("prepare-xwindow-id", NULL);
  msg = gst_message_new_element (GST_OBJECT (overlay), s);
  gst_element_post_message (GST_ELEMENT (overlay), msg);
}

/**
 * gst_x_overlay_expose:
 * @overlay: a #GstXOverlay to expose.
 *
 * Tell an overlay that it has been exposed. This will redraw the current frame
 * in the drawable even if the pipeline is PAUSED.
 */
void
gst_x_overlay_expose (GstXOverlay * overlay)
{
  GstXOverlayClass *klass = GST_X_OVERLAY_GET_CLASS (overlay);

  if (klass->expose) {
    klass->expose (overlay);
  }
}
