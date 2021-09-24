/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#ifndef __GST_GTK_SINK_H__
#define __GST_GTK_SINK_H__

#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>

#include "gstgtkbasesink.h"

G_BEGIN_DECLS

#define GST_TYPE_GTK_SINK (gst_gtk_sink_get_type())
G_DECLARE_FINAL_TYPE (GstGtkSink, gst_gtk_sink, GST, GTK_SINK, GstGtkBaseSink)

/**
 * GstGtkSink:
 *
 * Opaque #GstGtkSink object
 */
struct _GstGtkSink
{
  /* <private> */
  GstGtkBaseSink       parent;
};

GST_ELEMENT_REGISTER_DECLARE (gtksink);

G_END_DECLS

#endif /* __GST_GTK_SINK_H__ */
