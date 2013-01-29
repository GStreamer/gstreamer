/*
 *  test-surfaces.c - Test GstVaapiSurface and GstVaapiSurfacePool
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2012 Intel Corporation
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

#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/gstvaapisurfacepool.h>
#include "output.h"

#define MAX_SURFACES 4

static void
gst_vaapi_object_destroy_cb(gpointer object, gpointer user_data)
{
    g_print("destroying GstVaapiObject %p\n", object);
}

int
main(int argc, char *argv[])
{
    GstVaapiDisplay    *display;
    GstVaapiSurface    *surface;
    GstVaapiID          surface_id;
    GstVaapiSurface    *surfaces[MAX_SURFACES];
    GstVaapiVideoPool  *pool;
    GstCaps            *caps;
    gint                i;

    static const GstVaapiChromaType chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
    static const guint              width       = 320;
    static const guint              height      = 240;

    if (!video_output_init(&argc, argv, NULL))
        g_error("failed to initialize video output subsystem");

    display = video_output_create_display(NULL);
    if (!display)
        g_error("could not create Gst/VA display");

    surface = gst_vaapi_surface_new(display, chroma_type, width, height);
    if (!surface)
        g_error("could not create Gst/VA surface");

    /* This also tests for the GstVaapiParamSpecID */
    g_object_get(G_OBJECT(surface), "id", &surface_id, NULL);
    if (surface_id != gst_vaapi_surface_get_id(surface))
        g_error("could not retrieve the native surface ID");
    g_print("created surface %" GST_VAAPI_ID_FORMAT "\n",
            GST_VAAPI_ID_ARGS(surface_id));

    g_object_unref(surface);

    caps = gst_caps_new_simple(
        GST_VAAPI_SURFACE_CAPS_NAME,
        "type", G_TYPE_STRING, "vaapi",
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        NULL
    );
    if (!caps)
        g_error("cound not create Gst/VA surface caps");

    pool = gst_vaapi_surface_pool_new(display, caps);
    if (!pool)
        g_error("could not create Gst/VA surface pool");

    for (i = 0; i < MAX_SURFACES; i++) {
        surface = gst_vaapi_video_pool_get_object(pool);
        if (!surface)
            g_error("could not allocate Gst/VA surface from pool");
        g_print("created surface %" GST_VAAPI_ID_FORMAT " from pool\n",
                GST_VAAPI_ID_ARGS(gst_vaapi_surface_get_id(surface)));
        surfaces[i] = surface;
    }

    /* Check the pool doesn't return the last free'd surface */
    surface = g_object_ref(surfaces[1]);

    for (i = 0; i < 2; i++)
        gst_vaapi_video_pool_put_object(pool, surfaces[i]);

    for (i = 0; i < 2; i++) {
        surfaces[i] = gst_vaapi_video_pool_get_object(pool);
        if (!surfaces[i])
            g_error("could not re-allocate Gst/VA surface%d from pool", i);
        g_print("created surface %" GST_VAAPI_ID_FORMAT " from pool (realloc)\n",
                GST_VAAPI_ID_ARGS(gst_vaapi_surface_get_id(surfaces[i])));
    }

    if (surface == surfaces[0])
        g_error("Gst/VA pool doesn't queue free surfaces");

    for (i = MAX_SURFACES - 1; i >= 0; i--) {
        if (!surfaces[i])
            continue;
        gst_vaapi_video_pool_put_object(pool, surfaces[i]);
        surfaces[i] = NULL;
    }

    g_signal_connect(
        G_OBJECT(surface),
        "destroy",
        G_CALLBACK(gst_vaapi_object_destroy_cb), NULL
    );

    /* Unref in random order to check objects are correctly refcounted */
    g_print("unref display\n");
    g_object_unref(display);
    gst_caps_unref(caps);
    g_print("unref pool\n");
    g_object_unref(pool);
    g_print("unref surface\n");
    g_object_unref(surface);
    video_output_exit();
    return 0;
}
