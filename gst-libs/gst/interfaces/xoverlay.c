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

enum {
  HAVE_XWINDOW_ID,
  LAST_SIGNAL
};

static guint gst_x_overlay_signals[LAST_SIGNAL] = { 0 };

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
						 "GstXOverlay",
						 &gst_x_overlay_info, 0);
    g_type_interface_add_prerequisite (gst_x_overlay_type,
				       GST_TYPE_IMPLEMENTS_INTERFACE);
  }

  return gst_x_overlay_type;
}

static void
gst_x_overlay_base_init (gpointer g_class)
{
  static gboolean initialized = FALSE;
  
  if (! initialized)
    {
      gst_x_overlay_signals[HAVE_XWINDOW_ID] =
        g_signal_new ("have_xwindow_id",
                      GST_TYPE_X_OVERLAY, G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GstXOverlayClass, have_xwindow_id),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
		      G_TYPE_POINTER);
      
      initialized = TRUE;
    }

  GST_X_OVERLAY_CLASS (g_class)->set_xwindow_id = NULL;
}

/**
 * gst_x_overlay_set_xwindow_id:
 * @overlay: a #GstXOverlay to set the XWindow on.
 * @xwindow_id: a #XID referencing the XWindow.
 *
 * This will call the video overlay's set_xwindow_id method. You should
 * use this method to tell to a XOverlay to display video output to a
 * specific XWindow.
 */
void
gst_x_overlay_set_xwindow_id (GstXOverlay *overlay, XID xwindow_id)
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
 * This will fire an have_xwindow_id signal.
 *
 * This function should be used by video overlay developpers.
 */
void
gst_x_overlay_got_xwindow_id (GstXOverlay *overlay, XID xwindow_id)
{
  g_return_if_fail (overlay != NULL);
  g_return_if_fail (GST_IS_X_OVERLAY (overlay));
  
  g_signal_emit (G_OBJECT (overlay),
                 gst_x_overlay_signals[HAVE_XWINDOW_ID], 0, xwindow_id);
}
