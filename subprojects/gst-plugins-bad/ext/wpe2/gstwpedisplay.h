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

#ifndef GstWPEDisplay_h
#define GstWPEDisplay_h

#include <glib-object.h>
#include <wpe/wpe-platform.h>

#include <gst/gl/egl/gstgldisplay_egl.h>
#include <gst/gl/gl.h>
#include <gst/gl/gstglfuncs.h>
#include "gstwpevideosrc.h"

G_BEGIN_DECLS

#define WPE_TYPE_DISPLAY_GSTREAMER (wpe_display_gstreamer_get_type())
G_DECLARE_FINAL_TYPE(WPEDisplayGStreamer, wpe_display_gstreamer, WPE,
                     DISPLAY_GSTREAMER, WPEDisplay)

WPEDisplay *wpe_display_gstreamer_new();

void wpe_display_gstreamer_set_gl(WPEDisplay *, GstGLDisplay *, GstGLContext *);

G_END_DECLS

#endif /* GstWPEDisplay_h */
