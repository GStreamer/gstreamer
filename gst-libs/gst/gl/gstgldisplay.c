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

/**
 * SECTION:gstgldisplay
 * @short_description: window system display connection abstraction
 * @title: GstGLDisplay
 * @see_also: #GstContext, #GstGLContext, #GstGLWindow
 *
 * #GstGLDisplay represents a connection to the underlying windowing system. 
 * Elements are required to make use of #GstContext to share and propogate
 * a #GstGLDisplay.
 *
 * There are a number of environment variables that influence the choice of
 * platform and window system specific functionality.
 * - GST_GL_WINDOW influences the window system to use.  Common values are
 *   'x11', 'wayland', 'win32' or 'cocoa'.
 * - GST_GL_PLATFORM influences the OpenGL platform to use.  Common values are
 *   'egl', 'glx', 'wgl' or 'cgl'.
 * - GST_GL_API influences the OpenGL API requested by the OpenGL platform.
 *   Common values are 'opengl' and 'gles2'.
 *
 * <note>Certain window systems require a special function to be called to
 * initialize threading support.  As this GStreamer GL library does not preclude
 * concurrent access to the windowing system, it is strongly advised that
 * applications ensure that threading support has been initialized before any
 * other toolkit/library functionality is accessed.  Failure to do so could
 * result in sudden application abortion during execution.  The most notably
 * example of such a function is X11's XInitThreads().</note>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gl.h"
#include "gstgldisplay.h"

#if GST_GL_HAVE_WINDOW_X11
#include <gst/gl/x11/gstgldisplay_x11.h>
#endif
#if GST_GL_HAVE_PLATFORM_EGL
#include <gst/gl/egl/gstgldisplay_egl.h>
#include <gst/gl/egl/gsteglimagememory.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_context);
GST_DEBUG_CATEGORY_STATIC (gst_gl_display_debug);
#define GST_CAT_DEFAULT gst_gl_display_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_display_debug, "gldisplay", 0, "opengl display"); \
  GST_DEBUG_CATEGORY_GET (gst_context, "GST_CONTEXT");

G_DEFINE_TYPE_WITH_CODE (GstGLDisplay, gst_gl_display, GST_TYPE_OBJECT,
    DEBUG_INIT);

#define GST_GL_DISPLAY_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_TYPE_GL_DISPLAY, GstGLDisplayPrivate))

static void gst_gl_display_finalize (GObject * object);
static guintptr gst_gl_display_default_get_handle (GstGLDisplay * display);

struct _GstGLDisplayPrivate
{
  gint dummy;
};

static void
gst_gl_display_class_init (GstGLDisplayClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstGLDisplayPrivate));

  klass->get_handle = gst_gl_display_default_get_handle;

  G_OBJECT_CLASS (klass)->finalize = gst_gl_display_finalize;
}

static void
gst_gl_display_init (GstGLDisplay * display)
{
  display->priv = GST_GL_DISPLAY_GET_PRIVATE (display);

  display->type = GST_GL_DISPLAY_TYPE_ANY;

  GST_TRACE ("init %p", display);

  gst_gl_memory_init ();

#if GST_GL_HAVE_PLATFORM_EGL
  gst_egl_image_memory_init ();
#endif
}

static void
gst_gl_display_finalize (GObject * object)
{
  GST_TRACE ("finalize %p", object);

  G_OBJECT_CLASS (gst_gl_display_parent_class)->finalize (object);
}

/**
 * gst_gl_display_new:
 *
 * Returns: (transfer full): a new #GstGLDisplay
 */
GstGLDisplay *
gst_gl_display_new (void)
{
  GstGLDisplay *display = NULL;
  const gchar *user_choice, *platform_choice;
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_gl_display_debug, "gldisplay", 0,
        "gldisplay element");
    g_once_init_leave (&_init, 1);
  }

  user_choice = g_getenv ("GST_GL_WINDOW");
  platform_choice = g_getenv ("GST_GL_PLATFORM");
  GST_INFO ("creating a display, user choice:%s (platform: %s)",
      GST_STR_NULL (user_choice), GST_STR_NULL (platform_choice));

