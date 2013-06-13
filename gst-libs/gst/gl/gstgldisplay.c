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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include "gstgldownload.h"
#include "gstglmemory.h"
#include "gstglfeature.h"
#include "gstglapi.h"

#include "gstgldisplay.h"

#define USING_OPENGL(display) (display->gl_api & GST_GL_API_OPENGL)
#define USING_OPENGL3(display) (display->gl_api & GST_GL_API_OPENGL3)
#define USING_GLES(display) (display->gl_api & GST_GL_API_GLES)
#define USING_GLES2(display) (display->gl_api & GST_GL_API_GLES2)
#define USING_GLES3(display) (display->gl_api & GST_GL_API_GLES3)

GST_DEBUG_CATEGORY_STATIC (gst_gl_display_debug);
#define GST_CAT_DEFAULT gst_gl_display_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_display_debug, "gldisplay", 0, "opengl display");

G_DEFINE_TYPE_WITH_CODE (GstGLDisplay, gst_gl_display, G_TYPE_OBJECT,
    DEBUG_INIT);

#define GST_GL_DISPLAY_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_DISPLAY, GstGLDisplayPrivate))

static void gst_gl_display_finalize (GObject * object);

/* Called in the gl thread, protected by lock and unlock */
gpointer gst_gl_display_thread_create_context (GstGLDisplay * display);
void gst_gl_display_thread_destroy_context (GstGLDisplay * display);
void gst_gl_display_thread_run_generic (GstGLDisplay * display);

struct _GstGLDisplayPrivate
{
  GstGLWindow *window;

  /* generic gl code */
  GstGLDisplayThreadFunc generic_callback;
  gpointer data;
};

/*------------------------------------------------------------
  --------------------- For klass GstGLDisplay ---------------
  ----------------------------------------------------------*/
static void
gst_gl_display_class_init (GstGLDisplayClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstGLDisplayPrivate));

  G_OBJECT_CLASS (klass)->finalize = gst_gl_display_finalize;
}


static void
gst_gl_display_init (GstGLDisplay * display)
{
  display->priv = GST_GL_DISPLAY_GET_PRIVATE (display);

  /* thread safe */
  g_mutex_init (&display->mutex);

  display->gl_vtable = g_slice_alloc0 (sizeof (GstGLFuncs));

  gst_gl_memory_init ();
}

static void
gst_gl_display_finalize (GObject * object)
{
  GstGLDisplay *display = GST_GL_DISPLAY (object);

  g_mutex_clear (&display->mutex);

  if (display->error_message) {
    g_free (display->error_message);
    display->error_message = NULL;
  }

  if (display->gl_vtable) {
    g_slice_free (GstGLFuncs, display->gl_vtable);
    display->gl_vtable = NULL;
  }

  if (display->priv->window) {
    gst_object_unref (display->priv->window);
    display->priv->window = NULL;
  }

  G_OBJECT_CLASS (gst_gl_display_parent_class)->finalize (object);
}

//------------------------------------------------------------
//------------------ BEGIN GL THREAD PROCS -------------------
//------------------------------------------------------------

/* Called in the gl thread */

void
gst_gl_display_set_error (GstGLDisplay * display, const char *format, ...)
{
  va_list args;

  if (display->error_message)
    g_free (display->error_message);

  va_start (args, format);
  display->error_message = g_strdup_vprintf (format, args);
  va_end (args);

  GST_WARNING ("%s", display->error_message);
}

void
gst_gl_display_thread_run_generic (GstGLDisplay * display)
{
  GST_TRACE ("running function:%p data:%p",
      display->priv->generic_callback, display->priv->data);

  display->priv->generic_callback (display, display->priv->data);
}

/*------------------------------------------------------------
  --------------------- BEGIN PUBLIC -------------------------
  ----------------------------------------------------------*/

void
gst_gl_display_lock (GstGLDisplay * display)
{
  g_mutex_lock (&display->mutex);
}

void
gst_gl_display_unlock (GstGLDisplay * display)
{
  g_mutex_unlock (&display->mutex);
}

/* Called by the first gl element of a video/x-raw-gl flow */
GstGLDisplay *
gst_gl_display_new (void)
{
  return g_object_new (GST_GL_TYPE_DISPLAY, NULL);
}

void
gst_gl_display_thread_add (GstGLDisplay * display,
    GstGLDisplayThreadFunc func, gpointer data)
{
  g_return_if_fail (GST_IS_GL_DISPLAY (display));
  g_return_if_fail (GST_GL_IS_WINDOW (display->priv->window));
  g_return_if_fail (func != NULL);

  gst_gl_display_lock (display);

  display->priv->data = data;
  display->priv->generic_callback = func;

  gst_gl_window_send_message (display->priv->window,
      GST_GL_WINDOW_CB (gst_gl_display_thread_run_generic), display);

  gst_gl_display_unlock (display);
}

guintptr
gst_gl_display_get_internal_gl_context (GstGLDisplay * display)
{
  g_return_val_if_fail (GST_IS_GL_DISPLAY (display), 0);
  g_return_val_if_fail (GST_GL_IS_WINDOW (display->priv->window), 0);

  return gst_gl_window_get_gl_context (display->priv->window);
}

GstGLAPI
gst_gl_display_get_gl_api (GstGLDisplay * display)
{
  g_return_val_if_fail (GST_IS_GL_DISPLAY (display), GST_GL_API_NONE);

  return display->gl_api;
}

gpointer
gst_gl_display_get_gl_vtable (GstGLDisplay * display)
{
  gpointer gl;

  g_return_val_if_fail (GST_IS_GL_DISPLAY (display), NULL);

  gl = display->gl_vtable;

  return gl;
}

void
gst_gl_display_set_window (GstGLDisplay * display, GstGLWindow * window)
{
  g_return_if_fail (GST_IS_GL_DISPLAY (display));
  g_return_if_fail (GST_GL_IS_WINDOW (window));

  gst_gl_display_lock (display);

  if (display->priv->window)
    gst_object_unref (display->priv->window);

  display->priv->window = gst_object_ref (window);

  gst_gl_display_unlock (display);
}

GstGLWindow *
gst_gl_display_get_window (GstGLDisplay * display)
{
  GstGLWindow *window;

  g_return_val_if_fail (GST_IS_GL_DISPLAY (display), NULL);

  gst_gl_display_lock (display);

  window =
      display->priv->window ? gst_object_ref (display->priv->window) : NULL;

  gst_gl_display_unlock (display);

  return window;
}

GstGLWindow *
gst_gl_display_get_window_unlocked (GstGLDisplay * display)
{
  g_return_val_if_fail (GST_IS_GL_DISPLAY (display), NULL);

  return display->priv->window ? gst_object_ref (display->priv->window) : NULL;
}
