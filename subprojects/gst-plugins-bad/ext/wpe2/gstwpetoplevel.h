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

#ifndef GstWPETopLevel_h
#define GstWPETopLevel_h

#include <glib-object.h>
#include "gstwpedisplay.h"

G_BEGIN_DECLS

#define WPE_TYPE_TOPLEVEL_GSTREAMER (wpe_toplevel_gstreamer_get_type())
G_DECLARE_FINAL_TYPE(WPEToplevelGStreamer, wpe_toplevel_gstreamer, WPE,
                     TOPLEVEL_GSTREAMER, WPEToplevel)

WPEToplevel *wpe_toplevel_gstreamer_new(WPEDisplayGStreamer *);

G_END_DECLS

#endif /* GstWPETopLevel_h */
