/* 
 * GStreamer
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

#ifndef _GST_GL_FILTER_H_
#define _GST_GL_FILTER_H_

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#include "gstglbuffer.h"

G_BEGIN_DECLS

#define GST_TYPE_GL_FILTER            (gst_gl_filter_get_type())
#define GST_GL_FILTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_FILTER,GstGLFilter))
#define GST_IS_GL_FILTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_FILTER))
#define GST_GL_FILTER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_GL_FILTER,GstGLFilterClass))
#define GST_IS_GL_FILTER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_GL_FILTER))
#define GST_GL_FILTER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_GL_FILTER,GstGLFilterClass))
typedef struct _GstGLFilter GstGLFilter;
typedef struct _GstGLFilterClass GstGLFilterClass;


typedef gboolean (*GstGLFilterSetCaps) (GstGLFilter* filter, 
    GstCaps* incaps, GstCaps* outcaps);
typedef gboolean (*GstGLFilterProcessFunc) (GstGLFilter *filter,
    GstGLBuffer *inbuf, GstGLBuffer *outbuf);
typedef void (*GstGLFilterOnInitFBO) (GstGLFilter *filter);
typedef void (*GstGLFilterOnReset) (GstGLFilter *filter);

struct _GstGLFilter
{
    GstBaseTransform base_transform;

    GstPad *srcpad;
    GstPad *sinkpad;

    GstGLDisplay *display;
    GstVideoFormat video_format;
    gint width;
    gint height;
    guint fbo;
    guint depthbuffer;
    guint texture;
};

struct _GstGLFilterClass
{
    GstBaseTransformClass base_transform_class;
    GstGLFilterSetCaps set_caps;
    GstGLFilterProcessFunc filter;
    GstGLFilterOnInitFBO onInitFBO;
    GstGLFilterOnReset onReset;
};

GType gst_gl_filter_get_type(void);

G_END_DECLS

#endif

