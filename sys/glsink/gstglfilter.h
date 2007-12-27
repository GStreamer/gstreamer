/* 
 * GStreamer
 * Copyright (C) 2007 David Schleef <ds@schleef.org>
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

#ifndef _GST_GL_FILTER_H_
#define _GST_GL_FILTER_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gstglbuffer.h>

#define GST_TYPE_GL_FILTER            (gst_gl_filter_get_type())
#define GST_GL_FILTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_FILTER,GstGLFilter))
#define GST_IS_GL_FILTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_FILTER))
#define GST_GL_FILTER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_GL_FILTER,GstGLFilterClass))
#define GST_IS_GL_FILTER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_GL_FILTER))
#define GST_GL_FILTER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_GL_FILTER,GstGLFilterClass))
typedef struct _GstGLFilter GstGLFilter;
typedef struct _GstGLFilterClass GstGLFilterClass;

typedef gboolean (*GstGLFilterProcessFunc) (GstGLFilter *filter,
    GstGLBuffer *outbuf, GstGLBuffer *inbuf);
typedef gboolean (*GstGLFilterStartFunc) (GstGLFilter *filter);
typedef gboolean (*GstGLFilterStopFunc) (GstGLFilter *filter);

struct _GstGLFilter
{
  GstElement element;

  GstPad *srcpad;
  GstPad *sinkpad;

  /* < private > */

  GstGLDisplay *display;
  GstGLBufferFormat format;
  int width;
  int height;
};

struct _GstGLFilterClass
{
  GstElementClass element_class;
  GstGLFilterProcessFunc transform;
  GstGLFilterStartFunc start;
  GstGLFilterStopFunc stop;
};

GType gst_gl_filter_get_type(void);

#endif

