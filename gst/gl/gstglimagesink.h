/*
 * GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
 * Copyright (C) 2005,2006,2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
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

#ifndef _GLIMAGESINK_H_
#define _GLIMAGESINK_H_

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>

#include "gstglbufferpool.h"

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_debug_glimage_sink);

#define GST_TYPE_GLIMAGE_SINK \
    (gst_glimage_sink_get_type())
#define GST_GLIMAGE_SINK(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GLIMAGE_SINK,GstGLImageSink))
#define GST_GLIMAGE_SINK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GLIMAGE_SINK,GstGLImageSinkClass))
#define GST_IS_GLIMAGE_SINK(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GLIMAGE_SINK))
#define GST_IS_GLIMAGE_SINK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GLIMAGE_SINK))

typedef struct _GstGLImageSink GstGLImageSink;
typedef struct _GstGLImageSinkClass GstGLImageSinkClass;

struct _GstGLImageSink
{
    GstVideoSink video_sink;

    //properties
    gchar *display_name;

    gulong window_id;
    gulong new_window_id;

    //caps
    GstVideoInfo info;

    GstGLDisplay *display;
    GstBuffer *stored_buffer;

    CRCB clientReshapeCallback;
    CDCB clientDrawCallback;
    gpointer client_data;

    gboolean keep_aspect_ratio;
    GValue *par;

    GstBufferPool *pool;
};

struct _GstGLImageSinkClass
{
    GstVideoSinkClass video_sink_class;
};

GType gst_glimage_sink_get_type(void);

G_END_DECLS

#endif

