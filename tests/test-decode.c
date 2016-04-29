/*
 *  test-decode.c - Test GstVaapiDecoder
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

#include "gst/vaapi/sysdeps.h"
#include <string.h>
#include <gst/vaapi/gstvaapisurface.h>
#include "decoder.h"
#include "output.h"

/* Set to 1 to check display cache works (shared VA display) */
#define CHECK_DISPLAY_CACHE 1

static inline void
pause (void)
{
  g_print ("Press any key to continue...\n");
  getchar ();
}

static gchar *g_codec_str;
static gboolean g_use_pixmap;

static GOptionEntry g_options[] = {
  {"codec", 'c',
        0,
        G_OPTION_ARG_STRING, &g_codec_str,
      "codec to test", NULL},
  {"pixmap", 0,
        0,
        G_OPTION_ARG_NONE, &g_use_pixmap,
      "use render-to-pixmap", NULL},
  {NULL,}
};

int
main (int argc, char *argv[])
{
  GstVaapiDisplay *display, *display2;
  GstVaapiWindow *window;
  GstVaapiPixmap *pixmap = NULL;
  GstVaapiDecoder *decoder;
  GstVaapiSurfaceProxy *proxy;
  GstVaapiSurface *surface;
  const GstVaapiRectangle *crop_rect;

  static const guint win_width = 640;
  static const guint win_height = 480;

  if (!video_output_init (&argc, argv, g_options))
    g_error ("failed to initialize video output subsystem");

  g_print ("Test decode\n");

  display = video_output_create_display (NULL);
  if (!display)
    g_error ("could not create VA display");

  if (CHECK_DISPLAY_CACHE)
    display2 = video_output_create_display (NULL);
  else
    display2 = gst_vaapi_display_ref (display);
  if (!display2)
    g_error ("could not create second VA display");

  window = video_output_create_window (display, win_width, win_height);
  if (!window)
    g_error ("could not create window");

  decoder = decoder_new (display, g_codec_str);
  if (!decoder)
    g_error ("could not create decoder");

  g_print ("Decode %s sample frame\n", decoder_get_codec_name (decoder));

  if (!decoder_put_buffers (decoder))
    g_error ("could not fill decoder with sample data");

  proxy = decoder_get_surface (decoder);
  if (!proxy)
    g_error ("could not get decoded surface");

  surface = gst_vaapi_surface_proxy_get_surface (proxy);
  crop_rect = gst_vaapi_surface_proxy_get_crop_rect (proxy);

  gst_vaapi_window_show (window);

  if (g_use_pixmap) {
    guint width, height;

    if (crop_rect) {
      width = crop_rect->width;
      height = crop_rect->height;
    } else
      gst_vaapi_surface_get_size (surface, &width, &height);

    pixmap = video_output_create_pixmap (display, GST_VIDEO_FORMAT_xRGB,
        width, height);
    if (!pixmap)
      g_error ("could not create pixmap");

    if (!gst_vaapi_pixmap_put_surface (pixmap, surface, crop_rect,
            GST_VAAPI_PICTURE_STRUCTURE_FRAME))
      g_error ("could not render to pixmap");

    if (!gst_vaapi_window_put_pixmap (window, pixmap, NULL, NULL))
      g_error ("could not render pixmap");
  } else if (!gst_vaapi_window_put_surface (window, surface, crop_rect, NULL,
          GST_VAAPI_PICTURE_STRUCTURE_FRAME))
    g_error ("could not render surface");

  pause ();

  if (pixmap)
    gst_vaapi_pixmap_unref (pixmap);
  gst_vaapi_surface_proxy_unref (proxy);
  gst_vaapi_decoder_unref (decoder);
  gst_vaapi_window_unref (window);
  gst_vaapi_display_unref (display);
  gst_vaapi_display_unref (display2);
  g_free (g_codec_str);
  video_output_exit ();
  return 0;
}
