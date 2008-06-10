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

#ifndef _GST_GLGRAPHICMAKER_H_
#define _GST_GLGRAPHICMAKER_H_

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#include "gstglbuffer.h"

G_BEGIN_DECLS

#define GST_TYPE_GL_GRAPHICMAKER            (gst_gl_graphicmaker_get_type())
#define GST_GL_GRAPHICMAKER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_GRAPHICMAKER,GstGLGraphicmaker))
#define GST_IS_GL_GRAPHICMAKER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_GRAPHICMAKER))
#define GST_GL_GRAPHICMAKER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_GL_GRAPHICMAKER,GstGLGraphicmakerClass))
#define GST_IS_GL_GRAPHICMAKER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_GL_GRAPHICMAKER))
#define GST_GL_GRAPHICMAKER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_GL_GRAPHICMAKER,GstGLGraphicmakerClass))

typedef struct _GstGLGraphicmaker GstGLGraphicmaker;
typedef struct _GstGLGraphicmakerClass GstGLGraphicmakerClass;


struct _GstGLGraphicmaker
{
    GstBaseTransform base_transform;

    GstPad *srcpad;
    GstPad *sinkpad;

    GstGLDisplay *display;
    GstVideoFormat video_format;
    gint inWidth;
    gint inHeight;
    gint outWidth;
    gint outHeight;  
};

struct _GstGLGraphicmakerClass
{
    GstBaseTransformClass base_transform_class;
};

GType gst_gl_graphicmaker_get_type (void);

G_END_DECLS

#endif /* _GST_GLGRAPHICMAKER_H_ */
