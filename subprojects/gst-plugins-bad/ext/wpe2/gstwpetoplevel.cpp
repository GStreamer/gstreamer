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

#include "gstwpetoplevel.h"

struct _WPEToplevelGStreamer
{
  WPEToplevel parent;
};

#define wpe_toplevel_gstreamer_parent_class parent_class
G_DEFINE_TYPE (WPEToplevelGStreamer, wpe_toplevel_gstreamer, WPE_TYPE_TOPLEVEL);

static gboolean
wpe_toplevel_gstreamer_resize (WPEToplevel * toplevel, int width, int height)
{
  wpe_toplevel_resized (toplevel, width, height);
  /* *INDENT-OFF* */
  wpe_toplevel_foreach_view(toplevel, [](WPEToplevel *toplevel, WPEView *view, gpointer) -> gboolean {
    int width, height;
    wpe_toplevel_get_size (toplevel, &width, &height);
    wpe_view_resized (view, width, height);
    return FALSE;
  }, nullptr);
  /* *INDENT-ON* */
  return TRUE;
}

static void
wpe_toplevel_gstreamer_init (WPEToplevelGStreamer * toplevel)
{
}

static void
wpe_toplevel_gstreamer_class_init (WPEToplevelGStreamerClass * klass)
{
  WPEToplevelClass *toplevelClass = WPE_TOPLEVEL_CLASS (klass);
  toplevelClass->resize = wpe_toplevel_gstreamer_resize;
}

WPEToplevel *
wpe_toplevel_gstreamer_new (WPEDisplayGStreamer * display)
{
  return WPE_TOPLEVEL (g_object_new (WPE_TYPE_TOPLEVEL_GSTREAMER, "display",
          display, nullptr));
}
