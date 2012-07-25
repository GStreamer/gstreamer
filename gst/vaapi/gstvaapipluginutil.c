/*
 *  gstvaapipluginutil.h - VA-API plugin helpers
 *
 *  Copyright (C) 2011-2012 Intel Corporation
 *  Copyright (C) 2011 Collabora
 *    Author: Nicolas Dufresne <nicolas.dufresne@collabora.co.uk>
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <string.h>
#include <gst/video/videocontext.h>
#if USE_X11
# include <gst/vaapi/gstvaapidisplay_x11.h>
#endif
#if USE_GLX
# include <gst/vaapi/gstvaapidisplay_glx.h>
#endif
#include "gstvaapipluginutil.h"

/* Preferred first */
static const char *display_types[] = {
    "gst-vaapi-display",
    "vaapi-display",
    "x11-display",
    "x11-display-name",
    NULL
};

typedef struct {
    const gchar        *type_str;
    GstVaapiDisplayType type;
    GstVaapiDisplay * (*create_display)(const gchar *);
} DisplayMap;

static const DisplayMap g_display_map[] = {
#if USE_GLX
    { "glx",
      GST_VAAPI_DISPLAY_TYPE_GLX,
      gst_vaapi_display_glx_new },
#endif
#if USE_X11
    { "x11",
      GST_VAAPI_DISPLAY_TYPE_X11,
      gst_vaapi_display_x11_new },
#endif
    { NULL, }
};

gboolean
gst_vaapi_ensure_display(
    gpointer             element,
    GstVaapiDisplayType  display_type,
    GstVaapiDisplay    **display_ptr
)
{
    GstVaapiDisplay *display;
    GstVideoContext *context;
    const DisplayMap *m;

    g_return_val_if_fail(GST_IS_VIDEO_CONTEXT(element), FALSE);
    g_return_val_if_fail(display_ptr != NULL, FALSE);

    /* Already exist ? */
    display = *display_ptr;
    if (display)
        return TRUE;

    context = GST_VIDEO_CONTEXT(element);
    g_return_val_if_fail(context != NULL, FALSE);

    gst_video_context_prepare(context, display_types);

    /* Neighbour found and it updated the display */
    if (*display_ptr)
        return TRUE;

    /* If no neighboor, or application not interested, use system default */
    for (m = g_display_map; m->type_str != NULL; m++) {
        if (display_type != GST_VAAPI_DISPLAY_TYPE_ANY &&
            display_type != m->type)
            continue;

        display = m->create_display(NULL);
        if (display) {
            /* FIXME: allocator should return NULL if an error occurred */
            if (gst_vaapi_display_get_display(display)) {
                display_type = m->type;
                break;
            }
            g_object_unref(display);
            display = NULL;
        }

        if (display_type != GST_VAAPI_DISPLAY_TYPE_ANY)
            break;
    }

    if (display_ptr)
        *display_ptr = display;
    return display != NULL;
}

void
gst_vaapi_set_display(
    const gchar      *type,
    const GValue     *value,
    GstVaapiDisplay **display
)
{
    GstVaapiDisplay *dpy = NULL;

    if (!strcmp(type, "x11-display-name")) {
        g_return_if_fail(G_VALUE_HOLDS_STRING(value));
#if USE_GLX
        dpy = gst_vaapi_display_glx_new(g_value_get_string(value));
#endif
        if (!dpy)
            dpy = gst_vaapi_display_x11_new(g_value_get_string(value));
    }
    else if (!strcmp(type, "x11-display")) {
        g_return_if_fail(G_VALUE_HOLDS_POINTER(value));
#if USE_GLX
        dpy = gst_vaapi_display_glx_new_with_display(g_value_get_pointer(value));
#endif
        if (!dpy)
            dpy = gst_vaapi_display_x11_new_with_display(g_value_get_pointer(value));
    }
    else if (!strcmp(type, "vaapi-display")) {
        g_return_if_fail(G_VALUE_HOLDS_POINTER(value));
        dpy = gst_vaapi_display_new_with_display(g_value_get_pointer(value));
    }
    else if (!strcmp(type, "gst-vaapi-display")) {
        g_return_if_fail(G_VALUE_HOLDS_OBJECT(value));
        dpy = g_value_dup_object(value);
    }

    if (dpy) {
        if (*display)
            g_object_unref(*display);
        *display = dpy;
    }
}

gboolean
gst_vaapi_reply_to_query(GstQuery *query, GstVaapiDisplay *display)
{
    GstVaapiDisplayType display_type;
    const gchar **types;
    const gchar *type;
    gint i;
    gboolean res = FALSE;

    if (!display)
        return FALSE;

    types = gst_video_context_query_get_supported_types(query);

    if (!types)
        return FALSE;

    display_type = gst_vaapi_display_get_display_type(display);
    for (i = 0; types[i] && !res; i++) {
        type = types[i];

        res = TRUE;
        if (!strcmp(type, "gst-vaapi-display")) {
            gst_video_context_query_set_object(query, type, G_OBJECT(display));
        }
        else if (!strcmp(type, "vaapi-display")) {
            VADisplay vadpy = gst_vaapi_display_get_display(display);
            gst_video_context_query_set_pointer(query, type, vadpy);
        }
        else {
            switch (display_type) {
#if USE_GLX
            case GST_VAAPI_DISPLAY_TYPE_GLX:
#endif
            case GST_VAAPI_DISPLAY_TYPE_X11: {
                GstVaapiDisplayX11 * const xvadpy =
                    GST_VAAPI_DISPLAY_X11(display);
                Display * const x11dpy =
                    gst_vaapi_display_x11_get_display(xvadpy);
                if (!strcmp(type, "x11-display"))
                    gst_video_context_query_set_pointer(query, type, x11dpy);
                else if (!strcmp(type, "x11-display-name"))
                    gst_video_context_query_set_string(query, type,
                        DisplayString(x11dpy));
                else
                    res = FALSE;
                break;
            }
            default:
                res = FALSE;
                break;
            }
        }
    }
    return res;
}

gboolean
gst_vaapi_append_surface_caps(GstCaps *out_caps, GstCaps *in_caps)
{
    GstStructure *structure;
    const GValue *v_width, *v_height, *v_framerate, *v_par;
    guint i, n_structures;

    structure   = gst_caps_get_structure(in_caps, 0);
    v_width     = gst_structure_get_value(structure, "width");
    v_height    = gst_structure_get_value(structure, "height");
    v_framerate = gst_structure_get_value(structure, "framerate");
    v_par       = gst_structure_get_value(structure, "pixel-aspect-ratio");
    if (!v_width || !v_height)
        return FALSE;

    n_structures = gst_caps_get_size(out_caps);
    for (i = 0; i < n_structures; i++) {
        structure = gst_caps_get_structure(out_caps, i);
        gst_structure_set_value(structure, "width", v_width);
        gst_structure_set_value(structure, "height", v_height);
        if (v_framerate)
            gst_structure_set_value(structure, "framerate", v_framerate);
        if (v_par)
            gst_structure_set_value(structure, "pixel-aspect-ratio", v_par);
    }
    return TRUE;
}
