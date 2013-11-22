/*
 *  gstvaapivalue.c - GValue implementations specific to VA-API
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2012-2013 Intel Corporation
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
 * SECTION:gstvaapivalue
 * @short_description: GValue implementations specific to VA-API
 */

#include "sysdeps.h"
#include <gobject/gvaluecollector.h>
#include "gstvaapivalue.h"

static gpointer
default_copy_func(gpointer data)
{
    return data;
}

static void
default_free_func(gpointer data)
{
}

/* --- GstVaapiPoint --- */

GType
gst_vaapi_point_get_type(void)
{
    static volatile gsize g_type = 0;

    if (g_once_init_enter(&g_type)) {
        GType type = g_boxed_type_register_static(
            g_intern_static_string("GstVaapiPoint"),
            default_copy_func, default_free_func);
        g_once_init_leave(&g_type, type);
    }
    return g_type;
}

/* --- GstVaapiRectangle --- */

GType
gst_vaapi_rectangle_get_type(void)
{
    static volatile gsize g_type = 0;

    if (g_once_init_enter(&g_type)) {
        GType type = g_boxed_type_register_static(
            g_intern_static_string("GstVaapiRectangle"),
            default_copy_func, default_free_func);
        g_once_init_leave(&g_type, type);
    }
    return g_type;
}

/* --- GstVaapiRenderMode --- */

GType
gst_vaapi_render_mode_get_type(void)
{
    static GType render_mode_type = 0;

    static const GEnumValue render_modes[] = {
        { GST_VAAPI_RENDER_MODE_OVERLAY,
          "Overlay render mode", "overlay" },
        { GST_VAAPI_RENDER_MODE_TEXTURE,
          "Textured-blit render mode", "texture" },
        { 0, NULL, NULL }
    };

    if (!render_mode_type) {
        render_mode_type =
            g_enum_register_static("GstVaapiRenderMode", render_modes);
    }
    return render_mode_type;
}

/* --- GstVaapiRotation --- */

GType
gst_vaapi_rotation_get_type(void)
{
    static GType g_type = 0;

    static const GEnumValue rotation_values[] = {
        { GST_VAAPI_ROTATION_0,
          "Unrotated mode", "0" },
        { GST_VAAPI_ROTATION_90,
          "Rotated by 90°, clockwise", "90" },
        { GST_VAAPI_ROTATION_180,
          "Rotated by 180°, clockwise", "180" },
        { GST_VAAPI_ROTATION_270,
          "Rotated by 270°, clockwise", "270" },
        { 0, NULL, NULL },
    };

    if (!g_type)
        g_type = g_enum_register_static("GstVaapiRotation", rotation_values);
    return g_type;
}
