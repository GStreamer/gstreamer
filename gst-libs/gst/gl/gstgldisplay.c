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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gl.h"
#include "gstgldisplay.h"

GST_DEBUG_CATEGORY_STATIC (gst_gl_display_debug);
#define GST_CAT_DEFAULT gst_gl_display_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_display_debug, "gldisplay", 0, "opengl display");

G_DEFINE_TYPE_WITH_CODE (GstGLDisplay, gst_gl_display, G_TYPE_OBJECT,
    DEBUG_INIT);

#define GST_GL_DISPLAY_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_TYPE_GL_DISPLAY, GstGLDisplayPrivate))

static void gst_gl_display_finalize (GObject * object);

struct _GstGLDisplayPrivate
{
  gint dummy;
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

  display->gl_vtable = g_slice_alloc0 (sizeof (GstGLFuncs));

  display->gl_api = GST_GL_API_NONE;

  gst_gl_memory_init ();
}

static void
gst_gl_display_finalize (GObject * object)
{
  GstGLDisplay *display = GST_GL_DISPLAY (object);

  if (display->gl_vtable) {
    g_slice_free (GstGLFuncs, display->gl_vtable);
    display->gl_vtable = NULL;
  }

  if (display->window) {
    gst_object_unref (display->window);
    display->window = NULL;
  }

  G_OBJECT_CLASS (gst_gl_display_parent_class)->finalize (object);
}

typedef struct
{
  GstGLDisplay *display;
  GstGLDisplayThreadFunc func;
  gpointer data;
} RunGenericData;

static void
_gst_gl_display_thread_run_generic (RunGenericData * data)
{
  GST_TRACE ("running function:%p data:%p", data->func, data->data);

  data->func (data->display, data->data);
  g_object_unref (data->display);
  g_slice_free (RunGenericData, data);
}

GstGLDisplay *
gst_gl_display_new (void)
{
  return g_object_new (GST_TYPE_GL_DISPLAY, NULL);
}

void
gst_gl_display_thread_add (GstGLDisplay * display,
    GstGLDisplayThreadFunc func, gpointer data)
{
  RunGenericData *rdata;

  g_return_if_fail (GST_IS_GL_DISPLAY (display));
  g_return_if_fail (GST_GL_IS_WINDOW (display->window));
  g_return_if_fail (func != NULL);

  rdata = g_slice_new (RunGenericData);
  rdata->display = g_object_ref (display);
  rdata->data = data;
  rdata->func = func;

  gst_gl_window_send_message (display->window,
      GST_GL_WINDOW_CB (_gst_gl_display_thread_run_generic), rdata);
}

GstGLAPI
gst_gl_display_get_gl_api (GstGLDisplay * display)
{
  g_return_val_if_fail (GST_IS_GL_DISPLAY (display), GST_GL_API_NONE);
  g_return_val_if_fail (GST_GL_IS_WINDOW (display->window), GST_GL_API_NONE);

  return gst_gl_window_get_gl_api (display->window);
}

gpointer
gst_gl_display_get_gl_vtable (GstGLDisplay * display)
{
  g_return_val_if_fail (GST_IS_GL_DISPLAY (display), NULL);

  return display->gl_vtable;
}

void
gst_gl_display_set_window (GstGLDisplay * display, GstGLWindow * window)
{
  g_return_if_fail (GST_IS_GL_DISPLAY (display));
  g_return_if_fail (GST_GL_IS_WINDOW (window));

  gst_gl_display_lock (display);

  if (display->window)
    gst_object_unref (display->window);

  display->window = gst_object_ref (window);

  gst_gl_display_unlock (display);
}

GstGLWindow *
gst_gl_display_get_window (GstGLDisplay * display)
{
  GstGLWindow *window;

  g_return_val_if_fail (GST_IS_GL_DISPLAY (display), NULL);

  gst_gl_display_lock (display);

  window = display->window ? gst_object_ref (display->window) : NULL;

  gst_gl_display_unlock (display);

  return window;
}

GstGLWindow *
gst_gl_display_get_window_unlocked (GstGLDisplay * display)
{
  g_return_val_if_fail (GST_IS_GL_DISPLAY (display), NULL);

  return display->window ? gst_object_ref (display->window) : NULL;
}

void
gst_context_set_gl_display (GstContext * context, GstGLDisplay * display)
{
  GstStructure *s;

  s = gst_context_writable_structure (context);
  gst_structure_set (s, GST_GL_DISPLAY_CONTEXT_TYPE, GST_TYPE_GL_DISPLAY,
      display, NULL);
}

gboolean
gst_context_get_gl_display (GstContext * context, GstGLDisplay ** display)
{
  const GstStructure *s;

  s = gst_context_get_structure (context);
  return gst_structure_get (s, GST_GL_DISPLAY_CONTEXT_TYPE,
      GST_TYPE_GL_DISPLAY, display, NULL);
}
