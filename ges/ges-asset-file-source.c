/* GStreamer Editing Services
 *
 * Copyright (C) 2012 Thibault Saunier <thibault.saunier@collabora.com>
 * Copyright (C) 2012 Volodymyr Rudyi <vladimir.rudoy@gmail.com>
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
/**
 * SECTION: ges-asset-file-source
 * @short_description: A #GESAsset subclass speciliazed in #GESTimelineFileSource extraction
 *
 * The #GESAssetFileSource is a special #GESAssetFileSource that lets you handle
 * the media file to use inside the GStreamer Editing Services. It has APIs that
 * let you get information about the medias. Also, the tags found in the media file are
 * set as Metadatas of the Asser.
 */
#include <gst/pbutils/pbutils.h>
#include "ges.h"
#include "ges-internal.h"

static GHashTable *parent_newparent_table = NULL;
static void
initable_iface_init (GInitableIface * initable_iface)
{
  /*  We can not iniate synchronously */
  initable_iface->init = NULL;
}

G_DEFINE_TYPE_WITH_CODE (GESAssetFileSource, ges_asset_filesource,
    GES_TYPE_ASSET,
    G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init));

/* TODO: We should monitor files here, and add some way of reporting changes
 * to user
 */
enum
{
  PROP_0,
  PROP_DURATION,
  PROP_LAST
};
static GParamSpec *properties[PROP_LAST];

static void discoverer_discovered_cb (GstDiscoverer * discoverer,
    GstDiscovererInfo * info, GError * err, gpointer user_data);

struct _GESAssetFileSourcePrivate
{
  GstDiscovererInfo *info;
  GstClockTime duration;
  gboolean is_image;
};


