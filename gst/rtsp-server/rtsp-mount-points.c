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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "rtsp-mount-points.h"

G_DEFINE_TYPE (GstRTSPMountPoints, gst_rtsp_mount_points, G_TYPE_OBJECT);

GST_DEBUG_CATEGORY_STATIC (rtsp_media_debug);
#define GST_CAT_DEFAULT rtsp_media_debug

static void gst_rtsp_mount_points_finalize (GObject * obj);

static GstRTSPMediaFactory *find_factory (GstRTSPMountPoints * mounts,
    const GstRTSPUrl * url);

static void
gst_rtsp_mount_points_class_init (GstRTSPMountPointsClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_rtsp_mount_points_finalize;

  klass->find_factory = find_factory;

  GST_DEBUG_CATEGORY_INIT (rtsp_media_debug, "rtspmountpoints", 0,
      "GstRTSPMountPoints");
}

static void
gst_rtsp_mount_points_init (GstRTSPMountPoints * mounts)
{
  GST_DEBUG_OBJECT (mounts, "created");

  g_mutex_init (&mounts->lock);
  mounts->mounts = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);
}

static void
gst_rtsp_mount_points_finalize (GObject * obj)
{
  GstRTSPMountPoints *mounts = GST_RTSP_MOUNT_POINTS (obj);

  GST_DEBUG_OBJECT (mounts, "finalized");

  g_hash_table_unref (mounts->mounts);
  g_mutex_clear (&mounts->lock);

  G_OBJECT_CLASS (gst_rtsp_mount_points_parent_class)->finalize (obj);
}

/**
 * gst_rtsp_mount_points_new:
 *
 * Make a new mount points object.
 *
 * Returns: a new #GstRTSPMountPoints
 */
GstRTSPMountPoints *
gst_rtsp_mount_points_new (void)
{
  GstRTSPMountPoints *result;

  result = g_object_new (GST_TYPE_RTSP_MOUNT_POINTS, NULL);

  return result;
}

static GstRTSPMediaFactory *
find_factory (GstRTSPMountPoints * mounts, const GstRTSPUrl * url)
{
  GstRTSPMediaFactory *result;

  g_mutex_lock (&mounts->lock);
  /* find the location of the media in the hashtable we only use the absolute
   * path of the uri to find a media factory. If the factory depends on other
   * properties found in the url, this method should be overridden. */
  result = g_hash_table_lookup (mounts->mounts, url->abspath);
  if (result)
    g_object_ref (result);
  g_mutex_unlock (&mounts->lock);

  GST_INFO ("found media factory %p for url abspath %s", result, url->abspath);

  return result;
}

/**
 * gst_rtsp_mount_points_find_factory:
 * @mounts: a #GstRTSPMountPoints
 * @url: a url
 *
 * Find the #GstRTSPMediaFactory for @url. The default implementation of this object
 * will use the media factory added with gst_rtsp_mount_points_add_factory ().
 *
 * Returns: (transfer full): the #GstRTSPMediaFactory for @url. g_object_unref() after usage.
 */
GstRTSPMediaFactory *
gst_rtsp_mount_points_find_factory (GstRTSPMountPoints * mounts,
    const GstRTSPUrl * url)
{
  GstRTSPMediaFactory *result;
  GstRTSPMountPointsClass *klass;

  g_return_val_if_fail (GST_IS_RTSP_MOUNT_POINTS (mounts), NULL);
  g_return_val_if_fail (url != NULL, NULL);

  klass = GST_RTSP_MOUNT_POINTS_GET_CLASS (mounts);

  if (klass->find_factory)
    result = klass->find_factory (mounts, url);
  else
    result = NULL;

  return result;
}

/**
 * gst_rtsp_mount_points_add_factory:
 * @mounts: a #GstRTSPMountPoints
 * @path: a mount point
 * @factory: (transfer full): a #GstRTSPMediaFactory
 *
 * Attach @factory to the mount point @path in @mounts.
 *
 * @path is of the form (/node)+. Any previous mount point will be freed.
 *
 * Ownership is taken of the reference on @factory so that @factory should not be
 * used after calling this function.
 */
void
gst_rtsp_mount_points_add_factory (GstRTSPMountPoints * mounts,
    const gchar * path, GstRTSPMediaFactory * factory)
{
  g_return_if_fail (GST_IS_RTSP_MOUNT_POINTS (mounts));
  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));
  g_return_if_fail (path != NULL);

  g_mutex_lock (&mounts->lock);
  g_hash_table_insert (mounts->mounts, g_strdup (path), factory);
  g_mutex_unlock (&mounts->lock);
}

/**
 * gst_rtsp_mount_points_remove_factory:
 * @mounts: a #GstRTSPMountPoints
 * @path: a mount point
 *
 * Remove the #GstRTSPMediaFactory associated with @path in @mounts.
 */
void
gst_rtsp_mount_points_remove_factory (GstRTSPMountPoints * mounts,
    const gchar * path)
{
  g_return_if_fail (GST_IS_RTSP_MOUNT_POINTS (mounts));
  g_return_if_fail (path != NULL);

  g_mutex_lock (&mounts->lock);
  g_hash_table_remove (mounts->mounts, path);
  g_mutex_unlock (&mounts->lock);
}
