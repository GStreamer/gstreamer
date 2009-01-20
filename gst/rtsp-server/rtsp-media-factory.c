/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
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

#include "rtsp-media-factory.h"

G_DEFINE_TYPE (GstRTSPMediaFactory, gst_rtsp_media_factory, G_TYPE_OBJECT);

static void gst_rtsp_media_factory_finalize (GObject * obj);

static GstRTSPMedia * create_media (GstRTSPMediaFactory *factory, const gchar *url);

static void
gst_rtsp_media_factory_class_init (GstRTSPMediaFactoryClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_rtsp_media_factory_finalize;

  klass->create_media = create_media;
}

static void
gst_rtsp_media_factory_init (GstRTSPMediaFactory * factory)
{
}

static void
gst_rtsp_media_factory_finalize (GObject * obj)
{
  G_OBJECT_CLASS (gst_rtsp_media_factory_parent_class)->finalize (obj);
}

GstRTSPMediaFactory *
gst_rtsp_media_factory_new (void)
{
  GstRTSPMediaFactory *result;

  result = g_object_new (GST_TYPE_RTSP_MEDIA_FACTORY, NULL);

  return result;
}

static GstRTSPMedia *
create_media (GstRTSPMediaFactory *factory, const gchar *url)
{
  GstRTSPMedia *result;

  result = gst_rtsp_media_new (url);

  return result;
}

GstRTSPMedia *
gst_rtsp_media_factory_create (GstRTSPMediaFactory *factory, const gchar *url)
{
  GstRTSPMedia *result = NULL;
  GstRTSPMediaFactoryClass *klass;

  klass = GST_RTSP_MEDIA_FACTORY_GET_CLASS (factory);

  if (klass->create_media)
    result = klass->create_media (factory, url);

  return result;
}

void
gst_rtsp_media_factory_add (GstRTSPMediaFactory *factory, const gchar *path,
    GType type)
{
  g_warning ("gst_rtsp_media_factory_add: not implemented");
}
void
gst_rtsp_media_factory_remove (GstRTSPMediaFactory *factory, const gchar *path,
    GType type)
{
  g_warning ("gst_rtsp_media_factory_remove: not implemented");
}