static void
ges_asset_filesource_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESAssetFileSourcePrivate *priv = GES_ASSET_FILESOURCE (object)->priv;

  switch (property_id) {
    case PROP_DURATION:
      g_value_set_uint64 (value, priv->duration);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_asset_filesource_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESAssetFileSourcePrivate *priv = GES_ASSET_FILESOURCE (object)->priv;

  switch (property_id) {
    case PROP_DURATION:
      priv->duration = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static GESAssetLoadingReturn
_start_loading (GESAsset * asset, GError ** error)
{
  gboolean ret;
  const gchar *uri;
  GESAssetFileSourceClass *class = GES_ASSET_FILESOURCE_GET_CLASS (asset);

  GST_DEBUG ("Started loading %p", asset);

  uri = ges_asset_get_id (asset);

  ret = gst_discoverer_discover_uri_async (class->discoverer, uri);
  if (ret)
    return GES_ASSET_LOADING_ASYNC;

  return GES_ASSET_LOADING_ERROR;
}


static GESExtractable *
ges_asset_filesource_extract (GESAsset * self, GError ** error)
{
  const gchar *uri = ges_asset_get_id (self);
  GESTrackType supportedformats;

  GESTimelineFileSource *tfs = ges_timeline_filesource_new ((gchar *) uri);

  GST_DEBUG_OBJECT (self, "Extracting filesource with uri %s", uri);

  ges_meta_container_get_uint (GES_META_CONTAINER (self),
      GES_META_TIMELINE_OBJECT_SUPPORTED_FORMATS, &supportedformats);

  ges_timeline_object_set_supported_formats (GES_TIMELINE_OBJECT (tfs),
      supportedformats);

  return GES_EXTRACTABLE (tfs);
}

static gboolean
_request_id_update (GESAsset * self, gchar ** proposed_new_id, GError * error)
{
  if (error->domain == GST_RESOURCE_ERROR &&
      error->code == GST_RESOURCE_ERROR_NOT_FOUND) {
    const gchar *uri = ges_asset_get_id (self);
    GFile *parent, *file = g_file_new_for_uri (uri);

    /* Check if we have the new parent in cache */
    parent = g_file_get_parent (file);
    if (parent) {
      GFile *new_parent = g_hash_table_lookup (parent_newparent_table, parent);

      if (new_parent) {
        gchar *basename = g_file_get_basename (file);
        GFile *new_file = g_file_get_child (new_parent, basename);

        *proposed_new_id = g_file_get_uri (new_file);

        g_object_unref (new_file);
        g_object_unref (parent);
        g_free (basename);
      }
    }

    g_object_unref (file);

    return TRUE;
  }

  return FALSE;
}

static void
_asset_proxied (GESAsset * self, const gchar * new_uri)
{
  const gchar *uri = ges_asset_get_id (self);
  GFile *parent, *new_parent, *new_file = g_file_new_for_uri (new_uri),
      *file = g_file_new_for_uri (uri);

  parent = g_file_get_parent (file);
  new_parent = g_file_get_parent (new_file);
  g_hash_table_insert (parent_newparent_table, parent, new_parent);
  g_object_unref (file);
  g_object_unref (new_file);
}

static void
ges_asset_filesource_class_init (GESAssetFileSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  g_type_class_add_private (klass, sizeof (GESAssetFileSourcePrivate));

  object_class->get_property = ges_asset_filesource_get_property;
  object_class->set_property = ges_asset_filesource_set_property;

  GES_ASSET_CLASS (klass)->start_loading = _start_loading;
  GES_ASSET_CLASS (klass)->request_id_update = _request_id_update;
  GES_ASSET_CLASS (klass)->inform_proxy = _asset_proxied;

  GES_ASSET_CLASS (klass)->extract = ges_asset_filesource_extract;

  /**
   * GESAssetFileSource:duration:
   *
   * The duration (in nanoseconds) of the media file
   */
  properties[PROP_DURATION] =
      g_param_spec_uint64 ("duration", "Duration", "The duration to use", 0,
      G_MAXUINT64, GST_CLOCK_TIME_NONE, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_DURATION,
      properties[PROP_DURATION]);

  klass->discoverer = gst_discoverer_new (GST_SECOND, NULL);
  g_signal_connect (klass->discoverer, "discovered",
      G_CALLBACK (discoverer_discovered_cb), NULL);

  /* We just start the discoverer and let it live */
  gst_discoverer_start (klass->discoverer);

  if (parent_newparent_table == NULL) {
    parent_newparent_table = g_hash_table_new_full (g_file_hash,
        (GEqualFunc) g_file_equal, g_object_unref, g_object_unref);
  }
}

static void
ges_asset_filesource_init (GESAssetFileSource * self)
{
  GESAssetFileSourcePrivate *priv;

  priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_ASSET_FILESOURCE, GESAssetFileSourcePrivate);

  priv->info = NULL;
  priv->duration = GST_CLOCK_TIME_NONE;
  priv->is_image = FALSE;
}

static void
ges_asset_filesource_set_info (GESAssetFileSource * self,
    GstDiscovererInfo * info)
{
  GList *tmp, *stream_list;

  GESTrackType supportedformats = GES_TRACK_TYPE_UNKNOWN;
  GESAssetFileSourcePrivate *priv = GES_ASSET_FILESOURCE (self)->priv;

  /* Extract infos from the GstDiscovererInfo */
  stream_list = gst_discoverer_info_get_stream_list (info);
  for (tmp = stream_list; tmp; tmp = tmp->next) {
    GstDiscovererStreamInfo *sinf = (GstDiscovererStreamInfo *) tmp->data;

    if (GST_IS_DISCOVERER_AUDIO_INFO (sinf)) {
      if (supportedformats == GES_TRACK_TYPE_UNKNOWN)
        supportedformats = GES_TRACK_TYPE_AUDIO;
      else
        supportedformats |= GES_TRACK_TYPE_AUDIO;

    } else if (GST_IS_DISCOVERER_VIDEO_INFO (sinf)) {
      if (supportedformats == GES_TRACK_TYPE_UNKNOWN)
        supportedformats = GES_TRACK_TYPE_VIDEO;
      else
        supportedformats |= GES_TRACK_TYPE_VIDEO;
      if (gst_discoverer_video_info_is_image ((GstDiscovererVideoInfo *)
              sinf)) {
        priv->is_image = TRUE;
      }
    } else
      supportedformats = GES_TRACK_TYPE_UNKNOWN;
  }
  ges_meta_container_set_uint (GES_META_CONTAINER (self),
      GES_META_TIMELINE_OBJECT_SUPPORTED_FORMATS, supportedformats);

  if (stream_list)
    gst_discoverer_stream_info_list_free (stream_list);

  if (priv->is_image == FALSE)
    priv->duration = gst_discoverer_info_get_duration (info);
  /* else we keep #GST_CLOCK_TIME_NONE */

  priv->info = g_object_ref (info);
}

static void
_set_meta_foreach (const GstTagList * tags, const gchar * tag,
    GESMetaContainer * container)
{
  GValue value = { 0 };

  gst_tag_list_copy_value (&value, tags, tag);
  ges_meta_container_set_meta (container, tag, &value);
  g_value_unset (&value);
}

static void
discoverer_discovered_cb (GstDiscoverer * discoverer,
    GstDiscovererInfo * info, GError * err, gpointer user_data)
{
  const GstTagList *tags;

  const gchar *uri = gst_discoverer_info_get_uri (info);
  GESAssetFileSource *mfs =
      GES_ASSET_FILESOURCE (ges_asset_cache_lookup
      (GES_TYPE_TIMELINE_FILE_SOURCE, uri));

  tags = gst_discoverer_info_get_tags (info);
  if (tags)
    gst_tag_list_foreach (tags, (GstTagForeachFunc) _set_meta_foreach, mfs);

  if (err == NULL)
    ges_asset_filesource_set_info (mfs, info);
  ges_asset_cache_set_loaded (GES_TYPE_TIMELINE_FILE_SOURCE, uri, err);
}

/* API implementation */
/**
 * ges_asset_filesource_get_info:
 * @self: Target asset
 *
 * Gets #GstDiscovererInfo about the file
 *
 * Returns: (transfer none): #GstDiscovererInfo of specified asset
 */
GstDiscovererInfo *
ges_asset_filesource_get_info (const GESAssetFileSource * self)
{
  return self->priv->info;
}

/**
 * ges_asset_filesource_get_duration:
 * @self: a #GESAssetFileSource
 *
 * Gets duration of the file represented by @self
 *
 * Returns: The duration of @self
 */
GstClockTime
ges_asset_filesource_get_duration (GESAssetFileSource * self)
{
  g_return_val_if_fail (GES_IS_ASSET_FILESOURCE (self), GST_CLOCK_TIME_NONE);

  return self->priv->duration;
}

/**
 * ges_asset_filesource_get_supported_types:
 * @self: a #GESAssetFileSource
 *
 * Gets track types the file as
 *
 * Returns: The track types on which @self will create TrackObject when added to
 * a layer
 */
GESTrackType
ges_asset_filesource_get_supported_types (GESAssetFileSource * self)
{
  GESTrackType supportedformats;

  g_return_val_if_fail (GES_IS_ASSET_FILESOURCE (self), GES_TRACK_TYPE_UNKNOWN);

  ges_meta_container_get_uint (GES_META_CONTAINER (self),
      GES_META_TIMELINE_OBJECT_SUPPORTED_FORMATS, &supportedformats);

  return supportedformats;
}

/**
 * ges_asset_filesource_is_image:
 * @self: a #indent: Standard input:311: Error:Unexpected end of file
GESAssetFileSource
 *
 * Gets Whether the file represented by @self is an image or not
 *
 * Returns: Whether the file represented by @self is an image or not
 */
gboolean
ges_asset_filesource_is_image (GESAssetFileSource * self)
{
  g_return_val_if_fail (GES_IS_ASSET_FILESOURCE (self), FALSE);

  return self->priv->is_image;
}

/**
 * ges_asset_filesource_new:
 * @uri: The URI of the file for which to create a #GESAssetFileSource
 * @cancellable: optional %GCancellable object, %NULL to ignore.
 * @callback: (scope async): a #GAsyncReadyCallback to call when the initialization is finished
 * @user_data: The user data to pass when @callback is called
 *
 * Creates a #GESAssetFileSource for @uri
 *
 * Example of request of a GESAssetFileSource:
 * |[
 * // The request callback
 * static void
 * filesource_asset_loaded_cb (GESAsset * source, GAsyncResult * res, gpointer user_data)
 * {
 *   GError *error = NULL;
 *   GESAssetFileSource *filesource_asset;
 *
 *   filesource_asset = GES_ASSET_FILESOURCE (ges_asset_request_finish (res, &error));
 *   if (filesource_asset) {
 *    g_print ("The file: %s is usable as a FileSource, it is%s an image and lasts %" GST_TIME_FORMAT,
 *        ges_asset_get_id (GES_ASSET (filesource_asset))
 *        ges_asset_filesource_is_image (filesource_asset) ? "" : " not",
 *        GST_TIME_ARGS (ges_asset_filesource_get_duration (filesource_asset));
 *   } else {
 *    g_print ("The file: %s is *not* usable as a FileSource because: %s",
 *        ges_asset_get_id (source), error->message);
 *   }
 *
 *   g_object_unref (mfs);
 * }
 *
 * // The request:
 * ges_asset_filesource_new (uri, (GAsyncReadyCallback) filesource_asset_loaded_cb, user_data);
 * ]|
 */
void
ges_asset_filesource_new (const gchar * uri, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  ges_asset_request_async (GES_TYPE_TIMELINE_FILE_SOURCE, uri, cancellable,
      callback, user_data);
}

/**
 * ges_asset_filesource_set_timeout:
 * @class: The #GESAssetFileSourceClass on which to set the discoverer timeout
 * @timeout: The timeout to set
 *
 * Sets the timeout of #GESAssetFileSource loading
 */
void
ges_asset_filesource_set_timeout (GESAssetFileSourceClass * class,
    GstClockTime timeout)
{
  g_return_if_fail (GES_IS_ASSET_FILESOURCE_CLASS (class));

  g_object_set (class->discoverer, "timeout", timeout, NULL);
}
