/* GStreamer
 *
 * Copyright (C) 2014-2015 Sebastian Dröge <sebastian@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:gstplayer-videooverlayvideorenderer
 * @short_description: Player Video Overlay Video Renderer
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstplayer-video-overlay-video-renderer.h"
#include "gstplayer.h"

#include <gst/video/video.h>

struct _GstPlayerVideoOverlayVideoRenderer
{
  GObject parent;

  GstVideoOverlay *video_overlay;
  gpointer window_handle;
};

struct _GstPlayerVideoOverlayVideoRendererClass
{
  GObjectClass parent_class;
};

static void
    gst_player_video_overlay_video_renderer_interface_init
    (GstPlayerVideoRendererInterface * iface);

enum
{
  VIDEO_OVERLAY_VIDEO_RENDERER_PROP_0,
  VIDEO_OVERLAY_VIDEO_RENDERER_PROP_WINDOW_HANDLE,
  VIDEO_OVERLAY_VIDEO_RENDERER_PROP_LAST
};

G_DEFINE_TYPE_WITH_CODE (GstPlayerVideoOverlayVideoRenderer,
    gst_player_video_overlay_video_renderer, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_PLAYER_VIDEO_RENDERER,
        gst_player_video_overlay_video_renderer_interface_init));

static GParamSpec
    * video_overlay_video_renderer_param_specs
    [VIDEO_OVERLAY_VIDEO_RENDERER_PROP_LAST] = { NULL, };

static void
gst_player_video_overlay_video_renderer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstPlayerVideoOverlayVideoRenderer *self =
      GST_PLAYER_VIDEO_OVERLAY_VIDEO_RENDERER (object);

  switch (prop_id) {
    case VIDEO_OVERLAY_VIDEO_RENDERER_PROP_WINDOW_HANDLE:
      self->window_handle = g_value_get_pointer (value);
      if (self->video_overlay)
        gst_video_overlay_set_window_handle (self->video_overlay,
            (guintptr) self->window_handle);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_player_video_overlay_video_renderer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstPlayerVideoOverlayVideoRenderer *self =
      GST_PLAYER_VIDEO_OVERLAY_VIDEO_RENDERER (object);

  switch (prop_id) {
    case VIDEO_OVERLAY_VIDEO_RENDERER_PROP_WINDOW_HANDLE:
      g_value_set_pointer (value, self->window_handle);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_player_video_overlay_video_renderer_finalize (GObject * object)
{
  GstPlayerVideoOverlayVideoRenderer *self =
      GST_PLAYER_VIDEO_OVERLAY_VIDEO_RENDERER (object);

  if (self->video_overlay)
    gst_object_unref (self->video_overlay);

  G_OBJECT_CLASS
      (gst_player_video_overlay_video_renderer_parent_class)->finalize (object);
}

static void
    gst_player_video_overlay_video_renderer_class_init
    (GstPlayerVideoOverlayVideoRendererClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property =
      gst_player_video_overlay_video_renderer_set_property;
  gobject_class->get_property =
      gst_player_video_overlay_video_renderer_get_property;
  gobject_class->finalize = gst_player_video_overlay_video_renderer_finalize;

  video_overlay_video_renderer_param_specs
      [VIDEO_OVERLAY_VIDEO_RENDERER_PROP_WINDOW_HANDLE] =
      g_param_spec_pointer ("window-handle", "Window Handle",
      "Window handle to embed the video into",
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
      VIDEO_OVERLAY_VIDEO_RENDERER_PROP_LAST,
      video_overlay_video_renderer_param_specs);
}

static void
    gst_player_video_overlay_video_renderer_init
    (G_GNUC_UNUSED GstPlayerVideoOverlayVideoRenderer * self)
{
}

static GstElement *gst_player_video_overlay_video_renderer_create_video_sink
    (GstPlayerVideoRenderer * iface, GstPlayer * player)
{
  GstElement *video_overlay;
  GstPlayerVideoOverlayVideoRenderer *self =
      GST_PLAYER_VIDEO_OVERLAY_VIDEO_RENDERER (iface);

  if (self->video_overlay)
    gst_object_unref (self->video_overlay);

  video_overlay = gst_player_get_pipeline (player);
  g_return_val_if_fail (GST_IS_VIDEO_OVERLAY (video_overlay), NULL);

  self->video_overlay = GST_VIDEO_OVERLAY (video_overlay);

  gst_video_overlay_set_window_handle (self->video_overlay,
      (guintptr) self->window_handle);

  return NULL;
}

static void
    gst_player_video_overlay_video_renderer_interface_init
    (GstPlayerVideoRendererInterface * iface)
{
  iface->create_video_sink =
      gst_player_video_overlay_video_renderer_create_video_sink;
}

/**
 * gst_player_video_overlay_video_renderer_new:
 * @window_handle: (allow-none): Window handle to use or %NULL
 *
 * Returns: (transfer full):
 */
GstPlayerVideoRenderer *
gst_player_video_overlay_video_renderer_new (gpointer window_handle)
{
  return g_object_new (GST_TYPE_PLAYER_VIDEO_OVERLAY_VIDEO_RENDERER,
      "window-handle", window_handle, NULL);
}

/**
 * gst_player_video_overlay_video_renderer_set_window_handle:
 * @self: #GstPlayerVideoRenderer instance
 * @window_handle: handle referencing to the platform specific window
 *
 * Sets the platform specific window handle into which the video
 * should be rendered
 **/
void gst_player_video_overlay_video_renderer_set_window_handle
    (GstPlayerVideoOverlayVideoRenderer * self, gpointer window_handle)
{
  g_return_if_fail (GST_IS_PLAYER_VIDEO_OVERLAY_VIDEO_RENDERER (self));

  g_object_set (self, "window-handle", window_handle, NULL);
}

/**
 * gst_player_video_overlay_video_renderer_get_window_handle:
 * @self: #GstPlayerVideoRenderer instance
 *
 * Returns: (transfer none): The currently set, platform specific window
 * handle
 */
gpointer
    gst_player_video_overlay_video_renderer_get_window_handle
    (GstPlayerVideoOverlayVideoRenderer * self) {
  gpointer window_handle;

  g_return_val_if_fail (GST_IS_PLAYER_VIDEO_OVERLAY_VIDEO_RENDERER (self),
      NULL);

  g_object_get (self, "window-handle", &window_handle, NULL);

  return window_handle;
}
