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
 * SECTION: gesuriclipasset
 * @short_description: A GESAsset subclass specialized in GESUriClip extraction
 *
 * The #GESUriClipAsset is a special #GESAsset that lets you handle
 * the media file to use inside the GStreamer Editing Services. It has APIs that
 * let you get information about the medias. Also, the tags found in the media file are
 * set as Metadatas of the Asser.
 */
#include <errno.h>
#include <gst/pbutils/pbutils.h>
#include "ges.h"
#include "ges-internal.h"
#include "ges-track-element-asset.h"

static GHashTable *parent_newparent_table = NULL;

static void
initable_iface_init (GInitableIface * initable_iface)
{
  /*  We can not iniate synchronously */
  initable_iface->init = NULL;
}

G_DEFINE_TYPE_WITH_CODE (GESUriClipAsset, ges_uri_clip_asset,
    GES_TYPE_CLIP_ASSET,
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

struct _GESUriClipAssetPrivate
{
  GstDiscovererInfo *info;
  GstClockTime duration;
  gboolean is_image;

  GList *asset_trackfilesources;
};

struct _GESUriSourceAssetPrivate
{
  GstDiscovererStreamInfo *sinfo;
  GESUriClipAsset *parent_asset;

  const gchar *uri;
};


static void
ges_uri_clip_asset_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESUriClipAssetPrivate *priv = GES_URI_CLIP_ASSET (object)->priv;

  switch (property_id) {
    case PROP_DURATION:
      g_value_set_uint64 (value, priv->duration);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_uri_clip_asset_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESUriClipAssetPrivate *priv = GES_URI_CLIP_ASSET (object)->priv;

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
  GESUriClipAssetClass *class = GES_URI_CLIP_ASSET_GET_CLASS (asset);

  GST_DEBUG ("Started loading %p", asset);

  uri = ges_asset_get_id (asset);

  ret = gst_discoverer_discover_uri_async (class->discoverer, uri);
  if (ret)
    return GES_ASSET_LOADING_ASYNC;

  return GES_ASSET_LOADING_ERROR;
}

static gboolean
_request_id_update (GESAsset * self, gchar ** proposed_new_id, GError * error)
{
  if (error->domain == GST_RESOURCE_ERROR &&
      (error->code == GST_RESOURCE_ERROR_NOT_FOUND ||
          error->code == GST_RESOURCE_ERROR_OPEN_READ)) {
    const gchar *uri = ges_asset_get_id (self);
    GFile *parent, *file = g_file_new_for_uri (uri);

    /* Check if we have the new parent in cache */
    parent = g_file_get_parent (file);
    if (parent) {
      GFile *new_parent = g_hash_table_lookup (parent_newparent_table, parent);

      if (new_parent) {
        gchar *basename = g_file_get_basename (file);
        GFile *new_file = g_file_get_child (new_parent, basename);

        /* FIXME Handle the GCancellable */
        if (g_file_query_exists (new_file, NULL)) {
          *proposed_new_id = g_file_get_uri (new_file);
          GST_DEBUG_OBJECT (self, "Proposing path: %s as proxy",
              *proposed_new_id);
        }

        gst_object_unref (new_file);
        g_free (basename);
      }
      gst_object_unref (parent);
    }

    gst_object_unref (file);

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
  gst_object_unref (file);
  gst_object_unref (new_file);
}

static void
ges_uri_clip_asset_class_init (GESUriClipAssetClass * klass)
{
  GstClockTime timeout;
  const gchar *timeout_str;
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  g_type_class_add_private (klass, sizeof (GESUriClipAssetPrivate));

  object_class->get_property = ges_uri_clip_asset_get_property;
  object_class->set_property = ges_uri_clip_asset_set_property;

  GES_ASSET_CLASS (klass)->start_loading = _start_loading;
  GES_ASSET_CLASS (klass)->request_id_update = _request_id_update;
  GES_ASSET_CLASS (klass)->inform_proxy = _asset_proxied;


  /**
   * GESUriClipAsset:duration:
   *
   * The duration (in nanoseconds) of the media file
   */
  properties[PROP_DURATION] =
      g_param_spec_uint64 ("duration", "Duration", "The duration to use", 0,
      G_MAXUINT64, GST_CLOCK_TIME_NONE, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_DURATION,
      properties[PROP_DURATION]);

  errno = 0;
  timeout_str = g_getenv ("GES_DISCOVERY_TIMEOUT");
  if (timeout_str)
    timeout = g_ascii_strtod (timeout_str, NULL) * GST_SECOND;
  else
    errno = 10;

  if (errno)
    timeout = 60 * GST_SECOND;

  klass->discoverer = gst_discoverer_new (timeout, NULL);
  klass->sync_discoverer = gst_discoverer_new (timeout, NULL);
  g_signal_connect (klass->discoverer, "discovered",
      G_CALLBACK (discoverer_discovered_cb), NULL);

  /* We just start the discoverer and let it live */
  gst_discoverer_start (klass->discoverer);
  if (parent_newparent_table == NULL) {
    parent_newparent_table = g_hash_table_new_full (g_file_hash,
        (GEqualFunc) g_file_equal, gst_object_unref, gst_object_unref);
  }
}

static void
ges_uri_clip_asset_init (GESUriClipAsset * self)
{
  GESUriClipAssetPrivate *priv;

  priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_URI_CLIP_ASSET, GESUriClipAssetPrivate);

  priv->info = NULL;
  priv->duration = GST_CLOCK_TIME_NONE;
  priv->is_image = FALSE;
}

static void
_create_uri_source_asset (GESUriClipAsset * asset,
    GstDiscovererStreamInfo * sinfo, GESTrackType type)
{
  GESAsset *tck_filesource_asset;
  GESUriSourceAssetPrivate *priv_tckasset;
  GESUriClipAssetPrivate *priv = asset->priv;
  gchar *stream_id =
      g_strdup (gst_discoverer_stream_info_get_stream_id (sinfo));

  if (stream_id == NULL) {
    GST_WARNING ("No stream ID found, using the pointer instead");

    stream_id = g_strdup_printf ("%i", GPOINTER_TO_INT (sinfo));
  }

  if (type == GES_TRACK_TYPE_VIDEO)
    tck_filesource_asset = ges_asset_request (GES_TYPE_VIDEO_URI_SOURCE,
        stream_id, NULL);
  else
    tck_filesource_asset = ges_asset_request (GES_TYPE_AUDIO_URI_SOURCE,
        stream_id, NULL);
  g_free (stream_id);

  priv_tckasset = GES_URI_SOURCE_ASSET (tck_filesource_asset)->priv;
  priv_tckasset->uri = ges_asset_get_id (GES_ASSET (asset));
  priv_tckasset->sinfo = gst_object_ref (sinfo);
  priv_tckasset->parent_asset = asset;
  ges_track_element_asset_set_track_type (GES_TRACK_ELEMENT_ASSET
      (tck_filesource_asset), type);

  priv->asset_trackfilesources = g_list_append (priv->asset_trackfilesources,
      gst_object_ref (tck_filesource_asset));
}

static void
ges_uri_clip_asset_set_info (GESUriClipAsset * self, GstDiscovererInfo * info)
{
  GList *tmp, *stream_list;

  GESTrackType supportedformats = GES_TRACK_TYPE_UNKNOWN;
  GESUriClipAssetPrivate *priv = GES_URI_CLIP_ASSET (self)->priv;

  /* Extract infos from the GstDiscovererInfo */
  stream_list = gst_discoverer_info_get_stream_list (info);
  for (tmp = stream_list; tmp; tmp = tmp->next) {
    GESTrackType type = GES_TRACK_TYPE_UNKNOWN;
    GstDiscovererStreamInfo *sinf = (GstDiscovererStreamInfo *) tmp->data;

    if (GST_IS_DISCOVERER_AUDIO_INFO (sinf)) {
      if (supportedformats == GES_TRACK_TYPE_UNKNOWN)
        supportedformats = GES_TRACK_TYPE_AUDIO;
      else
        supportedformats |= GES_TRACK_TYPE_AUDIO;

      type = GES_TRACK_TYPE_AUDIO;
    } else if (GST_IS_DISCOVERER_VIDEO_INFO (sinf)) {
      if (supportedformats == GES_TRACK_TYPE_UNKNOWN)
        supportedformats = GES_TRACK_TYPE_VIDEO;
      else
        supportedformats |= GES_TRACK_TYPE_VIDEO;
      if (gst_discoverer_video_info_is_image ((GstDiscovererVideoInfo *)
              sinf))
        priv->is_image = TRUE;
      type = GES_TRACK_TYPE_VIDEO;
    }

    GST_DEBUG_OBJECT (self, "Creating GESUriSourceAsset for stream: %s",
        gst_discoverer_stream_info_get_stream_id (sinf));
    _create_uri_source_asset (self, sinf, type);
  }
  ges_clip_asset_set_supported_formats (GES_CLIP_ASSET
      (self), supportedformats);

  if (stream_list)
    gst_discoverer_stream_info_list_free (stream_list);

  if (priv->is_image == FALSE)
    priv->duration = gst_discoverer_info_get_duration (info);
  /* else we keep #GST_CLOCK_TIME_NONE */

  priv->info = gst_object_ref (info);
}

static void
_set_meta_foreach (const GstTagList * tags, const gchar * tag,
    GESMetaContainer * container)
{
  GValue value = { 0 };

  if (gst_tag_list_copy_value (&value, tags, tag)) {
    ges_meta_container_set_meta (container, tag, &value);
    g_value_unset (&value);
  } else {
    GST_INFO ("Could not set metadata: %s", tag);
  }
}

static void
discoverer_discovered_cb (GstDiscoverer * discoverer,
    GstDiscovererInfo * info, GError * err, gpointer user_data)
{
  GError *error = NULL;
  const GstTagList *tags;

  const gchar *uri = gst_discoverer_info_get_uri (info);
  GESUriClipAsset *mfs =
      GES_URI_CLIP_ASSET (ges_asset_cache_lookup (GES_TYPE_URI_CLIP, uri));

  tags = gst_discoverer_info_get_tags (info);
  if (tags)
    gst_tag_list_foreach (tags, (GstTagForeachFunc) _set_meta_foreach, mfs);

  if (gst_discoverer_info_get_result (info) == GST_DISCOVERER_OK) {
    ges_uri_clip_asset_set_info (mfs, info);
  } else {
    if (err) {
      error = g_error_copy (err);
    } else {
      error = g_error_new (GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
          "Stream %s discovering failed (error code: %d)",
          uri, gst_discoverer_info_get_result (info));
    }
  }

  ges_asset_cache_set_loaded (GES_TYPE_URI_CLIP, uri, error);

  if (error)
    g_error_free (error);
}

/* API implementation */
/**
 * ges_uri_clip_asset_get_info:
 * @self: Target asset
 *
 * Gets #GstDiscovererInfo about the file
 *
 * Returns: (transfer none): #GstDiscovererInfo of specified asset
 */
GstDiscovererInfo *
ges_uri_clip_asset_get_info (const GESUriClipAsset * self)
{
  g_return_val_if_fail (GES_IS_URI_CLIP_ASSET (self), NULL);

  return self->priv->info;
}

/**
 * ges_uri_clip_asset_get_duration:
 * @self: a #GESUriClipAsset
 *
 * Gets duration of the file represented by @self
 *
 * Returns: The duration of @self
 */
GstClockTime
ges_uri_clip_asset_get_duration (GESUriClipAsset * self)
{
  g_return_val_if_fail (GES_IS_URI_CLIP_ASSET (self), GST_CLOCK_TIME_NONE);

  return self->priv->duration;
}

/**
 * ges_uri_clip_asset_is_image:
 * @self: a #indent: Standard input:311: Error:Unexpected end of file
GESUriClipAsset
 *
 * Gets Whether the file represented by @self is an image or not
 *
 * Returns: Whether the file represented by @self is an image or not
 */
gboolean
ges_uri_clip_asset_is_image (GESUriClipAsset * self)
{
  g_return_val_if_fail (GES_IS_URI_CLIP_ASSET (self), FALSE);

  return self->priv->is_image;
}

/**
 * ges_uri_clip_asset_new:
 * @uri: The URI of the file for which to create a #GESUriClipAsset
 * @cancellable: optional %GCancellable object, %NULL to ignore.
 * @callback: (scope async): a #GAsyncReadyCallback to call when the initialization is finished
 * @user_data: The user data to pass when @callback is called
 *
 * Creates a #GESUriClipAsset for @uri
 *
 * Example of request of a GESUriClipAsset:
 * |[
 * // The request callback
 * static void
 * filesource_asset_loaded_cb (GESAsset * source, GAsyncResult * res, gpointer user_data)
 * {
 *   GError *error = NULL;
 *   GESUriClipAsset *filesource_asset;
 *
 *   filesource_asset = GES_URI_CLIP_ASSET (ges_asset_request_finish (res, &error));
 *   if (filesource_asset) {
 *    g_print ("The file: %s is usable as a FileSource, it is%s an image and lasts %" GST_TIME_FORMAT,
 *        ges_asset_get_id (GES_ASSET (filesource_asset))
 *        ges_uri_clip_asset_is_image (filesource_asset) ? "" : " not",
 *        GST_TIME_ARGS (ges_uri_clip_asset_get_duration (filesource_asset));
 *   } else {
 *    g_print ("The file: %s is *not* usable as a FileSource because: %s",
 *        ges_asset_get_id (source), error->message);
 *   }
 *
 *   gst_object_unref (mfs);
 * }
 *
 * // The request:
 * ges_uri_clip_asset_new (uri, (GAsyncReadyCallback) filesource_asset_loaded_cb, user_data);
 * ]|
 */
void
ges_uri_clip_asset_new (const gchar * uri, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  ges_asset_request_async (GES_TYPE_URI_CLIP, uri, cancellable,
      callback, user_data);
}

/**
 * ges_uri_clip_asset_request_sync:
 * @uri: The URI of the file for which to create a #GESUriClipAsset.
 * You can also use multi file uris for #GESMultiFileSource.
 * @error: (allow-none): An error to be set in case something wrong happens or %NULL
 *
 * Creates a #GESUriClipAsset for @uri syncronously. You should avoid
 * to use it in application, and rather create #GESUriClipAsset asynchronously
 *
 * Returns: (transfer none): A reference to the requested asset or %NULL if an error happend
 */
GESUriClipAsset *
ges_uri_clip_asset_request_sync (const gchar * uri, GError ** error)
{
  GError *lerror = NULL;
  GstDiscovererInfo *info;
  GstDiscoverer *discoverer;
  GESUriClipAsset *asset;
  gchar *first_file, *first_file_uri;

  asset = GES_URI_CLIP_ASSET (ges_asset_request (GES_TYPE_URI_CLIP, uri,
          &lerror));

  if (asset)
    return asset;

  if (lerror && lerror->domain == GES_ERROR &&
      lerror->code == GES_ERROR_ASSET_WRONG_ID) {
    g_propagate_error (error, lerror);

    return NULL;
  }

  asset = g_object_new (GES_TYPE_URI_CLIP_ASSET, "id", uri,
      "extractable-type", GES_TYPE_URI_CLIP, NULL);
  discoverer = GES_URI_CLIP_ASSET_GET_CLASS (asset)->sync_discoverer;

  if (g_str_has_prefix (uri, GES_MULTI_FILE_URI_PREFIX)) {
    GESMultiFileURI *uri_data;

    uri_data = ges_multi_file_uri_new (uri);
    first_file = g_strdup_printf (uri_data->location, uri_data->start);
    first_file_uri = gst_filename_to_uri (first_file, &lerror);
    info = gst_discoverer_discover_uri (discoverer, first_file_uri, &lerror);
    GST_DEBUG ("Got multifile uri. Discovering first file %s", first_file_uri);
    g_free (uri_data);
    g_free (first_file_uri);
    g_free (first_file);
  } else {
    info = gst_discoverer_discover_uri (discoverer, uri, &lerror);
  }

  ges_asset_cache_put (gst_object_ref (asset), NULL);
  ges_uri_clip_asset_set_info (asset, info);
  ges_asset_cache_set_loaded (GES_TYPE_URI_CLIP, uri, lerror);

  if (info == NULL || lerror != NULL) {
    gst_object_unref (asset);
    if (lerror)
      g_propagate_error (error, lerror);

    return NULL;
  }

  return asset;
}

/**
 * ges_uri_clip_asset_class_set_timeout:
 * @klass: The #GESUriClipAssetClass on which to set the discoverer timeout
 * @timeout: The timeout to set
 *
 * Sets the timeout of #GESUriClipAsset loading
 */
void
ges_uri_clip_asset_class_set_timeout (GESUriClipAssetClass * klass,
    GstClockTime timeout)
{
  g_return_if_fail (GES_IS_URI_CLIP_ASSET_CLASS (klass));

  g_object_set (klass->discoverer, "timeout", timeout, NULL);
  g_object_set (klass->sync_discoverer, "timeout", timeout, NULL);
}

/**
 * ges_uri_clip_asset_get_stream_assets:
 * @self: A #GESUriClipAsset
 *
 * Get the GESUriSourceAsset @self containes
 *
 * Returns: (transfer none) (element-type GESUriSourceAsset): a
 * #GList of #GESUriSourceAsset
 */
const GList *
ges_uri_clip_asset_get_stream_assets (GESUriClipAsset * self)
{
  g_return_val_if_fail (GES_IS_URI_CLIP_ASSET (self), FALSE);

  return self->priv->asset_trackfilesources;
}

/*****************************************************************
 *            GESUriSourceAsset implementation             *
 *****************************************************************/
/**
 * SECTION: gesurisourceasset
 * @short_description: A GESAsset subclass specialized in GESUriSource extraction
 *
 * NOTE: You should never request such a #GESAsset as they will be created automatically
 * by #GESUriClipAsset-s.
 */

G_DEFINE_TYPE (GESUriSourceAsset, ges_uri_source_asset,
    GES_TYPE_TRACK_ELEMENT_ASSET);

static GESExtractable *
_extract (GESAsset * asset, GError ** error)
{
  gchar *uri = NULL;
  GESTrackElement *trackelement;
  GESUriSourceAssetPrivate *priv = GES_URI_SOURCE_ASSET (asset)->priv;

  if (GST_IS_DISCOVERER_STREAM_INFO (priv->sinfo) == FALSE) {
    GST_WARNING_OBJECT (asset, "Can not extract as no strean info set");

    return NULL;
  }

  if (priv->uri == NULL) {
    GST_WARNING_OBJECT (asset, "Can not extract as no uri set");

    return NULL;
  }

  uri = g_strdup (priv->uri);

  if (g_str_has_prefix (priv->uri, GES_MULTI_FILE_URI_PREFIX)) {
    trackelement = GES_TRACK_ELEMENT (ges_multi_file_source_new (uri));
  } else if (GST_IS_DISCOVERER_VIDEO_INFO (priv->sinfo)
      && gst_discoverer_video_info_is_image ((GstDiscovererVideoInfo *)
          priv->sinfo))
    trackelement = GES_TRACK_ELEMENT (ges_image_source_new (uri));
  else if (GST_IS_DISCOVERER_VIDEO_INFO (priv->sinfo))
    trackelement = GES_TRACK_ELEMENT (ges_video_uri_source_new (uri));
  else
    trackelement = GES_TRACK_ELEMENT (ges_audio_uri_source_new (uri));

  ges_track_element_set_track_type (trackelement,
      ges_track_element_asset_get_track_type (GES_TRACK_ELEMENT_ASSET (asset)));

  g_free (uri);

  return GES_EXTRACTABLE (trackelement);
}

static void
ges_uri_source_asset_class_init (GESUriSourceAssetClass * klass)
{
  g_type_class_add_private (klass, sizeof (GESUriSourceAssetPrivate));

  GES_ASSET_CLASS (klass)->extract = _extract;
}

static void
ges_uri_source_asset_init (GESUriSourceAsset * self)
{
  GESUriSourceAssetPrivate *priv;

  priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_URI_SOURCE_ASSET, GESUriSourceAssetPrivate);

  priv->sinfo = NULL;
  priv->parent_asset = NULL;
  priv->uri = NULL;
}

/**
 * ges_uri_source_asset_get_stream_info:
 * @asset: A #GESUriClipAsset
 *
 * Get the #GstDiscovererStreamInfo user by @asset
 *
 * Returns: (transfer none): a #GESUriClipAsset
 */
GstDiscovererStreamInfo *
ges_uri_source_asset_get_stream_info (GESUriSourceAsset * asset)
{
  g_return_val_if_fail (GES_IS_URI_SOURCE_ASSET (asset), NULL);

  return asset->priv->sinfo;
}

const gchar *
ges_uri_source_asset_get_stream_uri (GESUriSourceAsset * asset)
{
  g_return_val_if_fail (GES_IS_URI_SOURCE_ASSET (asset), NULL);

  return asset->priv->uri;
}

/**
 * ges_uri_source_asset_get_filesource_asset:
 * @asset: A #GESUriClipAsset
 *
 * Get the #GESUriClipAsset @self is contained in
 *
 * Returns: a #GESUriClipAsset
 */
const GESUriClipAsset *
ges_uri_source_asset_get_filesource_asset (GESUriSourceAsset * asset)
{
  g_return_val_if_fail (GES_IS_URI_SOURCE_ASSET (asset), NULL);

  return asset->priv->parent_asset;
}
