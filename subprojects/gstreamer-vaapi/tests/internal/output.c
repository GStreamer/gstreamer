/*
 *  output.c - Video output helpers
 *
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

#include "gst/vaapi/sysdeps.h"
#include <gst/gst.h>
#if GST_VAAPI_USE_DRM
# include <gst/vaapi/gstvaapidisplay_drm.h>
# include <gst/vaapi/gstvaapiwindow_drm.h>
#endif
#if GST_VAAPI_USE_X11
# include <gst/vaapi/gstvaapidisplay_x11.h>
# include <gst/vaapi/gstvaapiwindow_x11.h>
#endif
#if GST_VAAPI_USE_GLX
# include <gst/vaapi/gstvaapidisplay_glx.h>
# include <gst/vaapi/gstvaapiwindow_glx.h>
#endif
#if GST_VAAPI_USE_EGL
# include <gst/vaapi/gstvaapidisplay_egl.h>
# include <gst/vaapi/gstvaapiwindow_egl.h>
#endif
#if GST_VAAPI_USE_WAYLAND
# include <gst/vaapi/gstvaapidisplay_wayland.h>
# include <gst/vaapi/gstvaapiwindow_wayland.h>
#endif
#include "output.h"

static const VideoOutputInfo *g_video_output;
static const VideoOutputInfo g_video_outputs[] = {
  /* Video outputs are sorted in test order for automatic characterisation */
#if GST_VAAPI_USE_WAYLAND
  {"wayland",
        gst_vaapi_display_wayland_new,
      gst_vaapi_window_wayland_new},
#endif
#if GST_VAAPI_USE_X11
  {"x11",
        gst_vaapi_display_x11_new,
      gst_vaapi_window_x11_new},
#endif
#if GST_VAAPI_USE_GLX
  {"glx",
        gst_vaapi_display_glx_new,
      gst_vaapi_window_glx_new},
#endif
#if GST_VAAPI_USE_DRM
  {"drm",
        gst_vaapi_display_drm_new,
      gst_vaapi_window_drm_new},
#endif
  {NULL,}
};

static gchar *g_output_name;
static gboolean g_list_outputs = FALSE;
static gboolean g_fullscreen = FALSE;

static gboolean g_egl_mode = FALSE;
static guint g_gles_version;

static GOptionEntry g_options[] = {
  {"list-outputs", 0,
        0,
        G_OPTION_ARG_NONE, &g_list_outputs,
      "list video outputs", NULL},
  {"output", 'o',
        0,
        G_OPTION_ARG_STRING, &g_output_name,
      "video output name", NULL},
  {"fullscreen", 'f',
        0,
        G_OPTION_ARG_NONE, &g_fullscreen,
      "fullscreen mode", NULL},
  {"egl", 0,
        0,
        G_OPTION_ARG_NONE, &g_egl_mode,
      "enable EGL rendering", NULL},
  {"gles-version", 0,
        0,
        G_OPTION_ARG_INT, &g_gles_version,
      "OpenGL|ES version (in --egl mode)", NULL},
  {NULL,}
};

static void
list_outputs (void)
{
  const VideoOutputInfo *o;

  g_print ("Video outputs:");
  for (o = g_video_outputs; o->name != NULL; o++)
    g_print (" %s", o->name);
  g_print ("\n");
}

gboolean
video_output_init (int *argc, char *argv[], GOptionEntry * options)
{
  GOptionContext *ctx;
  gboolean success;

  ctx = g_option_context_new ("- test options");
  if (!ctx)
    return FALSE;

  g_option_context_add_group (ctx, gst_init_get_option_group ());
  g_option_context_add_main_entries (ctx, g_options, NULL);
  if (options)
    g_option_context_add_main_entries (ctx, options, NULL);
  success = g_option_context_parse (ctx, argc, &argv, NULL);
  g_option_context_free (ctx);

  if (g_list_outputs) {
    list_outputs ();
    exit (0);
  }
  return success;
}

void
video_output_exit (void)
{
  g_free (g_output_name);
  gst_deinit ();
}

const VideoOutputInfo *
video_output_lookup (const gchar * output_name)
{
  const VideoOutputInfo *o;

  if (!output_name)
    return NULL;

  for (o = g_video_outputs; o->name != NULL; o++) {
    if (g_ascii_strcasecmp (o->name, output_name) == 0)
      return o;
  }
  return NULL;
}

GstVaapiDisplay *
video_output_create_display (const gchar * display_name)
{
  const VideoOutputInfo *o = g_video_output;
  GstVaapiDisplay *egl_display, *display = NULL;

  if (!o) {
    if (g_output_name)
      o = video_output_lookup (g_output_name);
    else {
      for (o = g_video_outputs; o->name != NULL; o++) {
        display = o->create_display (display_name);
        if (display) {
          if (gst_vaapi_display_get_display (display))
            break;
          gst_object_unref (display);
          display = NULL;
        }
      }
    }
    if (!o || !o->name)
      return NULL;
    g_print ("Using %s video output\n", o->name);
    g_video_output = o;
  }

  if (!display)
    display = o->create_display (display_name);

  if (g_egl_mode) {
#if GST_VAAPI_USE_EGL
    egl_display = gst_vaapi_display_egl_new (display, g_gles_version);
#else
    egl_display = NULL;
    g_print ("error: unsupported EGL renderering mode\n");
#endif
    gst_object_unref (display);
    if (!egl_display)
      return NULL;
    display = egl_display;
  }
  return display;
}

GstVaapiWindow *
video_output_create_window (GstVaapiDisplay * display, guint width,
    guint height)
{
  GstVaapiWindow *window;

  if (!g_video_output)
    return NULL;

#if GST_VAAPI_USE_EGL
  if (g_egl_mode)
    window = gst_vaapi_window_egl_new (display, width, height);
  else
#endif
    window = g_video_output->create_window (display, width, height);
  if (!window)
    return NULL;

  /* Force fullscreen mode, should this be requested by the user */
  if (g_fullscreen)
    gst_vaapi_window_set_fullscreen (window, TRUE);
  return window;
}
