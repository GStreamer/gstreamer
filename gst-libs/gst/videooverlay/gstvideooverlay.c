/*
 *  GStreamer Video Overlay interface.
 *
 *  Copyright (C) <2003> Julien Moutte <julien@moutte.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideooverlay.h"

/* VideoSink signals and args */

enum {
  HAVE_VIDEO_OVERLAY,
  HAVE_SIZE,
  LAST_SIGNAL
};

static guint gst_video_overlay_signals[LAST_SIGNAL] = { 0 };

/* Initing stuff */

static void
gst_video_overlay_base_init (gpointer g_class)
{
  static gboolean initialized = FALSE;
  
  if (! initialized)
    {
      gst_video_overlay_signals[HAVE_SIZE] =
        g_signal_new ("have_size",
                      GST_TYPE_VIDEO_OVERLAY, G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GstVideoOverlayInterface, have_size),
                      NULL, NULL,
                      gst_marshal_VOID__INT_INT, G_TYPE_NONE, 2,
		      G_TYPE_UINT, G_TYPE_UINT);

      gst_video_overlay_signals[HAVE_VIDEO_OVERLAY] =
        g_signal_new ("have_video_overlay",
                      GST_TYPE_VIDEO_OVERLAY, G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GstVideoOverlayInterface, have_video_overlay),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
		      G_TYPE_POINTER);
      
      initialized = TRUE;
    }
}

/* Public virtual methods */

/**
 * gst_video_overlay_set_video_overlay:
 * @overlay: a #GstVideoOverlay to set the video overlay on.
 * @video_out: the #gpointer linking to video overlay.
 *
 * This will call the video overlay's set_video_overlay method. You should
 * use this method to tell to a video overlay to display video output to a
 * specific video overlay resource.
 */
void
gst_video_overlay_set_video_overlay (GstVideoOverlay *overlay,
                                     GstVideoOverlayResource *resource)
{
  GstVideoOverlayInterface *interface;
  
  g_return_if_fail (overlay != NULL);
  g_return_if_fail (GST_IS_VIDEO_OVERLAY (overlay));
  
  interface = GST_VIDEO_OVERLAY_GET_INTERFACE (overlay);
  
  if (interface->set_video_out)
    interface->set_video_out (overlay, resource);
}

/* Public methods */

/**
 * gst_video_overlay_got_video_overlay:
 * @overlay: a #GstVideoOverlay which got a video overlay resource.
 * @video_out: a #gpointer linking to the video overlay ressource.
 *
 * This will fire an have_video_out signal.
 *
 * This function should be used by video overlay developpers.
 */
void
gst_video_overlay_got_video_overlay (GstVideoOverlay *overlay,
                                     GstVideoOverlayResource *resource)
{
  g_return_if_fail (overlay != NULL);
  g_return_if_fail (GST_IS_VIDEO_OVERLAY (overlay));
  
  g_signal_emit (G_OBJECT (overlay),
                 gst_video_overlay_signals[HAVE_VIDEO_OVERLAY], 0, resource);
}

/**
 * gst_video_overlay_got_video_size:
 * @overlay: a #GstVideoOverlay which received video geometry.
 * @width: a width as a #gint.
 * @height: a height as a #gint.
 *
 * This will fire an have_size signal.
 *
 * This function should be used by video overlay developpers.
 */
void
gst_video_overlay_got_video_size (GstVideoOverlay *overlay,
                                  gint width, gint height)
{
  g_return_if_fail (overlay != NULL);
  g_return_if_fail (GST_IS_VIDEO_OVERLAY (overlay));
  
  g_signal_emit (G_OBJECT (overlay), gst_video_overlay_signals[HAVE_SIZE],
                 0, width, height);
}

GType
gst_video_overlay_get_type (void)
{
  static GType overlay_type = 0;

  if (!overlay_type)
    {
      static const GTypeInfo overlay_info = {
        sizeof (GstVideoOverlayInterface), /* class_size */
        gst_video_overlay_base_init,   /* base_init */
        NULL,
        NULL,
        NULL,
        NULL,
        0,
        0,
        NULL
      };
    
      overlay_type = g_type_register_static (G_TYPE_INTERFACE,
                                             "GstVideoOverlay",
                                             &overlay_info, 0);
      g_type_interface_add_prerequisite (overlay_type,
                                         GST_TYPE_INTERFACE);
    }
    
  return overlay_type;
}
