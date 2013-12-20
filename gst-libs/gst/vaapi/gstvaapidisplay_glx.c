/*
 *  gstvaapidisplay_glx.c - VA/GLX display abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:gstvaapidisplay_glx
 * @short_description: VA/GLX display abstraction
 */

#include "sysdeps.h"
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"
#include "gstvaapiutils_glx.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapidisplay_x11_priv.h"
#include "gstvaapidisplay_glx.h"
#include "gstvaapidisplay_glx_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

static const guint g_display_types = 1U << GST_VAAPI_DISPLAY_TYPE_GLX;

static gboolean
gst_vaapi_display_glx_get_display_info (GstVaapiDisplay * display,
    GstVaapiDisplayInfo * info)
{
  const GstVaapiDisplayGLXClass *const klass =
      GST_VAAPI_DISPLAY_GLX_GET_CLASS (display);

  info->va_display = vaGetDisplayGLX (GST_VAAPI_DISPLAY_XDISPLAY (display));
  if (!info->va_display)
    return FALSE;
  info->display_type = GST_VAAPI_DISPLAY_TYPE_GLX;
  return klass->parent_get_display (display, info);
}

static void
gst_vaapi_display_glx_class_init (GstVaapiDisplayGLXClass * klass)
{
  GstVaapiMiniObjectClass *const object_class =
      GST_VAAPI_MINI_OBJECT_CLASS (klass);
  GstVaapiDisplayClass *const dpy_class = GST_VAAPI_DISPLAY_CLASS (klass);

  gst_vaapi_display_x11_class_init (&klass->parent_class);

  object_class->size = sizeof (GstVaapiDisplayGLX);
  klass->parent_get_display = dpy_class->get_display;
  dpy_class->display_types = g_display_types;
  dpy_class->get_display = gst_vaapi_display_glx_get_display_info;
}

static inline const GstVaapiDisplayClass *
gst_vaapi_display_glx_class (void)
{
  static GstVaapiDisplayGLXClass g_class;
  static gsize g_class_init = FALSE;

  if (g_once_init_enter (&g_class_init)) {
    gst_vaapi_display_glx_class_init (&g_class);
    g_once_init_leave (&g_class_init, TRUE);
  }
  return GST_VAAPI_DISPLAY_CLASS (&g_class);
}

/**
 * gst_vaapi_display_glx_new:
 * @display_name: the X11 display name
 *
 * Opens an X11 #Display using @display_name and returns a newly
 * allocated #GstVaapiDisplay object. The X11 display will be cloed
 * when the reference count of the object reaches zero.
 *
 * Return value: a newly allocated #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_display_glx_new (const gchar * display_name)
{
  return gst_vaapi_display_new (gst_vaapi_display_glx_class (),
      GST_VAAPI_DISPLAY_INIT_FROM_DISPLAY_NAME, (gpointer) display_name);
}

/**
 * gst_vaapi_display_glx_new_with_display:
 * @x11_display: an X11 #Display
 *
 * Creates a #GstVaapiDisplay based on the X11 @x11_display
 * display. The caller still owns the display and must call
 * XCloseDisplay() when all #GstVaapiDisplay references are
 * released. Doing so too early can yield undefined behaviour.
 *
 * Return value: a newly allocated #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_display_glx_new_with_display (Display * x11_display)
{
  g_return_val_if_fail (x11_display != NULL, NULL);

  return gst_vaapi_display_new (gst_vaapi_display_glx_class (),
      GST_VAAPI_DISPLAY_INIT_FROM_NATIVE_DISPLAY, x11_display);
}
