/* GStreamer X-based Overlay
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * tv-mixer.c: tv-mixer design virtual class function wrappers
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

static void 	gst_x_overlay_class_init	(GstXOverlayClass *klass);

GType
gst_x_overlay_get_type (void)
{
  static GType gst_x_overlay_type = 0;

  if (!gst_x_overlay_type) {
    static const GTypeInfo gst_x_overlay_info = {
      sizeof (GstXOverlayClass),
      (GBaseInitFunc) gst_x_overlay_class_init,
      NULL,
      NULL,
      NULL,
      NULL,
      0,
      0,
      NULL,
    };

    gst_x_overlay_type = g_type_register_static (G_TYPE_INTERFACE,
						 "GstXOverlay",
						 &gst_x_overlay_info, 0);
    g_type_interface_add_prerequisite (gst_x_overlay_type,
				       GST_TYPE_INTERFACE);
  }

  return gst_x_overlay_type;
}

static void
gst_x_overlay_class_init (GstXOverlayClass *klass)
{
  /* default virtual functions */
  klass->set_xwindow_id = NULL;
}

void
gst_x_overlay_set_xwindow_id (GstXOverlay *overlay,
			      XID          xwindow_id)
{
  GstXOverlayClass *klass = GST_X_OVERLAY_GET_CLASS (overlay);

  if (klass->set_xwindow_id) {
    klass->set_xwindow_id (overlay, xwindow_id);
  }
}
