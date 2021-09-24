/*
 *  test-surfaces.c - Test GstVaapiSurface and GstVaapiSurfacePool
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

#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/gstvaapisurfacepool.h>
#include "output.h"

#define MAX_SURFACES 4

int
main (int argc, char *argv[])
{
  GstVaapiDisplay *display;
  GstVaapiSurface *surface;
  GstVaapiID surface_id;
  GstVaapiSurface *surfaces[MAX_SURFACES];
  GstVaapiVideoPool *pool;
  gint i;

  static const GstVaapiChromaType chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
  static const guint width = 320;
  static const guint height = 240;

  if (!video_output_init (&argc, argv, NULL))
    g_error ("failed to initialize video output subsystem");

  display = video_output_create_display (NULL);
  if (!display)
    g_error ("could not create Gst/VA display");

  surface = gst_vaapi_surface_new (display, chroma_type, width, height);
  if (!surface)
    g_error ("could not create Gst/VA surface");

  surface_id = gst_vaapi_surface_get_id (surface);
  g_print ("created surface %" GST_VAAPI_ID_FORMAT "\n",
      GST_VAAPI_ID_ARGS (surface_id));

  gst_vaapi_surface_unref (surface);

  pool = gst_vaapi_surface_pool_new (display, GST_VIDEO_FORMAT_ENCODED,
      width, height, 0);
  if (!pool)
    g_error ("could not create Gst/VA surface pool");

  for (i = 0; i < MAX_SURFACES; i++) {
    surface = gst_vaapi_video_pool_get_object (pool);
    if (!surface)
      g_error ("could not allocate Gst/VA surface from pool");
    g_print ("created surface %" GST_VAAPI_ID_FORMAT " from pool\n",
        GST_VAAPI_ID_ARGS (gst_vaapi_surface_get_id (surface)));
    surfaces[i] = surface;
  }

  /* Check the pool doesn't return the last free'd surface */
  surface = (GstVaapiSurface *)
      gst_mini_object_ref (GST_MINI_OBJECT_CAST (surfaces[1]));

  for (i = 0; i < 2; i++)
    gst_vaapi_video_pool_put_object (pool, surfaces[i]);

  for (i = 0; i < 2; i++) {
    surfaces[i] = gst_vaapi_video_pool_get_object (pool);
    if (!surfaces[i])
      g_error ("could not re-allocate Gst/VA surface%d from pool", i);
    g_print ("created surface %" GST_VAAPI_ID_FORMAT " from pool (realloc)\n",
        GST_VAAPI_ID_ARGS (gst_vaapi_surface_get_id (surfaces[i])));
  }

  if (surface == surfaces[0])
    g_error ("Gst/VA pool doesn't queue free surfaces");

  for (i = MAX_SURFACES - 1; i >= 0; i--) {
    if (!surfaces[i])
      continue;
    gst_vaapi_video_pool_put_object (pool, surfaces[i]);
    surfaces[i] = NULL;
  }

  /* Unref in random order to check objects are correctly refcounted */
  gst_object_unref (display);
  gst_vaapi_video_pool_unref (pool);
  gst_vaapi_surface_unref (surface);
  video_output_exit ();
  return 0;
}
