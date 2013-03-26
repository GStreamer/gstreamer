/* GStreamer
 * Copyright (C) 2012 Roland Krikava <info@bluedigits.com>
 * Copyright (C) 2010-2011 David Hoyt <dhoyt@hoytsoft.org>
 * Copyright (C) 2010 Andoni Morales <ylatuya@gmail.com>
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
#ifndef _GSTD3DVIDEOSINK_H_
#define _GSTD3DVIDEOSINK_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/videooverlay.h>
#include <gst/video/navigation.h>

#include "d3dhelpers.h"

G_BEGIN_DECLS

#define GST_TYPE_D3DVIDEOSINK                     (gst_d3dvideosink_get_type())
#define GST_D3DVIDEOSINK(obj)                     (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_D3DVIDEOSINK,GstD3DVideoSink))
#define GST_D3DVIDEOSINK_CLASS(klass)             (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_D3DVIDEOSINK,GstD3DVideoSinkClass))
#define GST_D3DVIDEOSINK_GET_CLASS(obj)           (GST_D3DVIDEOSINK_CLASS(G_OBJECT_GET_CLASS(obj)))
#define GST_IS_D3DVIDEOSINK(obj)                  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_D3DVIDEOSINK))
#define GST_IS_D3DVIDEOSINK_CLASS(klass)          (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_D3DVIDEOSINK))

typedef struct _GstD3DVideoSink GstD3DVideoSink;
typedef struct _GstD3DVideoSinkClass GstD3DVideoSinkClass;

struct _GstD3DVideoSink
{
  GstVideoSink       sink;
  GstD3DData         d3d;

  GstCaps *          supported_caps;

  GstVideoFormat     format;
  GstVideoInfo       info;
  gint               width;
  gint               height;
  GstBufferPool      *pool;
  GstBufferPool      *fallback_pool;
  GstBuffer          *fallback_buffer;

  GstVideoRectangle  crop_rect;
  GstVideoRectangle  render_rect;

  GRecMutex    lock;

  /* Properties */
  gboolean           force_aspect_ratio;
  gboolean           create_internal_window;
  gboolean           stream_stop_on_close;
  gboolean           enable_navigation_events;
};

struct _GstD3DVideoSinkClass
{
  GstVideoSinkClass parent_class;
  GstD3DDataClass   d3d;
  GRecMutex   lock;
};

#if 1
# define LOCK_SINK(sink)          g_rec_mutex_lock(&sink->lock);
# define UNLOCK_SINK(sink)        g_rec_mutex_unlock(&sink->lock);
# define LOCK_CLASS(obj, klass)   g_rec_mutex_lock(&klass->lock);
# define UNLOCK_CLASS(obj, klass) g_rec_mutex_unlock(&klass->lock);
#else
# define LOCK_SINK(sink)          GST_LOG_OBJECT(sink, "SINK   LOCK"); g_rec_mutex_lock(&sink->lock); GST_LOG_OBJECT(sink, "SINK LOCKED");
# define UNLOCK_SINK(sink)        g_rec_mutex_unlock(&sink->lock); GST_LOG_OBJECT(sink, "SINK UNLOCKED");
# define LOCK_CLASS(obj, klass)   GST_LOG_OBJECT(obj, "CLASS   LOCK"); g_rec_mutex_lock(&klass->lock); GST_LOG_OBJECT(obj, "CLASS LOCKED");
# define UNLOCK_CLASS(obj, klass) g_rec_mutex_unlock(&klass->lock); GST_LOG_OBJECT(obj, "CLASS UNLOCKED");
#endif

GType    gst_d3dvideosink_get_type (void);

G_END_DECLS


#endif /* _GSTD3DVIDEOSINK_H_ */
