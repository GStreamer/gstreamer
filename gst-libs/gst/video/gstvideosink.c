/*
 *  GStreamer Video sink.
 *
 *  Copyright (C) <2003> Julien Moutte <julien@moutte.net>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "videosink.h"

static GstElementClass *parent_class = NULL;


/* Initing stuff */

static void
gst_video_sink_init (GstVideoSink * videosink)
{
  videosink->width = 0;
  videosink->height = 0;
}

static void
gst_video_sink_class_init (GstVideoSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
}

/* Public methods */

GType
gst_video_sink_get_type (void)
{
  static GType videosink_type = 0;

  if (!videosink_type) {
    static const GTypeInfo videosink_info = {
      sizeof (GstVideoSinkClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_video_sink_class_init,
      NULL,
      NULL,
      sizeof (GstVideoSink),
      0,
      (GInstanceInitFunc) gst_video_sink_init,
    };

    videosink_type = g_type_register_static (GST_TYPE_BASE_SINK,
        "GstVideoSink", &videosink_info, 0);
  }

  return videosink_type;
}
