/*
 * GStreamer
 * Copyright (C) 2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
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

#ifndef __GST_GL_H__
#define __GST_GL_H__

#include "gstglconfig.h"

#include <gst/video/video.h>

typedef struct _GstGLShader GstGLShader;
typedef struct _GstGLWindow GstGLWindow;

#include "gstglwindow.h"
#include "gstglshader.h"
#include "gstglutils.h"

G_BEGIN_DECLS

GType gst_gl_display_get_type (void);
#define GST_GL_TYPE_DISPLAY (gst_gl_display_get_type())
#define GST_GL_DISPLAY(obj)	(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_GL_TYPE_DISPLAY,GstGLDisplay))
#define GST_GL_DISPLAY_CLASS(klass)	\
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_GL_TYPE_DISPLAY,GstGLDisplayClass))
#define GST_IS_GL_DISPLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_GL_TYPE_DISPLAY))
#define GST_IS_GL_DISPLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_GL_TYPE_DISPLAY))
#define GST_GL_DISPLAY_CAST(obj) ((GstGLDisplay*)(obj))

typedef struct _GstGLDisplay GstGLDisplay;
typedef struct _GstGLDisplayClass GstGLDisplayClass;
typedef struct _GstGLDisplayPrivate GstGLDisplayPrivate;

/**
 * GstGLDisplayConversion:
 *
 * %GST_GL_DISPLAY_CONVERSION_GLSL: Convert using GLSL (shaders)
 * %GST_GL_DISPLAY_CONVERSION_MATRIX: Convert using the ARB_imaging extension (not implemented)
 * %GST_GL_DISPLAY_CONVERSION_MESA: Convert using support in MESA
 */
typedef enum
{
  GST_GL_DISPLAY_CONVERSION_GLSL,
  GST_GL_DISPLAY_CONVERSION_MATRIX,
  GST_GL_DISPLAY_CONVERSION_MESA,
} GstGLDisplayConversion;


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
  GObject        object;

  /* thread safe */
  GMutex         mutex;

  /* gl API we are using */
  GstGLAPI       gl_api;
  /* foreign gl context */
  gulong         external_gl_context;

  GstGLFuncs *gl_vtable;

  GstGLDisplayPrivate *priv;
};


struct _GstGLDisplayClass
{
  GObjectClass object_class;
};

GstGLDisplay *gst_gl_display_new (void);

void gst_gl_display_thread_add (GstGLDisplay * display,
    GstGLDisplayThreadFunc func, gpointer data);

gulong gst_gl_display_get_internal_gl_context (GstGLDisplay * display);

void gst_gl_display_lock (GstGLDisplay * display);
void gst_gl_display_unlock (GstGLDisplay * display);
GstGLAPI gst_gl_display_get_gl_api (GstGLDisplay * display);

gpointer gst_gl_display_get_gl_vtable (GstGLDisplay * display);

void gst_gl_display_set_window (GstGLDisplay * display, GstGLWindow * window);
GstGLWindow * gst_gl_display_get_window (GstGLDisplay * display);
GstGLWindow * gst_gl_display_get_window_unlocked (GstGLDisplay * display);

G_END_DECLS

#endif /* __GST_GL_H__ */
