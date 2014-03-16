/*
 * GStreamer
 * Copyright (C) 2008 Filippo Argiolas <filippo.argiolas@gmail.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GST_GL_FILTERSOBEL_H_
#define _GST_GL_FILTERSOBEL_H_

#include <gst/gl/gstglfilter.h>

#define GST_TYPE_GL_FILTERSOBEL            (gst_gl_filtersobel_get_type())
#define GST_GL_FILTERSOBEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_FILTERSOBEL,GstGLFilterSobel))
#define GST_IS_GL_FILTERSOBEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_FILTERSOBEL))
#define GST_GL_FILTERSOBEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_GL_FILTERSOBEL,GstGLFilterSobelClass))
#define GST_IS_GL_FILTERSOBEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_GL_FILTERSOBEL))
#define GST_GL_FILTERSOBEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_GL_FILTERSOBEL,GstGLFilterSobelClass))

typedef struct _GstGLFilterSobel GstGLFilterSobel;
typedef struct _GstGLFilterSobelClass GstGLFilterSobelClass;

struct _GstGLFilterSobel
{
  GstGLFilter filter;
  GstGLShader *hconv;
  GstGLShader *vconv;
  GstGLShader *len;
  GstGLShader *desat;

  GLuint midtexture[5];

  gboolean invert;
};

struct _GstGLFilterSobelClass
{
  GstGLFilterClass filter_class;
};

GType gst_gl_filtersobel_get_type (void);

#endif /* _GST_GL_FILTERSOBEL_H_ */
