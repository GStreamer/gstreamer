/*
 *  GStreamer Video sink.
 *
 *  Copyright (C) <2003> Julien Moutte <julien@moutte.net>
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
 
#ifndef __GST_VIDEOSINK_H__
#define __GST_VIDEOSINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

G_BEGIN_DECLS
  
#define GST_TYPE_VIDEOSINK (gst_videosink_get_type())
#define GST_VIDEOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VIDEOSINK, GstVideoSink))
#define GST_VIDEOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VIDEOSINK, GstVideoSink))
#define GST_IS_VIDEOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VIDEOSINK))
#define GST_IS_VIDEOSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VIDEOSINK))
#define GST_VIDEOSINK_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VIDEOSINK, GstVideoSinkClass))
  
#define GST_VIDEOSINK_PAD GST_BASESINK_PAD
#define GST_VIDEOSINK_CLOCK GST_BASESINK_CLOCK
#define GST_VIDEOSINK_WIDTH(obj) (GST_VIDEOSINK (obj)->width)
#define GST_VIDEOSINK_HEIGHT(obj) (GST_VIDEOSINK (obj)->height)
  
typedef struct _GstVideoSink GstVideoSink;
typedef struct _GstVideoSinkClass GstVideoSinkClass;

struct _GstVideoSink {
  GstBaseSink element;
  
  gint width, height;
  
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstVideoSinkClass {
  GstBaseSinkClass parent_class;
      
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_videosink_get_type (void);

G_END_DECLS

#endif  /* __GST_VIDEOSINK_H__ */
