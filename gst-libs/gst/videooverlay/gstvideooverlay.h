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
 
#ifndef __GST_VIDEO_OVERLAY_H__
#define __GST_VIDEO_OVERLAY_H__

#include <gst/gst.h>

G_BEGIN_DECLS
  
#define GST_TYPE_VIDEO_OVERLAY               (gst_video_overlay_get_type ())
#define GST_VIDEO_OVERLAY(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VIDEO_OVERLAY, GstVideoOverlay))
#define GST_IS_VIDEO_OVERLAY(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VIDEO_OVERLAY))
#define GST_VIDEO_OVERLAY_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), GST_TYPE_VIDEO_OVERLAY, GstVideoOverlayInterface))
  
typedef struct _GstVideoOverlay GstVideoOverlay;
typedef struct _GstVideoOverlayInterface GstVideoOverlayInterface;

typedef struct _GstVideoOverlayResource GstVideoOverlayResource;

typedef enum {
  GST_VIDEO_OVERLAY_RESOURCE_TYPE_XID,
  GST_VIDEO_OVERLAY_RESOURCE_TYPE_DIRECTFB
} GstVideoOverlayResourceType;

struct _GstVideoOverlayResource
{
  GstVideoOverlayResourceType resource_type;
  union
  {
    struct
    {
      guint32 xid;
    } x_resource;
    struct
    {
      gpointer display_layer;
      gpointer window;
    } dfb_resource;
  } resource_data;
};

struct _GstVideoOverlay;

struct GstVideoOverlayInterface {
  GTypeInterface parent_class;
  
  /* public virtual methods */
  void (*set_video_overlay) (GstVideoOverlay *overlay,
                             GstVideoOverlayResource *resource);

  /* signals */
  void (*have_video_overlay)  (GstVideoOverlay *overlay,
                               GstVideoOverlayResource *resource);
  void (*have_size)       (GstVideoOverlay *overlay, gint width, gint height);
};

GType gst_video_overlay_get_type (void);

/* public virtual methods */
void gst_video_overlay_set_video_overlay (GstVideoOverlay *overlay,
                                          GstVideoOverlayResource *resource);

/* public methods to fire signals */
void gst_video_overlay_got_video_overlay (GstVideoOverlay *overlay,
                                          GstVideoOverlayResource *resource);
void gst_video_overlay_got_video_size (GstVideoOverlay *overlay,
                                       gint width, gint height);

G_END_DECLS

#endif  /* __GST_VIDEO_OVERLAY_H__ */
