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

#ifndef GstWPEView_h
#define GstWPEView_h

#include <glib-object.h>
#include "gstwpedisplay.h"

class GstWPEThreadedView;

G_BEGIN_DECLS

#define WPE_TYPE_VIEW_GSTREAMER (wpe_view_gstreamer_get_type())
G_DECLARE_FINAL_TYPE(WPEViewGStreamer, wpe_view_gstreamer, WPE,
                     VIEW_GSTREAMER, WPEView)

typedef struct _WPEDisplayGStreamer WPEDisplayGStreamer;

WPEView *wpe_view_gstreamer_new(WPEDisplayGStreamer *);

void wpe_view_gstreamer_set_client(WPEViewGStreamer*, GstWPEThreadedView*);

G_END_DECLS

#endif /* GstWPEView_h */
