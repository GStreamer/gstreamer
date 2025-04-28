/* Copyright (C) <2025> Philippe Normand <philn@igalia.com>
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
#include <config.h>
#endif

#include "gstwpeview.h"
#include "gstwpethreadedview.h"

struct _WPEViewGStreamer
{
  WPEView parent;

  GstWPEThreadedView *client;
};

#define wpe_view_gstreamer_parent_class parent_class
G_DEFINE_TYPE (WPEViewGStreamer, wpe_view_gstreamer, WPE_TYPE_VIEW);

static gboolean
wpe_view_gstreamer_render_buffer (WPEView * view, WPEBuffer * buffer,
    const WPERectangle *, guint, GError ** error)
{
  auto self = WPE_VIEW_GSTREAMER (view);
  // TODO: Add support for damage rects.
  return self->client->setPendingBuffer (buffer, error);
}

static void
wpe_view_gstreamer_init (WPEViewGStreamer * view)
{
}

static void
wpe_view_gstreamer_class_init (WPEViewGStreamerClass * klass)
{
  WPEViewClass *viewClass = WPE_VIEW_CLASS (klass);
  viewClass->render_buffer = wpe_view_gstreamer_render_buffer;
}

WPEView *
wpe_view_gstreamer_new (WPEDisplayGStreamer * display)
{
  return WPE_VIEW (g_object_new (WPE_TYPE_VIEW_GSTREAMER, "display", display,
          nullptr));
}

void
wpe_view_gstreamer_set_client (WPEViewGStreamer * view,
    GstWPEThreadedView * client)
{
  view->client = client;
}
