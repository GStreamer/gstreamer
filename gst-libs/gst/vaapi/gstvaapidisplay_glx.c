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
#include "gstvaapiwindow_glx.h"
#include "gstvaapitexture_glx.h"

#define DEBUG 1
#include "gstvaapidebug.h"

static GstVaapiWindow *
gst_vaapi_display_glx_create_window (GstVaapiDisplay * display, GstVaapiID id,
    guint width, guint height)
{
  return id != GST_VAAPI_ID_INVALID ?
      gst_vaapi_window_glx_new_with_xid (display, id) :
      gst_vaapi_window_glx_new (display, width, height);
}

static void
ensure_texture_map (GstVaapiDisplayGLX * display)
{
  if (!display->texture_map)
    display->texture_map = gst_vaapi_texture_map_new ();
}

static GstVaapiTexture *
gst_vaapi_display_glx_create_texture (GstVaapiDisplay * display, GstVaapiID id,
    guint target, guint format, guint width, guint height)
{
  GstVaapiTexture *texture;
  GstVaapiDisplayGLX *dpy = GST_VAAPI_DISPLAY_GLX (display);

  if (id == GST_VAAPI_ID_INVALID)
    return gst_vaapi_texture_glx_new (display, target, format, width, height);

  ensure_texture_map (dpy);
  if (!(texture = gst_vaapi_texture_map_lookup (dpy->texture_map, id))) {
    if ((texture =
            gst_vaapi_texture_glx_new_wrapped (display, id, target, format))) {
      gst_vaapi_texture_map_add (dpy->texture_map, texture, id);
    }
  }

  return texture;
}

static GstVaapiTextureMap *
gst_vaapi_display_glx_get_texture_map (GstVaapiDisplay * display)
{
  return GST_VAAPI_DISPLAY_GLX (display)->texture_map;
}

static void
gst_vaapi_display_glx_finalize (GstVaapiDisplay * display)
{
  GstVaapiDisplayGLX *dpy = GST_VAAPI_DISPLAY_GLX (display);

  if (dpy->texture_map)
    gst_object_unref (dpy->texture_map);
  GST_VAAPI_DISPLAY_GLX_GET_CLASS (display)->parent_finalize (display);
}

static void
gst_vaapi_display_glx_class_init (GstVaapiDisplayGLXClass * klass)
{
  GstVaapiMiniObjectClass *const object_class =
      GST_VAAPI_MINI_OBJECT_CLASS (klass);
  GstVaapiDisplayClass *const dpy_class = GST_VAAPI_DISPLAY_CLASS (klass);

  gst_vaapi_display_x11_class_init (&klass->parent_class);

  /* chain up destructor */
  klass->parent_finalize = object_class->finalize;
  object_class->finalize = (GDestroyNotify) gst_vaapi_display_glx_finalize;

  object_class->size = sizeof (GstVaapiDisplayGLX);
  dpy_class->display_type = GST_VAAPI_DISPLAY_TYPE_GLX;
  dpy_class->create_window = gst_vaapi_display_glx_create_window;
  dpy_class->create_texture = gst_vaapi_display_glx_create_texture;
  dpy_class->get_texture_map = gst_vaapi_display_glx_get_texture_map;
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
