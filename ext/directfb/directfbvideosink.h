/* GStreamer DirectFB plugin
 * Copyright (C) 2004 Julien MOUTTE <julien@moutte.net>
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
 
#ifndef __GST_DIRECTFBVIDEOSINK_H__
#define __GST_DIRECTFBVIDEOSINK_H__

#include <gst/video/videosink.h>

#include <directfb.h>

G_BEGIN_DECLS

#define GST_TYPE_DIRECTFBVIDEOSINK              (gst_directfbvideosink_get_type())
#define GST_DIRECTFBVIDEOSINK(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DIRECTFBVIDEOSINK, GstDirectFBVideoSink))
#define GST_DIRECTFBVIDEOSINK_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DIRECTFBVIDEOSINK, GstDirectFBVideoSink))
#define GST_IS_DIRECTFBVIDEOSINK(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DIRECTFBVIDEOSINK))
#define GST_IS_DIRECTFBVIDEOSINK_CLASS(obj)     (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DIRECTFBVIDEOSINK))

typedef struct _GstDirectFBVideoSink GstDirectFBVideoSink;
typedef struct _GstDirectFBVideoSinkClass GstDirectFBVideoSinkClass;

struct _GstDirectFBVideoSink {
  /* Our element stuff */
  GstVideoSink videosink;
  
  gdouble framerate;
  guint video_width, video_height;     /* size of incoming video */
  
  GstClockTime time;
  
  IDirectFB *directfb;
  IDirectFBDisplayLayer *layer;
  IDirectFBSurface *surface;
  IDirectFBSurface *foreign_surface;
  IDirectFBSurface *primary;
  DFBSurfacePixelFormat pixel_format;
  
  gboolean surface_locked;
  gboolean internal_surface;
};

struct _GstDirectFBVideoSinkClass {
  GstVideoSinkClass parent_class;
};

GType gst_directfbvideosink_get_type (void);

G_END_DECLS

#endif /* __GST_DIRECTFBVIDEOSINK_H__ */
