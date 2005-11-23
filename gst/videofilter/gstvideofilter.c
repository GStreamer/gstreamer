/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
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

#include "gstvideofilter.h"

GST_DEBUG_CATEGORY_STATIC (gst_videofilter_debug);
#define GST_CAT_DEFAULT gst_videofilter_debug

static void gst_videofilter_class_init (gpointer g_class, gpointer class_data);
static void gst_videofilter_init (GTypeInstance * instance, gpointer g_class);

static GstBaseTransformClass *parent_class = NULL;

GType
gst_videofilter_get_type (void)
{
  static GType videofilter_type = 0;

  if (!videofilter_type) {
    static const GTypeInfo videofilter_info = {
      sizeof (GstVideofilterClass),
      NULL,
      NULL,
      gst_videofilter_class_init,
      NULL,
      NULL,
      sizeof (GstVideofilter),
      0,
      gst_videofilter_init,
    };

    videofilter_type = g_type_register_static (GST_TYPE_BASE_TRANSFORM,
        "GstVideofilter", &videofilter_info, G_TYPE_FLAG_ABSTRACT);
  }
  return videofilter_type;
}

static void
gst_videofilter_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *trans_class;
  GstVideofilterClass *klass;

  klass = (GstVideofilterClass *) g_class;
  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  GST_DEBUG_CATEGORY_INIT (gst_videofilter_debug, "videofilter", 0,
      "videofilter");
}

static void
gst_videofilter_init (GTypeInstance * instance, gpointer g_class)
{
  GstVideofilter *videofilter = GST_VIDEOFILTER (instance);

  GST_DEBUG_OBJECT (videofilter, "gst_videofilter_init");

  videofilter->inited = FALSE;
}
