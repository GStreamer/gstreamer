/*
 * GStreamer
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

#include <gst/gl/x11/gstgldisplay_x11.h>

GST_DEBUG_CATEGORY_STATIC (gst_gl_display_debug);
#define GST_CAT_DEFAULT gst_gl_display_debug

G_DEFINE_TYPE (GstGLDisplayX11, gst_gl_display_x11, GST_TYPE_GL_DISPLAY);

static void gst_gl_display_x11_finalize (GObject * object);
static guintptr gst_gl_display_x11_get_handle (GstGLDisplay * display);

static void
gst_gl_display_x11_class_init (GstGLDisplayX11Class * klass)
{
  GST_GL_DISPLAY_CLASS (klass)->get_handle =
      GST_DEBUG_FUNCPTR (gst_gl_display_x11_get_handle);

  G_OBJECT_CLASS (klass)->finalize = gst_gl_display_x11_finalize;
}

static void
gst_gl_display_x11_init (GstGLDisplayX11 * display_x11)
{
  GstGLDisplay *display = (GstGLDisplay *) display_x11;

  display->type = GST_GL_DISPLAY_TYPE_X11;
  display_x11->foreign_display = FALSE;
}

static void
gst_gl_display_x11_finalize (GObject * object)
{
  GstGLDisplayX11 *display_x11 = GST_GL_DISPLAY_X11 (object);

  if (display_x11->name)
    g_free (display_x11->name);

  if (!display_x11->foreign_display && display_x11->display) {
    XCloseDisplay (display_x11->display);
  }

  G_OBJECT_CLASS (gst_gl_display_x11_parent_class)->finalize (object);
}

/**
 * gst_gl_display_x11_new:
 * @name: (allow-none): a display name
 *
 * Create a new #GstGLDisplayX11 from the x11 display name.  See XOpenDisplay()
 * for details on what is a valid name.
 *
 * Returns: (transfer full): a new #GstGLDisplayX11 or %NULL
 */
GstGLDisplayX11 *
gst_gl_display_x11_new (const gchar * name)
{
  GstGLDisplayX11 *ret;

  GST_DEBUG_CATEGORY_GET (gst_gl_display_debug, "gldisplay");

  ret = g_object_new (GST_TYPE_GL_DISPLAY_X11, NULL);
  ret->name = g_strdup (name);
  ret->display = XOpenDisplay (ret->name);

  if (!ret->display) {
    GST_ERROR ("Failed to open X11 display connection with name, \'%s\'", name);
  }

  return ret;
}

/**
 * gst_gl_display_x11_new_with_display:
 * @display: an existing, x11 display
 *
 * Creates a new display connection from a X11 Display.
 *
 * Returns: (transfer full): a new #GstGLDisplayX11
 */
GstGLDisplayX11 *
gst_gl_display_x11_new_with_display (Display * display)
{
  GstGLDisplayX11 *ret;

  g_return_val_if_fail (display != NULL, NULL);

  GST_DEBUG_CATEGORY_GET (gst_gl_display_debug, "gldisplay");

  ret = g_object_new (GST_TYPE_GL_DISPLAY_X11, NULL);

  ret->name = g_strdup (DisplayString (display));
  ret->display = display;
  ret->foreign_display = TRUE;

  return ret;
}

static guintptr
gst_gl_display_x11_get_handle (GstGLDisplay * display)
{
  return (guintptr) GST_GL_DISPLAY_X11 (display)->display;
}
