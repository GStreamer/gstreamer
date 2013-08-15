/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
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

#ifndef __GST_GL_CONTEXT_COCOA_H__
#define __GST_GL_CONTEXT_COCOA_H__

#include <gst/gst.h>

#include <gst/gl/gl.h>

G_BEGIN_DECLS

#define GST_GL_TYPE_CONTEXT_COCOA         (gst_gl_context_cocoa_get_type())
#define GST_GL_CONTEXT_COCOA(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_GL_TYPE_CONTEXT_COCOA, GstGLContextCocoa))
#define GST_GL_CONTEXT_COCOA_CLASS(k)     (G_TYPE_CHECK_CLASS((k), GST_GL_TYPE_CONTEXT_COCOA, GstGLContextCocoaClass))
#define GST_GL_IS_CONTEXT_COCOA(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_GL_TYPE_CONTEXT_COCOA))
#define GST_GL_IS_CONTEXT_COCOA_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_GL_TYPE_CONTEXT_COCOA))
#define GST_GL_CONTEXT_COCOA_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_GL_TYPE_CONTEXT_COCOA, GstGLContextCocoaClass))

typedef struct _GstGLContextCocoa        GstGLContextCocoa;
typedef struct _GstGLContextCocoaPrivate GstGLContextCocoaPrivate;
typedef struct _GstGLContextCocoaClass   GstGLContextCocoaClass;

struct _GstGLContextCocoa {
  /*< private >*/
  GstGLContext parent;

  /*< private >*/
  GstGLContextCocoaPrivate *priv;
  
  gpointer _reserved[GST_PADDING];
};

struct _GstGLContextCocoaClass {
  /*< private >*/
  GstGLContextClass parent_class;

  /*< private >*/
  gpointer _reserved[GST_PADDING_LARGE];

  GstGLContextCocoaPrivate *priv;
};

GType gst_gl_context_cocoa_get_type (void);

GstGLContextCocoa * gst_gl_context_cocoa_new (void);

G_END_DECLS

#endif /* __GST_GL_CONTEXT_COCOA_H__ */
