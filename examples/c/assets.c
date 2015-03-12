/* GStreamer Editing Services
 * Copyright (C) 2012 Volodymyr Rudyi<vladimir.rudoy@gmail.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <ges/ges.h>
#include <ges/ges-uri-asset.h>
#include <gst/pbutils/encoding-profile.h>
#include <gst/pbutils/gstdiscoverer.h>
#include <ges/ges-internal.h>

static void
asset_loaded_cb (GObject * source, GAsyncResult * res, GMainLoop * mainloop)
{
  GESUriClipAsset *mfs =
      GES_URI_CLIP_ASSET (ges_asset_request_finish (res, NULL));
  GstDiscovererInfo *discoverer_info = NULL;
  discoverer_info = ges_uri_clip_asset_get_info (mfs);

  GST_DEBUG ("Result is %d", gst_discoverer_info_get_result (discoverer_info));
  GST_DEBUG ("Info type is %s", G_OBJECT_TYPE_NAME (mfs));
  GST_DEBUG ("Duration is %" GST_TIME_FORMAT,
      GST_TIME_ARGS (ges_uri_clip_asset_get_duration (mfs)));

  gst_object_unref (mfs);

  g_main_loop_quit (mainloop);
}

int
main (int argc, gchar ** argv)
{
  GMainLoop *mainloop;

  if (argc != 2) {
    return 1;
  }
  /* Initialize GStreamer (this will parse environment variables and commandline
   * arguments. */
  gst_init (NULL, NULL);

  /* Initialize the GStreamer Editing Services */
  ges_init ();

  /* ... and we start a GMainLoop. GES **REQUIRES** a GMainLoop to be running in
   * order to function properly ! */
  mainloop = g_main_loop_new (NULL, FALSE);

  ges_asset_request_async (GES_TYPE_URI_CLIP, argv[1], NULL,
      (GAsyncReadyCallback) asset_loaded_cb, mainloop);

  g_main_loop_run (mainloop);
  g_main_loop_unref (mainloop);

  return 0;
}
