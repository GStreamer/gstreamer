/*
 * GStreamer
 * Copyright (C) 2015 Lubosz Sarnecki <lubosz.sarnecki@collabora.co.uk>
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

#ifndef __GST_GL_COMPOSITION_OVERLAY_H__
#define __GST_GL_COMPOSITION_OVERLAY_H__

#include <gst/video/video.h>
#include <gst/gl/gstgl_fwd.h>

#define GST_TYPE_GL_COMPOSITION_OVERLAY (gst_gl_composition_overlay_get_type())
#define GST_GL_COMPOSITION_OVERLAY(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_COMPOSITION_OVERLAY,GstGLCompositionOverlay))
#define GST_GL_COMPOSITION_OVERLAY_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_COMPOSITION_OVERLAY,GstGLCompositionOverlayClass))
#define GST_IS_GL_COMPOSITION_OVERLAY(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_COMPOSITION_OVERLAY))
#define GST_IS_GL_COMPOSITION_OVERLAY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_COMPOSITION_OVERLAY))
#define GST_GL_COMPOSITION_OVERLAY_CAST(obj) ((GstGLCompositionOverlay*)(obj))

G_BEGIN_DECLS

GType gst_gl_composition_overlay_get_type (void);

/**
 * GstGLCompositionOverlay
 *
 * Opaque #GstGLCompositionOverlay object
 */
struct _GstGLCompositionOverlay
{
  /*< private >*/
  GstObject parent;
  GstGLContext *context;

  GLuint vao;
  GLuint index_buffer;
  GLuint position_buffer;
  GLuint texcoord_buffer;
  GLint  position_attrib;
  GLint  texcoord_attrib;

  GLfloat positions[16];

  GLuint texture_id;
  GstGLMemory *gl_memory;
  GstVideoOverlayRectangle *rectangle;
};

/**
 * GstGLCompositionOverlayClass:
 *
 */
struct _GstGLCompositionOverlayClass
{
  GstObjectClass object_class;
};

GstGLCompositionOverlay *gst_gl_composition_overlay_new (GstGLContext * context,
    GstVideoOverlayRectangle * rectangle, GLint position_attrib,
    GLint texcoord_attrib);

void gst_gl_composition_overlay_upload (GstGLCompositionOverlay * overlay,
    GstBuffer * buf);

void gst_gl_composition_overlay_draw (GstGLCompositionOverlay * overlay,
    GstGLShader * shader);

G_END_DECLS
#endif /* __GST_GL_COMPOSITION_OVERLAY_H__ */
