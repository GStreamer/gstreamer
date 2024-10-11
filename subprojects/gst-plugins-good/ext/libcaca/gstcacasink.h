/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_CACASINK_H__
#define __GST_CACASINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <gst/video/video.h>

#include <caca.h>
#ifdef CACA_API_VERSION_1
#   include <caca0.h>
#endif

G_BEGIN_DECLS

#define GST_TYPE_CACASINK (gst_cacasink_get_type())
G_DECLARE_FINAL_TYPE (GstCACASink, gst_cacasink, GST, CACASINK, GstBaseSink)

struct _GstCACASink {
  GstBaseSink parent;

  caca_canvas_t *cv;
  caca_display_t *dp;

  GstVideoInfo info;
  gint screen_width, screen_height;

  guint dither;
  gboolean antialiasing;

  guint driver;

  struct caca_bitmap *bitmap;
};

GST_ELEMENT_REGISTER_DECLARE (cacasink);

G_END_DECLS

#endif /* __GST_CACASINK_H__ */
