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

#include "rtsp-media-mapping.h"

G_DEFINE_TYPE (GstRTSPMediaMapping, gst_rtsp_media_mapping, G_TYPE_OBJECT);

static void gst_rtsp_media_mapping_finalize (GObject * obj);

static GstRTSPMediaFactory * find_media (GstRTSPMediaMapping *mapping, const GstRTSPUrl *url);

static void
gst_rtsp_media_mapping_class_init (GstRTSPMediaMappingClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_rtsp_media_mapping_finalize;

  klass->find_media = find_media;
}

static void
gst_rtsp_media_mapping_init (GstRTSPMediaMapping * mapping)
{
  mapping->mappings = g_hash_table_new_full (g_str_hash, g_str_equal,
		  	g_free, g_object_unref);
}

static void
gst_rtsp_media_mapping_finalize (GObject * obj)
{
  GstRTSPMediaMapping *mapping = GST_RTSP_MEDIA_MAPPING (obj);

  g_hash_table_unref (mapping->mappings);

  G_OBJECT_CLASS (gst_rtsp_media_mapping_parent_class)->finalize (obj);
}

GstRTSPMediaMapping *
gst_rtsp_media_mapping_new (void)
{
  GstRTSPMediaMapping *result;

  result = g_object_new (GST_TYPE_RTSP_MEDIA_MAPPING, NULL);

  return result;
}

static GstRTSPMediaFactory *
find_media (GstRTSPMediaMapping *mapping, const GstRTSPUrl *url)
{
  GstRTSPMediaFactory *result;

  /* find the location of the media in the hashtable we only use the absolute
   * path of the uri to find a mapping. If the mapping depends on other
   * properties found in the url, this method should be overridden. */
  result = g_hash_table_lookup (mapping->mappings, url->abspath);
  if (result) 
    g_object_ref (result);

  g_message ("found media %p for url abspath %s", result, url->abspath);

  return result;
}

/**
 * gst_rtsp_media_mapping_find_factory:
 * @mapping: a #GstRTSPMediaMapping
 * @url: a url
 *
 * Find the #GstRTSPMediaFactory for @url. The default implementation of this object 
 * will use the mappings added with gst_rtsp_media_mapping_add_factory ().
 *
 * Returns: the #GstRTSPMediaFactory for @url. g_object_unref() after usage.
 */
GstRTSPMediaFactory *
gst_rtsp_media_mapping_find_factory (GstRTSPMediaMapping *mapping, const GstRTSPUrl *url)
{
  GstRTSPMediaFactory *result;
  GstRTSPMediaMappingClass *klass;

  klass = GST_RTSP_MEDIA_MAPPING_GET_CLASS (mapping);

  if (klass->find_media)
    result = klass->find_media (mapping, url);
  else
    result = NULL;

  return result;
}

/**
 * gst_rtsp_media_mapping_add_factory:
 * @mapping: a #GstRTSPMediaMapping
 * @path: a mount point
 * @factory: a #GstRTSPMediaFactory
 *
 * Attach @factory to the mount point @path in @mapping.
 *
 * @path is of the form (/node)+. Any previous mapping will be freed.
 *
 * Ownership is taken of the reference on @factory so that @factory should not be
 * used after calling this function.
 */
void
gst_rtsp_media_mapping_add_factory (GstRTSPMediaMapping *mapping, const gchar *path,
    GstRTSPMediaFactory *factory)
{
  g_return_if_fail (GST_IS_RTSP_MEDIA_MAPPING (mapping));
  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));
  g_return_if_fail (path != NULL);

  g_hash_table_insert (mapping->mappings, g_strdup (path), factory);
}

/**
 * gst_rtsp_media_mapping_remove_factory:
 * @mapping: a #GstRTSPMediaMapping
 * @path: a mount point
 *
 * Remove the #GstRTSPMediaFactory associated with @path in @mapping.
 */
void
gst_rtsp_media_mapping_remove_factory (GstRTSPMediaMapping *mapping, const gchar *path)
{
  g_return_if_fail (GST_IS_RTSP_MEDIA_MAPPING (mapping));
  g_return_if_fail (path != NULL);

  g_hash_table_remove (mapping->mappings, path);
}

