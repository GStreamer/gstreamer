/*
 * GStreamer
 * Copyright (C) 2014 Matthew Waters <ystreet00@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gl/egl/gstgldisplay_egl.h>
#include <gst/gl/egl/gsteglimagememory.h>

GST_DEBUG_CATEGORY_STATIC (gst_gl_display_debug);
#define GST_CAT_DEFAULT gst_gl_display_debug

G_DEFINE_TYPE (GstGLDisplayEGL, gst_gl_display_egl, GST_TYPE_GL_DISPLAY);

static void gst_gl_display_egl_finalize (GObject * object);
static guintptr gst_gl_display_egl_get_handle (GstGLDisplay * display);

static void
gst_gl_display_egl_class_init (GstGLDisplayEGLClass * klass)
{
  GST_GL_DISPLAY_CLASS (klass)->get_handle =
      GST_DEBUG_FUNCPTR (gst_gl_display_egl_get_handle);

  G_OBJECT_CLASS (klass)->finalize = gst_gl_display_egl_finalize;
}

static void
gst_gl_display_egl_init (GstGLDisplayEGL * display_egl)
{
  GstGLDisplay *display = (GstGLDisplay *) display_egl;

  display->type = GST_GL_DISPLAY_TYPE_EGL;
  display_egl->foreign_display = FALSE;

  gst_egl_image_memory_init ();
}

static void
gst_gl_display_egl_finalize (GObject * object)
{
  GstGLDisplayEGL *display_egl = GST_GL_DISPLAY_EGL (object);

  if (display_egl->display && !display_egl->foreign_display) {
    eglTerminate (display_egl->display);
    display_egl->display = NULL;
  }

  G_OBJECT_CLASS (gst_gl_display_egl_parent_class)->finalize (object);
}

/**
 * gst_gl_display_egl_new:
 * @name: (allow-none): a display name
 *
 * Create a new #GstGLDisplayEGL from the x11 display name.  See XOpenDisplay()
 * for details on what is a valid name.
 *
 * Returns: (transfer full): a new #GstGLDisplayEGL or %NULL
 */
GstGLDisplayEGL *
gst_gl_display_egl_new (void)
{
  GstGLDisplayEGL *ret;

  GST_DEBUG_CATEGORY_GET (gst_gl_display_debug, "gldisplay");

  ret = g_object_new (GST_TYPE_GL_DISPLAY_EGL, NULL);
  ret->display = eglGetDisplay ((EGLNativeDisplayType) EGL_DEFAULT_DISPLAY);

  if (!ret->display) {
    GST_ERROR ("Failed to open EGL display connection");
  }

  return ret;
}

/**
 * gst_gl_display_egl_new_with_display:
 * @display: an existing, x11 display
 *
 * Creates a new display connection from a X11 Display.
 *
 * Returns: (transfer full): a new #GstGLDisplayEGL
 */
GstGLDisplayEGL *
gst_gl_display_egl_new_with_egl_display (EGLDisplay display)
{
  GstGLDisplayEGL *ret;

  g_return_val_if_fail (display != NULL, NULL);

  GST_DEBUG_CATEGORY_GET (gst_gl_display_debug, "gldisplay");

  ret = g_object_new (GST_TYPE_GL_DISPLAY_EGL, NULL);

  ret->display = display;
  ret->foreign_display = TRUE;

  return ret;
}

static guintptr
gst_gl_display_egl_get_handle (GstGLDisplay * display)
{
  return (guintptr) GST_GL_DISPLAY_EGL (display)->display;
}
