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
/**
 * SECTION:rtsp-mount-points
 * @short_description: Map a path to media
 * @see_also: #GstRTSPMediaFactory, #GstRTSPClient
 *
 * A #GstRTSPMountPoints object maintains a relation between paths
 * and #GstRTSPMediaFactory objects. This object is usually given to
 * #GstRTSPClient and used to find the media attached to a path.
 *
 * With gst_rtsp_mount_points_add_factory () and
 * gst_rtsp_mount_points_remove_factory(), factories can be added and
 * removed.
 *
 * With gst_rtsp_mount_points_match() you can find the #GstRTSPMediaFactory
 * object that completely matches the given path.
 *
 * Last reviewed on 2013-07-11 (1.0.0)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "rtsp-mount-points.h"

typedef struct
{
  gchar *path;
  gint len;
  GstRTSPMediaFactory *factory;
} DataItem;

static DataItem *
data_item_new (gchar * path, gint len, GstRTSPMediaFactory * factory)
{
  DataItem *item;

  item = g_new (DataItem, 1);
  item->path = path;
  item->len = len;
  item->factory = factory;

  return item;
}

static void
data_item_free (gpointer data)
{
  DataItem *item = data;

  g_free (item->path);
  g_object_unref (item->factory);
  g_free (item);
}

static void
data_item_dump (gconstpointer a, gconstpointer prefix)
{
  const DataItem *item = a;

  GST_DEBUG ("%s%s %p", (gchar *) prefix, item->path, item->factory);
}

static gint
data_item_compare (gconstpointer a, gconstpointer b, gpointer user_data)
{
  const DataItem *item1 = a, *item2 = b;
  gint res;

  res = g_strcmp0 (item1->path, item2->path);

  return res;
}

struct _GstRTSPMountPointsPrivate
{
  GMutex lock;
  GSequence *mounts;            /* protected by lock */
  gboolean dirty;
};

G_DEFINE_TYPE_WITH_PRIVATE (GstRTSPMountPoints, gst_rtsp_mount_points,
    G_TYPE_OBJECT);

GST_DEBUG_CATEGORY_STATIC (rtsp_media_debug);
#define GST_CAT_DEFAULT rtsp_media_debug

static gchar *default_make_path (GstRTSPMountPoints * mounts,
    const GstRTSPUrl * url);
static void gst_rtsp_mount_points_finalize (GObject * obj);

static void
gst_rtsp_mount_points_class_init (GstRTSPMountPointsClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_rtsp_mount_points_finalize;

  klass->make_path = default_make_path;

  GST_DEBUG_CATEGORY_INIT (rtsp_media_debug, "rtspmountpoints", 0,
      "GstRTSPMountPoints");
}

static void
gst_rtsp_mount_points_init (GstRTSPMountPoints * mounts)
{
  GstRTSPMountPointsPrivate *priv;

  GST_DEBUG_OBJECT (mounts, "created");

  mounts->priv = priv = gst_rtsp_mount_points_get_instance_private (mounts);

  g_mutex_init (&priv->lock);
  priv->mounts = g_sequence_new (data_item_free);
  priv->dirty = FALSE;
}

static void
gst_rtsp_mount_points_finalize (GObject * obj)
{
  GstRTSPMountPoints *mounts = GST_RTSP_MOUNT_POINTS (obj);
  GstRTSPMountPointsPrivate *priv = mounts->priv;

  GST_DEBUG_OBJECT (mounts, "finalized");

  g_sequence_free (priv->mounts);
  g_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (gst_rtsp_mount_points_parent_class)->finalize (obj);
}

/**
 * gst_rtsp_mount_points_new:
 *
 * Make a new mount points object.
 *
 * Returns: (transfer full): a new #GstRTSPMountPoints
 */
GstRTSPMountPoints *
gst_rtsp_mount_points_new (void)
{
  GstRTSPMountPoints *result;

  result = g_object_new (GST_TYPE_RTSP_MOUNT_POINTS, NULL);

  return result;
}

static gchar *
default_make_path (GstRTSPMountPoints * mounts, const GstRTSPUrl * url)
{
  /* normalize rtsp://<IP>:<PORT> to rtsp://<IP>:<PORT>/ */
  return g_strdup (url->abspath[0] ? url->abspath : "/");
}

/**
 * gst_rtsp_mount_points_make_path:
 * @mounts: a #GstRTSPMountPoints
 * @url: a #GstRTSPUrl
 *
 * Make a path string from @url.
 *
 * Returns: (transfer full) (nullable): a path string for @url, g_free() after usage.
 */
gchar *
gst_rtsp_mount_points_make_path (GstRTSPMountPoints * mounts,
    const GstRTSPUrl * url)
{
  GstRTSPMountPointsClass *klass;
  gchar *result;

  g_return_val_if_fail (GST_IS_RTSP_MOUNT_POINTS (mounts), NULL);
  g_return_val_if_fail (url != NULL, NULL);

  klass = GST_RTSP_MOUNT_POINTS_GET_CLASS (mounts);

  if (klass->make_path)
    result = klass->make_path (mounts, url);
  else
    result = NULL;

  return result;
}

static gboolean
has_prefix (DataItem * str, DataItem * prefix)
{
  /* prefix needs to be smaller than str */
  if (str->len < prefix->len)
    return FALSE;

  /* special case when "/" is the entire prefix */
  if (prefix->len == 1 && prefix->path[0] == '/' && str->path[0] == '/')
    return TRUE;

  /* if str is larger, it there should be a / following the prefix */
  if (str->len > prefix->len && str->path[prefix->len] != '/')
    return FALSE;

  return strncmp (str->path, prefix->path, prefix->len) == 0;
}

