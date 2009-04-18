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

#ifndef _GST_GLUPLOAD_H_
#define _GST_GLUPLOAD_H_

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#include "gstglbuffer.h"

G_BEGIN_DECLS

#define GST_TYPE_GL_UPLOAD            (gst_gl_upload_get_type())
#define GST_GL_UPLOAD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_UPLOAD,GstGLUpload))
#define GST_IS_GL_UPLOAD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_UPLOAD))
#define GST_GL_UPLOAD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_GL_UPLOAD,GstGLUploadClass))
#define GST_IS_GL_UPLOAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_GL_UPLOAD))
#define GST_GL_UPLOAD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_GL_UPLOAD,GstGLUploadClass))

typedef struct _GstGLUpload GstGLUpload;
typedef struct _GstGLUploadClass GstGLUploadClass;


struct _GstGLUpload
{
    GstBaseTransform base_transform;

    GstPad *srcpad;
    GstPad *sinkpad;

    GstGLDisplay *display;

    GstVideoFormat video_format;
    gint video_width;
    gint video_height;
    gint gl_width;
    gint gl_height;
    guint64 external_gl_context;
};

struct _GstGLUploadClass
{
    GstBaseTransformClass base_transform_class;
};

GType gst_gl_upload_get_type (void);

G_END_DECLS

#endif /* _GST_GLUPLOAD_H_ */
