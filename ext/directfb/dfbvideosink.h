/* GStreamer DirectFB plugin
 * Copyright (C) 2005 Julien MOUTTE <julien@moutte.net>
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
 
#ifndef __GST_DFBVIDEOSINK_H__
#define __GST_DFBVIDEOSINK_H__

#include <gst/video/gstvideosink.h>

#include <directfb.h>

G_BEGIN_DECLS

#define GST_TYPE_DFBVIDEOSINK              (gst_dfbvideosink_get_type())
#define GST_DFBVIDEOSINK(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DFBVIDEOSINK, GstDfbVideoSink))
#define GST_DFBVIDEOSINK_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DFBVIDEOSINK, GstDfbVideoSinkClass))
#define GST_IS_DFBVIDEOSINK(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DFBVIDEOSINK))
#define GST_IS_DFBVIDEOSINK_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DFBVIDEOSINK))

typedef struct _GstDfbVideoSink GstDfbVideoSink;
typedef struct _GstDfbVideoSinkClass GstDfbVideoSinkClass;

typedef struct _GstMetaDfbSurface GstMetaDfbSurface;

const GstMetaInfo * gst_meta_dfbsurface_get_info (void);

#define GST_META_DFBSURFACE_GET(buf) ((GstMetaDfbSurface *)gst_buffer_get_meta(buf,gst_meta_dfbsurface_get_info()))
#define GST_META_DFBSURFACE_ADD(buf) ((GstMetaDfbSurface *)gst_buffer_add_meta(buf,gst_meta_dfbsurface_get_info(),NULL))

struct _GstMetaDfbSurface {
  GstMeta meta;

  IDirectFBSurface *surface;

  gint width;
  gint height;

  gboolean locked;

  DFBSurfacePixelFormat pixel_format;

  GstDfbVideoSink *dfbvideosink;
};

typedef struct _GstDfbVMode GstDfbVMode;

struct _GstDfbVMode {
  gint width;
  gint height;
  gint bpp;
};

/**
 * GstDfbVideoSink:
 *
 * The opaque #GstDfbVideoSink structure.
 */
struct _GstDfbVideoSink {
  GstVideoSink videosink;
  
  /* < private > */
  GMutex *pool_lock;
  GSList *buffer_pool;
  
  /* Framerate numerator and denominator */
  gint fps_n;
  gint fps_d;
  
  gint video_width, video_height; /* size of incoming video */
  gint out_width, out_height;
  
  /* Standalone */
  IDirectFB *dfb;
  
  GSList *vmodes; /* Video modes */
  
  gint layer_id;
  IDirectFBDisplayLayer *layer;
  IDirectFBSurface *primary;
  IDirectFBEventBuffer *event_buffer;
  GThread *event_thread;
  
  /* Embedded */
  IDirectFBSurface *ext_surface;
  
  DFBSurfacePixelFormat pixel_format;
  
  gboolean hw_scaling;
  gboolean backbuffer;
  gboolean vsync;
  gboolean setup;
  gboolean running;
  
  /* Color balance */
  GList *cb_channels;
  gint brightness;
  gint contrast;
  gint hue;
  gint saturation;
  gboolean cb_changed;
  
  /* object-set pixel aspect ratio */
  GValue *par;
};

struct _GstDfbVideoSinkClass {
  GstVideoSinkClass parent_class;
};

GType gst_dfbvideosink_get_type (void);

G_END_DECLS

#endif /* __GST_DFBVIDEOSINK_H__ */