/**
 * gst_rtsp_mount_points_match:
 * @mounts: a #GstRTSPMountPoints
 * @path: a mount point
 * @matched: (out) (allow-none): the amount of @path matched
 *
 * Find the factory in @mounts that has the longest match with @path.
 *
 * If @matched is %NULL, @path will match the factory exactly otherwise
 * the amount of characters that matched is returned in @matched.
 *
 * Returns: (transfer full): the #GstRTSPMediaFactory for @path.
 * g_object_unref() after usage.
 */
GstRTSPMediaFactory *
gst_rtsp_mount_points_match (GstRTSPMountPoints * mounts,
    const gchar * path, gint * matched)
{
  GstRTSPMountPointsPrivate *priv;
  GstRTSPMediaFactory *result = NULL;
  GSequenceIter *iter, *best;
  DataItem item, *ritem;

  g_return_val_if_fail (GST_IS_RTSP_MOUNT_POINTS (mounts), NULL);
  g_return_val_if_fail (path != NULL, NULL);

  priv = mounts->priv;

  item.path = (gchar *) path;
  item.len = strlen (path);

  GST_LOG ("Looking for mount point path %s", path);

  g_mutex_lock (&priv->lock);
  if (priv->dirty) {
    g_sequence_sort (priv->mounts, data_item_compare, mounts);
    g_sequence_foreach (priv->mounts, (GFunc) data_item_dump,
        (gpointer) "sort :");
    priv->dirty = FALSE;
  }

  /* find the location of the media in the hashtable we only use the absolute
   * path of the uri to find a media factory. If the factory depends on other
   * properties found in the url, this method should be overridden. */
  iter = g_sequence_get_begin_iter (priv->mounts);
  best = NULL;
  while (!g_sequence_iter_is_end (iter)) {
    ritem = g_sequence_get (iter);

    data_item_dump (ritem, "inspect: ");

    /* The sequence is sorted, so any prefix match is an improvement upon
     * the previous best match, as '/abc' will always be before '/abcd' */
    if (has_prefix (&item, ritem)) {
      if (best == NULL) {
        data_item_dump (ritem, "prefix: ");
      } else {
        data_item_dump (ritem, "new best: ");
      }
      best = iter;
    } else {
      /* if have a match and the current item doesn't prefix match the best we
       * found so far then we're moving away and can bail out of the loop */
      if (best != NULL && !has_prefix (ritem, g_sequence_get (best)))
        break;
    }

    iter = g_sequence_iter_next (iter);
  }
  if (best) {
    ritem = g_sequence_get (best);
    data_item_dump (ritem, "result: ");
    if (matched || ritem->len == item.len) {
      result = g_object_ref (ritem->factory);
      if (matched)
        *matched = ritem->len;
    }
  }
  g_mutex_unlock (&priv->lock);

  GST_INFO ("found media factory %p for path %s", result, path);

  return result;
}

static void
gst_rtsp_mount_points_remove_factory_unlocked (GstRTSPMountPoints * mounts,
    const gchar * path)
{
  GstRTSPMountPointsPrivate *priv = mounts->priv;
  DataItem item;
  GSequenceIter *iter;

  item.path = (gchar *) path;

  if (priv->dirty) {
    g_sequence_sort (priv->mounts, data_item_compare, mounts);
    priv->dirty = FALSE;
  }
  iter = g_sequence_lookup (priv->mounts, &item, data_item_compare, mounts);
  if (iter) {
    g_sequence_remove (iter);
    priv->dirty = TRUE;
  }
}

/**
 * gst_rtsp_mount_points_add_factory:
 * @mounts: a #GstRTSPMountPoints
 * @path: a mount point
 * @factory: (transfer full): a #GstRTSPMediaFactory
 *
 * Attach @factory to the mount point @path in @mounts.
 *
 * @path is either of the form (/node)+ or the root path '/'. (An empty path is
 * not allowed.) Any previous mount point will be freed.
 *
 * Ownership is taken of the reference on @factory so that @factory should not be
 * used after calling this function.
 */
void
gst_rtsp_mount_points_add_factory (GstRTSPMountPoints * mounts,
    const gchar * path, GstRTSPMediaFactory * factory)
{
  GstRTSPMountPointsPrivate *priv;
  DataItem *item;

  g_return_if_fail (GST_IS_RTSP_MOUNT_POINTS (mounts));
  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));
  g_return_if_fail (path != NULL && path[0] == '/');

  priv = mounts->priv;

  item = data_item_new (g_strdup (path), strlen (path), factory);

  GST_INFO ("adding media factory %p for path %s", factory, path);

  g_mutex_lock (&priv->lock);
  gst_rtsp_mount_points_remove_factory_unlocked (mounts, path);
  g_sequence_append (priv->mounts, item);
  priv->dirty = TRUE;
  g_mutex_unlock (&priv->lock);
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
  GstRTSPMountPointsPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MOUNT_POINTS (mounts));
  g_return_if_fail (path != NULL && path[0] == '/');

  priv = mounts->priv;

  GST_INFO ("removing media factory for path %s", path);

  g_mutex_lock (&priv->lock);
  gst_rtsp_mount_points_remove_factory_unlocked (mounts, path);
  g_mutex_unlock (&priv->lock);
}
