/*
 *  gstvaapidisplay_glx.c - VA/GLX display abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2012 Intel Corporation
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

G_DEFINE_TYPE(GstVaapiDisplayGLX,
              gst_vaapi_display_glx,
              GST_VAAPI_TYPE_DISPLAY_X11);

static void
gst_vaapi_display_glx_finalize(GObject *object)
{
    G_OBJECT_CLASS(gst_vaapi_display_glx_parent_class)->finalize(object);
}

static gboolean
gst_vaapi_display_glx_get_display_info(
    GstVaapiDisplay     *display,
    GstVaapiDisplayInfo *info
)
{
    GstVaapiDisplayClass * const dpy_class =
        GST_VAAPI_DISPLAY_CLASS(gst_vaapi_display_glx_parent_class);

    info->va_display = vaGetDisplayGLX(GST_VAAPI_DISPLAY_XDISPLAY(display));
    if (!info->va_display)
        return FALSE;
    info->display_type = GST_VAAPI_DISPLAY_TYPE_GLX;
    return dpy_class->get_display(display, info);
}

static void
gst_vaapi_display_glx_class_init(GstVaapiDisplayGLXClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiDisplayClass * const dpy_class = GST_VAAPI_DISPLAY_CLASS(klass);

    object_class->finalize      = gst_vaapi_display_glx_finalize;
    dpy_class->get_display      = gst_vaapi_display_glx_get_display_info;
}

static void
gst_vaapi_display_glx_init(GstVaapiDisplayGLX *display)
{
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
gst_vaapi_display_glx_new(const gchar *display_name)
{
    return g_object_new(GST_VAAPI_TYPE_DISPLAY_GLX,
                        "display-name", display_name,
                        NULL);
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
gst_vaapi_display_glx_new_with_display(Display *x11_display)
{
    return g_object_new(GST_VAAPI_TYPE_DISPLAY_GLX,
                        "x11-display", x11_display,
                        NULL);
}
