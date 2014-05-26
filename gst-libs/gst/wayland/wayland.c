/*
 * GStreamer Wayland Library
 * Copyright (C) 2014 Collabora Ltd.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/wayland/wayland.h>
#include <gst/video/videooverlay.h>

gboolean
gst_is_wayland_display_handle_need_context_message (GstMessage * msg)
{
  const gchar *type = NULL;

  g_return_val_if_fail (GST_IS_MESSAGE (msg), FALSE);

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_NEED_CONTEXT &&
      gst_message_parse_context_type (msg, &type)) {
    return !g_strcmp0 (type, GST_WAYLAND_DISPLAY_HANDLE_CONTEXT_TYPE);
  }

  return FALSE;
}

GstContext *
gst_wayland_display_handle_context_new (struct wl_display * display)
{
  GstContext *context =
      gst_context_new (GST_WAYLAND_DISPLAY_HANDLE_CONTEXT_TYPE, TRUE);
  gst_structure_set (gst_context_writable_structure (context),
      "handle", G_TYPE_POINTER, display, NULL);
  return context;
}

struct wl_display *
gst_wayland_display_handle_context_get_handle (GstContext * context)
{
  const GstStructure *s;
  struct wl_display *display;

  g_return_val_if_fail (GST_IS_CONTEXT (context), NULL);

  s = gst_context_get_structure (context);
  gst_structure_get (s, "handle", G_TYPE_POINTER, &display, NULL);
  return display;
}


G_DEFINE_INTERFACE (GstWaylandVideo, gst_wayland_video, GST_TYPE_VIDEO_OVERLAY);

static void
gst_wayland_video_default_init (GstWaylandVideoInterface * klass)
{
  (void) klass;
}

/**
 * gst_wayland_video_pause_rendering:
 *
 * This tells the video sink to stop rendering on the surface,
 * dropping frames in the meanwhile. This should be called
 * before resizing a stack of subsurfaces, one of which is
 * the surface of the video sink.
 */
void
gst_wayland_video_pause_rendering (GstWaylandVideo * video)
{
  GstWaylandVideoInterface *iface;

  g_return_if_fail (video != NULL);
  g_return_if_fail (GST_IS_WAYLAND_VIDEO (video));

  iface = GST_WAYLAND_VIDEO_GET_INTERFACE (video);

  if (iface->pause_rendering) {
    iface->pause_rendering (video);
  }
}

/**
 * gst_wayland_video_resume_rendering:
 *
 * Resumes surface rendering that was previously paused
 * with gst_wayland_video_pause_rendering. This function will
 * block until there is a new wl_buffer commited on the surface
 * inside the element, either with a new frame (if the element
 * is PLAYING) or with an old frame (if the element is PAUSED).
 */
void
gst_wayland_video_resume_rendering (GstWaylandVideo * video)
{
  GstWaylandVideoInterface *iface;

  g_return_if_fail (video != NULL);
  g_return_if_fail (GST_IS_WAYLAND_VIDEO (video));

  iface = GST_WAYLAND_VIDEO_GET_INTERFACE (video);

  if (iface->resume_rendering) {
    iface->resume_rendering (video);
  }
}