#if GST_GL_HAVE_WINDOW_COCOA
  if (!display && (!user_choice || g_strstr_len (user_choice, 5, "cocoa")))
    display = g_object_new (GST_TYPE_GL_DISPLAY, NULL);
#endif
#if GST_GL_HAVE_WINDOW_X11
  if (!display && (!user_choice || g_strstr_len (user_choice, 3, "x11")))
    display = GST_GL_DISPLAY (gst_gl_display_x11_new (NULL));
#endif
#if GST_GL_HAVE_WINDOW_WAYLAND
  if (!display && (!user_choice || g_strstr_len (user_choice, 7, "wayland")))
    display = g_object_new (GST_TYPE_GL_DISPLAY, NULL);
#endif
#if GST_GL_HAVE_PLATFORM_EGL
  if (!display && (!platform_choice
          || g_strstr_len (platform_choice, 3, "egl")))
    display = GST_GL_DISPLAY (gst_gl_display_egl_new ());
#endif
  if (!display) {
    /* subclass returned a NULL window */
    GST_WARNING ("Could not create display. user specified %s "
        "(platform: %s), creating dummy",
        GST_STR_NULL (user_choice), GST_STR_NULL (platform_choice));

    return g_object_new (GST_TYPE_GL_DISPLAY, NULL);
  }

  return display;
}

guintptr
gst_gl_display_get_handle (GstGLDisplay * display)
{
  GstGLDisplayClass *klass;

  g_return_val_if_fail (GST_IS_GL_DISPLAY (display), 0);
  klass = GST_GL_DISPLAY_GET_CLASS (display);
  g_return_val_if_fail (klass->get_handle != NULL, 0);

  return klass->get_handle (display);
}

static guintptr
gst_gl_display_default_get_handle (GstGLDisplay * display)
{
  return 0;
}

/**
 * gst_gl_display_get_handle_type:
 * @display: a #GstGLDisplay
 *
 * Returns: the #GstGLDisplayType of @display
 */
GstGLDisplayType
gst_gl_display_get_handle_type (GstGLDisplay * display)
{
  g_return_val_if_fail (GST_IS_GL_DISPLAY (display), GST_GL_DISPLAY_TYPE_NONE);

  return display->type;
}

/**
 * gst_context_set_gl_display:
 * @context: a #GstContext
 * @display: resulting #GstGLDisplay
 *
 * Sets @display on @context
 */
void
gst_context_set_gl_display (GstContext * context, GstGLDisplay * display)
{
  GstStructure *s;

  g_return_if_fail (context != NULL);

  GST_CAT_LOG (gst_context, "setting GstGLDisplay(%p) on context(%p)", display,
      context);

  s = gst_context_writable_structure (context);
  gst_structure_set (s, GST_GL_DISPLAY_CONTEXT_TYPE, GST_TYPE_GL_DISPLAY,
      display, NULL);
}

/**
 * gst_context_get_gl_display:
 * @context: a #GstContext
 * @display: resulting #GstGLDisplay
 *
 * Returns: Whether @display was in @context
 */
gboolean
gst_context_get_gl_display (GstContext * context, GstGLDisplay ** display)
{
  const GstStructure *s;
  gboolean ret;

  g_return_val_if_fail (display != NULL, FALSE);
  g_return_val_if_fail (context != NULL, FALSE);

  s = gst_context_get_structure (context);
  ret = gst_structure_get (s, GST_GL_DISPLAY_CONTEXT_TYPE,
      GST_TYPE_GL_DISPLAY, display, NULL);

  GST_CAT_LOG (gst_context, "got GstGLDisplay(%p) from context(%p)", *display,
      context);

  return ret;
}
