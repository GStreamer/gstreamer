/*
 * GStreamer
 * Copyright (C) 2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2008 Filippo Argiolas <filippo.argiolas@gmail.com>
 * Copyright (C) 2013 Matthew Waters <ystreet00@gmail.com>
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

#ifndef __GST_GL_DISPLAY_H__
#define __GST_GL_DISPLAY_H__

#include <gst/gl/gstgl_fwd.h>

G_BEGIN_DECLS

GType gst_gl_display_get_type (void);

#define GST_TYPE_GL_DISPLAY             (gst_gl_display_get_type())
#define GST_GL_DISPLAY(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_DISPLAY,GstGLDisplay))
#define GST_GL_DISPLAY_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GL_DISPLAY,GstGLDisplayClass))
#define GST_IS_GL_DISPLAY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_DISPLAY))
#define GST_IS_GL_DISPLAY_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GL_DISPLAY))
#define GST_GL_DISPLAY_CAST(obj)        ((GstGLDisplay*)(obj))

/**
 * GstGLDisplayThreadFunc:
 * @display: a #GstGLDisplay
 * @data: user data
 *
 * Represents a function to run in the GL thread
 */
typedef void (*GstGLDisplayThreadFunc) (GstGLDisplay * display, gpointer data);

/**
 * GstGLDisplay:
 *
 * the contents of a #GstGLDisplay are private and should only be accessed
 * through the provided API
 */
struct _GstGLDisplay
{
  GstObject             object;

  /* <private> */
  GstGLContext         *context;
  GstGLAPI              gl_api;

  GstGLFuncs           *gl_vtable;

  GstGLDisplayPrivate  *priv;
};

struct _GstGLDisplayClass
{
  GstObjectClass object_class;
};

GstGLDisplay *gst_gl_display_new (void);

#define gst_gl_display_lock(display)        GST_OBJECT_LOCK (display)
#define gst_gl_display_unlock(display)      GST_OBJECT_UNLOCK (display)

GstGLAPI       gst_gl_display_get_gl_api             (GstGLDisplay * display);
gpointer       gst_gl_display_get_gl_vtable          (GstGLDisplay * display);
void           gst_gl_display_set_context             (GstGLDisplay * display, GstGLContext * context);
GstGLContext * gst_gl_display_get_context             (GstGLDisplay * display);
GstGLContext * gst_gl_display_get_context_unlocked    (GstGLDisplay * display);

void gst_gl_display_thread_add (GstGLDisplay * display,
    GstGLDisplayThreadFunc func, gpointer data);

#define GST_GL_DISPLAY_CONTEXT_TYPE "gst.gl.GLDisplay"
void     gst_context_set_gl_display (GstContext * context, GstGLDisplay * display);
gboolean gst_context_get_gl_display (GstContext * context, GstGLDisplay ** display);

G_END_DECLS

#endif /* __GST_GL_DISPLAY_H__ */
