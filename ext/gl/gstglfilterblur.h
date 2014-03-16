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

#ifndef _GST_GL_FILTERBLUR_H_
#define _GST_GL_FILTERBLUR_H_

#include <gst/gl/gstglfilter.h>

#define GST_TYPE_GL_FILTERBLUR            (gst_gl_filterblur_get_type())
#define GST_GL_FILTERBLUR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_FILTERBLUR,GstGLFilterBlur))
#define GST_IS_GL_FILTERBLUR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_FILTERBLUR))
#define GST_GL_FILTERBLUR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_GL_FILTERBLUR,GstGLFilterBlurClass))
#define GST_IS_GL_FILTERBLUR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_GL_FILTERBLUR))
#define GST_GL_FILTERBLUR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_GL_FILTERBLUR,GstGLFilterBlurClass))

typedef struct _GstGLFilterBlur GstGLFilterBlur;
typedef struct _GstGLFilterBlurClass GstGLFilterBlurClass;

struct _GstGLFilterBlur
{
  GstGLFilter filter;
  GstGLShader *shader0;
  GstGLShader *shader1;

  GLuint midtexture;
  float gauss_kernel[7];
};

struct _GstGLFilterBlurClass
{
  GstGLFilterClass filter_class;
};

GType gst_gl_filterblur_get_type (void);

#endif /* _GST_GL_FILTERBLUR_H_ */
